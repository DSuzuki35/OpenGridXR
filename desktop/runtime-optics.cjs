"use strict";
const fs = require("node:fs/promises");
const path = require("node:path");
const { createHash, randomUUID } = require("node:crypto");
const { atomicWrite } = require("./store.cjs");
const MAX_BYTES = 1024 * 1024;
const FIELDS = Object.freeze({
  eyeRotation: { min: -5, max: 15, step: 0.1, unit: "degrees per eye" },
  autoEyeRotation: { type: "boolean" },
  fovZoom: { min: 0.8, max: 1.1, step: 0.005, unit: "factor" },
  flatFovZoom: { min: 0.7, max: 1.15, step: 0.005, unit: "factor" },
});
const CAPABILITIES = {
  sboys: { supported: true, mode: "offline-file", fields: FIELDS, observedRuntime: "unverified", wideFov: false },
  pimaxNative: { supported: false, message: "No verified current Pimax native optics write API. Use Pimax Play for its Wide FOV mode." },
};
const digest = bytes => createHash("sha256").update(bytes).digest("hex");
const object = value => value !== null && typeof value === "object" && !Array.isArray(value);

// Parse spans as well as values: replacing only the selected scalar tokens keeps
// unrelated formatting and large numeric literals byte-exact. Reject duplicate
// members (including escaped aliases) before any edit or backup restore.
function parse(bytes) {
  const bom = bytes.subarray(0, 3).equals(Buffer.from([239, 187, 191])) ? bytes.subarray(0, 3) : Buffer.alloc(0);
  const body = bytes.subarray(bom.length), text = body.toString("utf8");
  if (!Buffer.from(text, "utf8").equals(body)) throw new Error("Invalid UTF-8 JSON encoding.");
  let index = 0, nodes = 0;
  const space = () => { while (/[\x20\t\r\n]/.test(text[index] || "!")) index++; };
  const fail = () => { throw new Error("Invalid settings JSON."); };
  function string() {
    const start = index++;
    while (index < text.length) {
      const char = text[index++];
      if (char === '"') {
        try { return JSON.parse(text.slice(start, index)); } catch { fail(); }
      }
      if (char === "\\") index++;
    }
    fail();
  }
  function value(depth) {
    if (depth > 64) throw new Error("Settings JSON exceeds the supported depth.");
    if (++nodes > 50000) throw new Error("Settings JSON contains too many values.");
    space();
    const start = index, char = text[index];
    let result, members = null;
    if (char === "{" || char === "[") {
      const isObject = char === "{", close = isObject ? "}" : "]";
      result = isObject ? Object.create(null) : [];
      members = isObject ? new Map() : [];
      index++; space();
      if (text[index] !== close) {
        while (true) {
          let key;
          if (isObject) {
            if (text[index] !== '"') fail();
            key = string(); space();
            if (members.has(key)) throw new Error(`Duplicate JSON member: ${key}.`);
            if (text[index++] !== ":") fail();
          }
          const child = value(depth + 1);
          if (isObject) { members.set(key, child); result[key] = child.value; }
          else { members.push(child); result.push(child.value); }
          space();
          if (text[index] === close) break;
          if (text[index++] !== ",") fail();
          space();
        }
      }
      index++;
    } else if (char === '"') result = string();
    else {
      const token = text.slice(index).match(/^(?:true|false|null|-?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][+-]?\d+)?)/)?.[0];
      if (!token) fail();
      index += token.length;
      result = JSON.parse(token);
    }
    return { start, end: index, value: result, members };
  }
  const root = value(0); space();
  if (index !== text.length || !object(root.value)) throw new Error("Settings JSON must be one object.");
  return { root, text, bom };
}

function targetNode(parsed, headset) {
  const node = parsed.root.members.get(headset);
  if (!node || !object(node.value)) throw new Error(`The selected headset ${headset} must exist as a JSON object.`);
  return node;
}
function validateChanges(changes) {
  if (!object(changes) || !Object.keys(changes).length) throw new Error("Invalid or empty optics changes.");
  for (const [key, value] of Object.entries(changes)) {
    if (!Object.hasOwn(FIELDS, key)) throw new Error(`Unsupported optics field: ${key}.`);
    const field = FIELDS[key];
    if (field.type === "boolean") {
      if (typeof value !== "boolean") throw new Error(`${key} must be boolean.`);
    } else if (typeof value !== "number" || !Number.isFinite(value) || value < field.min || value > field.max)
      throw new Error(`${key} must be finite and in the supported UI range ${field.min}-${field.max}.`);
  }
}
function rewrite(parsed, headset, changes) {
  const node = targetNode(parsed, headset), edits = [], additions = [];
  for (const [key, value] of Object.entries(changes)) {
    const existing = node.members.get(key);
    if (existing) edits.push({ start: existing.start, end: existing.end, text: JSON.stringify(value) });
    else additions.push(`${JSON.stringify(key)}: ${JSON.stringify(value)}`);
  }
  if (additions.length) edits.push({ start: node.end - 1, end: node.end - 1, text: `${node.members.size ? ", " : ""}${additions.join(", ")}` });
  let next = parsed.text;
  for (const edit of edits.sort((a, b) => b.start - a.start)) next = next.slice(0, edit.start) + edit.text + next.slice(edit.end);
  const bytes = Buffer.concat([parsed.bom, Buffer.from(next, "utf8")]);
  if (bytes.length > MAX_BYTES) throw new Error("Updated settings exceed 1 MiB.");
  parse(bytes);
  return bytes;
}
function display(node, parsed) {
  if (!node) return null;
  const value = node.value;
  if (value === null || typeof value === "string" || typeof value === "boolean") return value;
  if (typeof value === "number" && Number.isFinite(value) && (!Number.isInteger(value) || Number.isSafeInteger(value))) return value;
  return parsed.text.slice(node.start, node.end);
}
function fullDiff(before, after) {
  const changes = [];
  function visit(a, b, key) {
    if (a && b && before.text.slice(a.start, a.end) === after.text.slice(b.start, b.end)) return;
    if (a?.members instanceof Map && b?.members instanceof Map) {
      for (const name of new Set([...a.members.keys(), ...b.members.keys()])) visit(a.members.get(name), b.members.get(name), key ? `${key}.${name}` : name);
    } else changes.push({ key, before: display(a, before), after: display(b, after), beforePresent: !!a, afterPresent: !!b,
      beforeJson: a ? before.text.slice(a.start, a.end) : null, afterJson: b ? after.text.slice(b.start, b.end) : null });
  }
  visit(before.root, after.root, "");
  return changes;
}

function createRuntimeOptics({ appData, backupDir, ensureStopped }) {
  if (typeof appData !== "string" || !appData || typeof backupDir !== "string" || typeof ensureStopped !== "function")
    throw new Error("Optics discovery requires appData, backupDir and a runtime-stop check.");
  const locations = { neutral: path.resolve(appData, "CustomHeadset", "settings.json"), pimax: path.resolve(appData, "Pimax", "CustomHeadset", "settings.json") };
  let selected = null, pending = null, busy = false;
  const label = variant => variant === "neutral" ? "Sboys neutral build" : "Sboys VENDOR_PIMAX build";
  async function exclusive(action) {
    if (busy) throw new Error("An optics operation is already running.");
    busy = true;
    try { return await action(); } finally { busy = false; }
  }
  async function read(file) {
    const stat = await fs.lstat(file);
    if (!stat.isFile() || stat.isSymbolicLink()) throw new Error("Optics settings must be a regular file, not a link.");
    if (stat.size > MAX_BYTES) throw new Error("Optics settings exceed 1 MiB.");
    const handle = await fs.open(file, "r");
    let bytes;
    try {
      const buffer = Buffer.alloc(MAX_BYTES + 1);
      let count = 0;
      while (count < buffer.length) {
        const chunk = await handle.read(buffer, count, buffer.length - count, count);
        if (!chunk.bytesRead) break;
        count += chunk.bytesRead;
      }
      if (count > MAX_BYTES) throw new Error("Optics settings exceed 1 MiB.");
      bytes = buffer.subarray(0, count);
    } finally { await handle.close(); }
    return { bytes, revision: digest(bytes), parsed: parse(bytes) };
  }
  function context() {
    if (!selected) throw new Error("Select a configuration variant and headset first.");
    return { ...selected, file: locations[selected.variant] };
  }
  const backupPrefix = file => digest(file.toLowerCase()).slice(0, 16);
  const backupPattern = file => new RegExp(`^${backupPrefix(file)}-\\d{13}-[0-9a-f-]{36}\\.json$`);
  async function backups(file) {
    const names = await fs.readdir(backupDir).catch(error => { if (error.code === "ENOENT") return []; throw error; });
    return names.filter(name => backupPattern(file).test(name)).sort().reverse().slice(0, 100).map(id => ({ id, createdAt: Number(id.split("-")[1]) }));
  }
  async function inspectCurrent() {
    const candidates = await Promise.all(Object.entries(locations).map(async ([variant, file]) => {
      try {
        const stat = await fs.lstat(file);
        return { variant, label: label(variant), path: file, exists: true, regular: stat.isFile() && !stat.isSymbolicLink() };
      } catch (error) { if (error.code !== "ENOENT") throw error; return { variant, label: label(variant), path: file, exists: false, regular: false }; }
    }));
    const base = { candidates, selected: selected && { ...selected }, capabilities: CAPABILITIES, observedRuntime: "unverified", available: false, backups: [] };
    if (!selected) return { ...base, message: "Select the intended build and headset. File presence does not identify the active runtime." };
    const { file, headset } = context();
    try {
      const current = await read(file), node = targetNode(current.parsed, headset);
      const values = Object.fromEntries(Object.keys(FIELDS).map(key => [key, display(node.members.get(key), current.parsed)]));
      return { ...base, available: true, path: file, revision: current.revision, values, backups: await backups(file), message: "Saved configuration read. The active runtime and headset optics have not been verified." };
    } catch (error) {
      if (error.code !== "ENOENT") throw error;
      return { ...base, path: file, revision: "missing", message: "No existing settings file at this path. OpenGrid does not create driver configurations." };
    }
  }
  function stage(current, bytes, changes, kind, ctx) {
    const token = randomUUID();
    pending = { token, revision: current.revision, bytes, ctx };
    return { token, kind, wholeFile: kind === "restore", path: ctx.file, selected: { variant: ctx.variant, headset: ctx.headset }, changes, byteChanges: !current.bytes.equals(bytes), message: kind === "restore" ? "Restore replaces the complete settings file, including both headsets and unrelated preferences. The displaced file is backed up exactly." : "Only the reviewed fields change. An exact original-file backup is saved first. Close the game, SteamVR and driver GUI before applying." };
  }
  return {
    inspect: () => exclusive(inspectCurrent),
    choose: selection => exclusive(async () => {
      pending = null;
      if (!object(selection) || !Object.hasOwn(locations, selection.variant) || !["dreamAir", "dreamAirSE"].includes(selection.headset)) throw new Error("Choose a known configuration variant and headset target.");
      selected = { variant: selection.variant, headset: selection.headset };
      return inspectCurrent();
    }),
    preview: (changes, revision) => exclusive(async () => {
      pending = null;
      const ctx = context(); validateChanges(changes);
      const current = await read(ctx.file), node = targetNode(current.parsed, ctx.headset);
      if (revision !== current.revision) throw new Error("Configuration changed. Refresh and review it again.");
      const diff = Object.entries(changes).filter(([key, value]) => !node.members.has(key) || node.value[key] !== value).map(([key, value]) => {
        const before = node.members.get(key);
        return { key: `${ctx.headset}.${key}`, before: display(before, current.parsed), after: value, beforePresent: !!before, afterPresent: true,
          beforeJson: before ? current.parsed.text.slice(before.start, before.end) : null, afterJson: JSON.stringify(value) };
      });
      const changed = Object.fromEntries(diff.map(change => [change.key.slice(ctx.headset.length + 1), change.after]));
      return stage(current, rewrite(current.parsed, ctx.headset, changed), diff, "apply", ctx);
    }),
    previewRestore: (id, revision) => exclusive(async () => {
      pending = null;
      const ctx = context();
      if (typeof id !== "string" || !backupPattern(ctx.file).test(id)) throw new Error("Invalid optics backup ID.");
      const current = await read(ctx.file);
      if (revision !== current.revision) throw new Error("Configuration changed. Refresh before reviewing restore.");
      const backup = await read(path.join(backupDir, id));
      targetNode(backup.parsed, ctx.headset);
      return stage(current, backup.bytes, fullDiff(current.parsed, backup.parsed), "restore", ctx);
    }),
    apply: token => exclusive(async () => {
      const operation = pending; pending = null;
      if (typeof token !== "string" || !operation || token !== operation.token) throw new Error("Review optics changes before applying.");
      await ensureStopped();
      const { file } = operation.ctx, current = await read(file);
      if (current.revision !== operation.revision) throw new Error("Configuration changed after review. Nothing was applied.");
      if (!current.bytes.equals(operation.bytes)) {
        await fs.mkdir(backupDir, { recursive: true });
        const backup = path.join(backupDir, `${backupPrefix(file)}-${Date.now()}-${randomUUID()}.json`);
        const handle = await fs.open(backup, "wx");
        try { await handle.writeFile(current.bytes); await handle.sync(); } finally { await handle.close(); }
        await ensureStopped();
        if ((await read(file)).revision !== operation.revision) throw new Error("Configuration changed during backup. Nothing was applied.");
        await atomicWrite(file, operation.bytes);
      }
      const state = await inspectCurrent();
      return { ...state, message: "Configuration saved with an exact backup. Restart the driver, SteamVR and game, then verify headset optics. Active runtime behavior remains unverified." };
    }),
  };
}
module.exports = { createRuntimeOptics };
