"use strict";
const { spawn } = require("node:child_process");
function createOverlayHost({ executable, game, spawnProcess = spawn, onChange = () => {}, onSlotReady = () => {}, testMode = false }) {
  let child = null, buffer = "", stopped = false, frameId = 0;
  const pending = new Map();
  const dirtySlots = new Set();
  let state = { available: false, activeOverlayCount: 0, capacity: 0, streamActive: false, error: "Waiting for an OpenXR overlay receiver.", slots: [], frameErrors: {} };
  const release = (entry) => { if (!entry) return; clearTimeout(entry.timeout); try { entry.texture.release(); } catch {} };
  function send(message) {
    if (message.op === "clear") { delete state.frameErrors[message.slot]; dirtySlots.delete(message.slot); }
    if (!child || child.killed || stopped) return false;
    const line = JSON.stringify(message) + "\n";
    if (Buffer.byteLength(line) > 4096) throw new Error("Overlay command exceeds its limit.");
    child.stdin.write(line); return true;
  }
  function fail(message) {
    state = { ...state, available: false, error: message };
    onChange(state); child?.kill();
  }
  return {
    start() {
      if (child || stopped) return;
      child = spawnProcess(executable, ["--game", game, "--parent-pid", String(process.pid), ...(testMode ? ["--test-namespace"] : [])], { windowsHide: true, stdio: ["pipe", "pipe", "pipe"] });
      child.stdout.setEncoding("utf8");
      child.stdout.on("data", (chunk) => {
        buffer += chunk;
        if (buffer.length > 65536) return fail("Overlay host output exceeded its limit.");
        let end;
        while ((end = buffer.indexOf("\n")) >= 0) {
          const line = buffer.slice(0, end); buffer = buffer.slice(end + 1);
          if (line.length > 16384) return fail("Overlay host record exceeded its limit.");
          if (!line.trim()) continue;
          let record; try { record = JSON.parse(line); } catch { return fail("Invalid overlay host response."); }
          if (record.op === "ack") {
            const entry = pending.get(record.slot);
            if (entry && entry.frameId === record.frameId) {
              pending.delete(record.slot); release(entry);
              if (record.status === "error") state.frameErrors[record.slot] = typeof record.reason === "string" ? record.reason.slice(0, 500) : "GPU texture transfer failed.";
              else if (record.status === "copied") delete state.frameErrors[record.slot];
              const needsRefresh = dirtySlots.delete(record.slot) || record.status === "dropped";
              if (needsRefresh && record.status !== "error" && !stopped) onSlotReady(record.slot);
            }
          } else if (record.op === "status") {
            const wasAvailable = state.available;
            state = { available: record.available === true, activeOverlayCount: Math.max(0, Math.min(15, Number(record.activeOverlayCount) || 0)), capacity: Math.max(0, Math.min(15, Number(record.capacity) || 0)), streamActive: record.streamActive === true,
              error: typeof record.error === "string" ? record.error.slice(0, 500) : "", slots: Array.isArray(record.slots) ? record.slots.slice(0, 15) : [], frameErrors: state.frameErrors };
            if (!wasAvailable && state.available && !stopped) for (const slot of [...dirtySlots]) {
              if (!pending.has(slot)) { dirtySlots.delete(slot); onSlotReady(slot); }
            }
            onChange(state);
          }
        }
      });
      child.stderr.on("data", () => {});
      child.stdin.on("error", () => fail("Overlay host input disconnected."));
      child.on("error", () => fail("Build or install the overlay host first."));
      child.on("close", () => {
        child = null;
        for (const entry of pending.values()) release(entry);
        pending.clear(); state = { ...state, available: false, error: stopped ? "Overlay host stopped." : state.error || "Overlay host exited." }; onChange(state);
      });
    },
    send,
    submit(slot, texture) {
      const info = texture?.textureInfo, handle = info?.handle?.ntHandle;
      if (!Number.isInteger(slot) || slot < 0 || slot > 14 || stopped || info?.widgetType === "popup") { release({ texture }); return false; }
      if (!state.available) { dirtySlots.add(slot); release({ texture }); return false; }
      if (pending.has(slot)) { dirtySlots.add(slot); release({ texture }); return false; }
      if (!Buffer.isBuffer(handle) || ![4, 8].includes(handle.length) || !["bgra", "rgba"].includes(info?.pixelFormat)) { state.frameErrors[slot] = "The source did not provide a supported BGRA/RGBA shared GPU texture."; release({ texture }); return false; }
      const width = info.codedSize?.width, height = info.codedSize?.height;
      if (!Number.isInteger(width) || !Number.isInteger(height) || width < 1 || width > 1920 || height < 1 || height > 1080) { state.frameErrors[slot] = "The source texture exceeds 1920 × 1080."; release({ texture }); return false; }
      const id = ++frameId;
      const entry = { texture, frameId: id, timeout: setTimeout(() => fail("Overlay GPU copy timed out; capture stopped."), 5000) };
      pending.set(slot, entry);
      if (!send({ op: "frame", slot, frameId: id, handle: (handle.length === 8 ? handle.readBigUInt64LE() : BigInt(handle.readUInt32LE())).toString(), width, height, format: info.pixelFormat })) { pending.delete(slot); release(entry); return false; }
      return true;
    },
    state: () => structuredClone(state),
    running: () => !!child && !child.killed && !stopped,
    close() { if (stopped) return; send({ op: "shutdown" }); stopped = true; child?.stdin.end(); child?.kill(); if (!child) { for (const entry of pending.values()) release(entry); pending.clear(); } },
  };
}
module.exports = { createOverlayHost };
