"use strict";
const fs = require("node:fs/promises");
const path = require("node:path");
const { createHash, randomUUID } = require("node:crypto");
const { atomicWrite } = require("./store.cjs");
const FIELDS = {
  outerResolution: ["FoveatedOuterPctRes", 25, 50],
  focusWidth: ["FoveatedInsetWidthPct", 25, 50],
  resolutionScale: ["ResolutionScalePct", 50, 200],
};
const hash = (bytes) => createHash("sha256").update(bytes).digest("hex");
function validateValues(values) {
  if (!values || typeof values !== "object" || Array.isArray(values))
    throw new Error("Invalid native settings.");
  if (Object.keys(values).some((k) => !Object.hasOwn(FIELDS, k)))
    throw new Error("Unknown native setting.");
  for (const [name, [, min, max]] of Object.entries(FIELDS)) {
    if (
      !Number.isInteger(values[name]) ||
      values[name] < min ||
      values[name] > max
    )
      throw new Error(`${name} is outside the supported range ${min}-${max}.`);
  }
}
function parse(bytes) {
  let encoding = "latin1",
    bom = Buffer.alloc(0);
  if (bytes[0] === 255 && bytes[1] === 254) {
    encoding = "utf16le";
    bom = bytes.subarray(0, 2);
  } else if (bytes[0] === 254 && bytes[1] === 255)
    throw new Error("Big-endian game configuration is not supported.");
  else if (bytes.subarray(0, 3).equals(Buffer.from([239, 187, 191]))) {
    encoding = "utf8";
    bom = bytes.subarray(0, 3);
  }
  const text = bytes.subarray(bom.length).toString(encoding);
  if (!Buffer.from(text, encoding).equals(bytes.subarray(bom.length)))
    throw new Error("Invalid game configuration encoding.");
  if (text.includes("\0"))
    throw new Error("Invalid game configuration encoding.");
  const lines = (text.match(/[^\r\n]*(?:\r\n|\r|\n|$)/g) || [])
    .filter(Boolean)
    .map((line) => ({
      body: line.replace(/[\r\n]+$/, ""),
      end: line.match(/(?:\r\n|\r|\n)$/)?.[0] || "",
    }));
  const values = {
      outerResolution: null,
      focusWidth: null,
      resolutionScale: null,
    },
    found = {};
  let start = -1,
    end = lines.length,
    inOpenXR = false;
  const keyNames = new Map(
    Object.entries(FIELDS).map(([name, [key]]) => [key.toLowerCase(), name]),
  );
  for (let i = 0; i < lines.length; i++) {
    const section = lines[i].body.match(/^\s*\[([^\]]+)\]\s*(?:[;#].*)?$/);
    if (section) {
      if (inOpenXR) {
        end = i;
        inOpenXR = false;
      }
      if (section[1].trim().toLowerCase() === "openxr") {
        if (start !== -1)
          throw new Error(
            "Duplicate OpenXR sections require manual cleanup before editing.",
          );
        start = i;
        inOpenXR = true;
      }
      continue;
    }
    if (!inOpenXR) continue;
    const entry = lines[i].body.match(
      /^(\s*([^=;#]+?)\s*=\s*)([^;#]*)([;#].*)?$/,
    );
    if (!entry) continue;
    const name = keyNames.get(entry[2].trim().toLowerCase());
    if (!name) continue;
    if (found[name])
      throw new Error(
        `Duplicate ${FIELDS[name][0]} keys require manual cleanup.`,
      );
    if (!/^\d+(?:\.\d+)?$/.test(entry[3].trim()))
      throw new Error(`Invalid existing value for ${FIELDS[name][0]}.`);
    values[name] = Number(entry[3].trim());
    found[name] = {
      index: i,
      prefix: entry[1],
      tail: (entry[3].match(/\s*$/)?.[0] || "") + (entry[4] || ""),
    };
  }
  if (start === -1)
    throw new Error(
      "The file has no OpenXR section. Start iRacing in OpenXR mode once, then close it.",
    );
  return {
    values,
    rewrite(changes) {
      const next = lines.map((line) => ({ ...line })),
        newline = lines.find((line) => line.end)?.end || "\r\n",
        insert = [];
      for (const [name, value] of Object.entries(changes)) {
        const row = found[name];
        if (row) next[row.index].body = row.prefix + value + row.tail;
        else insert.push({ body: `${FIELDS[name][0]}=${value}`, end: newline });
      }
      if (insert.length) {
        if (end > 0 && !next[end - 1].end) next[end - 1].end = newline;
        next.splice(end, 0, ...insert);
      }
      return Buffer.concat([
        bom,
        Buffer.from(
          next.map((line) => line.body + line.end).join(""),
          encoding,
        ),
      ]);
    },
  };
}
function createNativeSettings({ defaultPath, backupDir, ensureStopped }) {
  let file = path.resolve(defaultPath),
    pending = null,
    busy = false;
  const prefix = () => hash(file.toLowerCase()).slice(0, 16);
  const backupPattern = () =>
    new RegExp(`^${prefix()}-\\d{13}-[0-9a-f-]{36}\\.ini$`);
  async function readCurrent() {
    const stat = await fs.lstat(file);
    if (!stat.isFile() || stat.isSymbolicLink())
      throw new Error("Choose a regular iRacing configuration file.");
    if (stat.size > 1024 * 1024)
      throw new Error("Game configuration exceeds 1 MiB.");
    const bytes = await fs.readFile(file);
    return { bytes, revision: hash(bytes), parsed: parse(bytes) };
  }
  async function backups() {
    const ids = await fs.readdir(backupDir).catch((e) => {
      if (e.code === "ENOENT") return [];
      throw e;
    });
    return ids
      .filter((id) => backupPattern().test(id))
      .sort()
      .reverse()
      .map((id) => ({ id, createdAt: Number(id.split("-")[1]) }));
  }
  async function inspect() {
    try {
      const current = await readCurrent();
      return {
        available: true,
        path: file,
        revision: current.revision,
        values: current.parsed.values,
        backups: await backups(),
        message:
          "Native settings loaded. Saving an OpenGrid profile does not change this file.",
      };
    } catch (e) {
      if (e.code !== "ENOENT") throw e;
      return {
        available: false,
        path: file,
        revision: "missing",
        values: {
          outerResolution: null,
          focusWidth: null,
          resolutionScale: null,
        },
        backups: await backups(),
        message:
          "No rendererDX11OpenXR.ini found. Start iRacing once in OpenXR mode, close the sim, then locate the generated file.",
      };
    }
  }
  async function choose(selected) {
    if (busy) throw new Error("A game-settings operation is already running.");
    if (
      typeof selected !== "string" ||
      path.basename(selected).toLowerCase() !== "rendererdx11openxr.ini"
    )
      throw new Error("Choose rendererDX11OpenXR.ini.");
    const previous = file;
    file = path.resolve(selected);
    pending = null;
    try {
      return await inspect();
    } catch (e) {
      file = previous;
      throw e;
    }
  }
  async function preview(values, revision) {
    if (busy) throw new Error("A game-settings operation is already running.");
    pending = null;
    validateValues(values);
    const state = await inspect();
    if (!state.available) throw new Error(state.message);
    if (state.revision !== revision)
      throw new Error(
        "The game configuration changed. Read it again before reviewing changes.",
      );
    const current = await readCurrent();
    if (current.revision !== revision)
      throw new Error("The game configuration changed. Read it again.");
    const changes = Object.entries(FIELDS)
      .filter(([name]) => state.values[name] !== values[name])
      .map(([name, [key]]) => ({
        key,
        before: state.values[name],
        after: values[name],
      }));
    const token = randomUUID();
    pending = {
      token,
      revision,
      file,
      bytes: current.parsed.rewrite(values),
      changes,
    };
    return {
      token,
      path: file,
      changes,
      message:
        "Only these three documented OpenXR settings change. An exact backup is created before applying. Close iRacing first.",
    };
  }
  async function commit(bytes, revision) {
    if (busy) throw new Error("A game-settings operation is already running.");
    busy = true;
    try {
      await ensureStopped();
      const current = await readCurrent();
      if (current.revision !== revision)
        throw new Error(
          "The game configuration changed after review. Review it again.",
        );
      if (current.bytes.equals(bytes)) return await inspect();
      await fs.mkdir(backupDir, { recursive: true });
      const backup = path.join(
        backupDir,
        `${prefix()}-${Date.now()}-${randomUUID()}.ini`,
      );
      await fs.writeFile(backup, current.bytes, { flag: "wx" });
      // Protect a change made while the backup was being written, too.
      if (hash(await fs.readFile(file)) !== revision)
        throw new Error(
          "The game configuration changed during backup. Nothing was applied.",
        );
      await atomicWrite(file, bytes);
      return await inspect();
    } finally {
      busy = false;
    }
  }
  async function apply(token) {
    if (
      typeof token !== "string" ||
      !pending ||
      token !== pending.token ||
      file !== pending.file
    )
      throw new Error("Review the game changes before applying them.");
    const operation = pending;
    pending = null;
    return commit(operation.bytes, operation.revision);
  }
  async function restore(id, revision) {
    if (typeof id !== "string" || !backupPattern().test(id))
      throw new Error("Invalid backup ID.");
    const backup = path.join(backupDir, id),
      stat = await fs.lstat(backup);
    if (!stat.isFile() || stat.isSymbolicLink() || stat.size > 1024 * 1024)
      throw new Error("Invalid game-settings backup.");
    const bytes = await fs.readFile(backup);
    parse(bytes);
    pending = null;
    return commit(bytes, revision);
  }
  return { inspect, choose, preview, apply, restore };
}
module.exports = { createNativeSettings };
