"use strict";
const { spawn: defaultSpawn } = require("node:child_process");
const DEVICE_ID = /^dinput:\{[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\}$/;

function createInputHost({ executable, onPress = () => {}, onStatus = () => {}, spawn = defaultSpawn, testMode = false, fixture = false } = {}) {
  let child = null, stoppingChild = null, retry = null, stopTimer = null, closed = false, active = false, attempt = 0, generation = 0;
  let state = { active: false, running: false, available: false, devices: [], error: "Input bindings are off.", retryAt: 0 };
  const report = (patch) => { state = { ...state, ...patch }; onStatus(structuredClone(state)); };
  const cancelRetry = () => { if (retry) clearTimeout(retry); retry = null; };
  function schedule() {
    if (!active || closed || child || retry || (testMode && !fixture)) return;
    const delay = Math.min(1000 * 2 ** Math.min(attempt++, 5), 30000);
    report({ retryAt: Date.now() + delay });
    retry = setTimeout(() => { retry = null; start(); }, delay);
    retry.unref?.();
  }
  function stop() {
    if (!child) return;
    const stopping = child;
    stoppingChild = stopping;
    try { stopping.stdin?.end('{"op":"quit"}\n'); } catch {}
    clearTimeout(stopTimer);
    stopTimer = setTimeout(() => { if (child === stopping) stopping.kill(); }, 250);
    stopTimer.unref?.();
  }
  function start() {
    if (!active || closed || child) return;
    if (testMode && !fixture) { report({ available: false, error: "Input capture is disabled in test mode." }); return; }
    const current = ++generation;
    let buffer = "", failed = false, received = false;
    const startedAt = Date.now();
    let spawned;
    const fail = (message) => {
      if (generation !== current || failed || stoppingChild === spawned || !active || closed) return;
      failed = true;
      report({ running: false, available: false, devices: [], error: message });
      try { spawned?.kill(); } catch {}
    };
    try {
      spawned = spawn(executable, fixture ? ["--fixture"] : [], { windowsHide: true, stdio: ["pipe", "pipe", "pipe"] });
      child = spawned;
      report({ running: true, available: false, devices: [], error: "Waiting for input devices.", retryAt: 0 });
      spawned.stdout.setEncoding("utf8");
      spawned.stdout.on("data", (chunk) => {
        if (generation !== current || child !== spawned || stoppingChild === spawned || !active || closed || failed) return;
        if (typeof chunk !== "string" || chunk.length > 131072) return fail("Input helper output exceeded its limit.");
        buffer += chunk;
        if (buffer.length > 131072) return fail("Input helper output exceeded its limit.");
        let end, count = 0;
        while ((end = buffer.indexOf("\n")) >= 0) {
          if (++count > 1024) return fail("Input helper event rate exceeded its limit.");
          const line = buffer.slice(0, end).trim(); buffer = buffer.slice(end + 1);
          if (!line) continue;
          if (line.length > 65536) return fail("Input helper record exceeded its limit.");
          let record;
          try { record = JSON.parse(line); } catch { return fail("Invalid input helper response."); }
          if (!record || typeof record !== "object" || Array.isArray(record)) return fail("Invalid input helper response.");
          if (record.op === "devices") {
            if (!Array.isArray(record.devices) || record.devices.length > 64) return fail("Invalid input device list.");
            const ids = new Set(), devices = [];
            for (const device of record.devices) {
              if (!device || typeof device.id !== "string" || !DEVICE_ID.test(device.id) || ids.has(device.id) ||
                  typeof device.name !== "string" || device.name.length > 512 || /[\x00-\x1f]/.test(device.name) ||
                  !Number.isInteger(device.buttons) || device.buttons < 0 || device.buttons > 128 ||
                  !Number.isInteger(device.povs) || device.povs < 0 || device.povs > 4) return fail("Invalid input device list.");
              ids.add(device.id); devices.push({ id: device.id, name: device.name, buttons: device.buttons, povs: device.povs });
            }
            received = true;
            report({ available: true, devices, error: devices.length ? "" : "No input devices connected." });
          } else if (record.op === "press") {
            const device = state.devices.find((item) => item.id === record.deviceId);
            if (!received || !device || !Number.isInteger(record.index) || record.index < 0 ||
                !["button", "pov"].includes(record.kind) ||
                (record.kind === "button" && (record.index >= device.buttons || record.direction !== undefined)) ||
                (record.kind === "pov" && (record.index >= device.povs || !Number.isInteger(record.direction) || record.direction < 0 || record.direction > 7))) return fail("Invalid input press event.");
            try { onPress({ deviceId: device.id, kind: record.kind, index: record.index, ...(record.kind === "pov" ? { direction: record.direction } : {}) }); }
            catch { return fail("Input action handler failed."); }
          } else return fail("Unknown input helper response.");
        }
        if (buffer.length > 65536) fail("Input helper record exceeded its limit.");
      });
      spawned.stderr.on("data", () => {});
      spawned.stdin?.on("error", () => fail("Input helper disconnected."));
      spawned.on("error", () => fail("Build or install the input helper to use bindings."));
      spawned.on("close", () => {
        if (generation !== current || child !== spawned) return;
        clearTimeout(stopTimer); stopTimer = null;
        const deliberate = stoppingChild === spawned;
        stoppingChild = null; child = null;
        if (!active || closed) report({ running: false, available: false, devices: [], retryAt: 0, error: "Input bindings are off." });
        else {
          if (!failed) report({ running: false, available: false, devices: [], error: "Input helper stopped. Retrying." });
          if (deliberate) start();
          else { if (received && Date.now() - startedAt >= 10000) attempt = 0; schedule(); }
        }
      });
    } catch {
      child = null;
      report({ running: false, available: false, devices: [], error: "Build or install the input helper to use bindings." });
      schedule();
    }
  }
  return {
    setActive(value) {
      if (closed) return;
      if (typeof value !== "boolean") throw new TypeError("Input activation must be a boolean.");
      active = value; report({ active });
      if (active) { if (!retry) start(); }
      else { cancelRetry(); attempt = 0; report({ running: false, available: false, devices: [], retryAt: 0, error: "Input bindings are off." }); stop(); }
    },
    state: () => structuredClone(state),
    close() { if (closed) return; active = false; closed = true; cancelRetry(); report({ active: false, running: false, available: false, devices: [], retryAt: 0, error: "Input bindings are off." }); stop(); },
  };
}
module.exports = { createInputHost };
