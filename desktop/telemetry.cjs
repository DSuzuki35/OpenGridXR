"use strict";
const fs = require("node:fs/promises");
const { spawn } = require("node:child_process");
const GAMES = new Set(["iracing", "lmu"]);
const NUMBERS = ["speedMps", "gear", "rpm", "throttle", "brake", "clutch", "fuelLiters", "lap", "lapsCompleted", "lapTimeSeconds", "lastLapSeconds", "bestLapSeconds"];
const FLAGS = ["yellow", "blue", "green", "white", "checkered", "red", "black"];
function validateRecord(value, game) {
  if (!value || value.schemaVersion !== 1 || value.game !== game ||
      value.source !== (game === "iracing" ? "iracing-sdk" : "lmu-native") ||
      !["live", "stale", "disconnected", "invalid", "unsupported"].includes(value.status) ||
      typeof value.connected !== "boolean" || typeof value.stale !== "boolean" ||
      !Number.isSafeInteger(value.sequence) || value.sequence < 0 ||
      !Number.isFinite(value.timestampMs) ||
      !(value.ageMs === null || (Number.isFinite(value.ageMs) && value.ageMs >= 0)) ||
      !value.data || typeof value.data !== "object") throw new Error("Invalid telemetry record.");
  const data = {};
  for (const key of NUMBERS) {
    const v = value.data[key];
    if (!(v === null || (typeof v === "number" && Number.isFinite(v)))) throw new Error("Invalid telemetry value.");
    data[key] = v;
  }
  for (const key of ['lateralAccelMps2','longitudinalAccelMps2','verticalAccelMps2']) {
    const v=value.data[key];
    if(v===undefined)continue;
    if(v!==null && (typeof v!=='number'||!Number.isFinite(v)||Math.abs(v)>200))throw new Error('Invalid acceleration telemetry.');
    data[key]=v;
  }
  const contextRanges={airTempC:[-100,100],trackTempC:[-100,150],rainIntensity:[0,1],trackWetness:[0,1],sessionTimeSeconds:[0,1e9],timeOfDaySeconds:[0,86400],solarAltitudeRadians:[-1.570797,1.570797],carsNearby:[0,127],carsVisible:[0,127],carsNearbyRadiusMeters:[100,100]};
  for(const [key,[lo,hi]] of Object.entries(contextRanges)) {
    const v=value.data[key];if(v===undefined)continue;
    if(v!==null && (typeof v!=='number'||!Number.isFinite(v)||v<lo||v>hi||(['carsNearby','carsVisible','carsNearbyRadiusMeters'].includes(key)&&!Number.isInteger(v))))throw new Error('Invalid telemetry context.');
    data[key]=v;
  }
  const categories={cloudCover:['clear','light-clouds','partly-cloudy','mostly-cloudy','overcast','cloudy-drizzle','cloudy-light-rain','overcast-light-rain','overcast-rain','overcast-heavy-rain','overcast-storm'],trackWetnessState:['dry','mostly-dry','very-lightly-wet','lightly-wet','moderately-wet','very-wet','extremely-wet'],carsNearbyMethod:['world-distance','track-distance']};
  for(const [key,allowed] of Object.entries(categories)) {
    const v=value.data[key];if(v===undefined)continue;
    if(v!==null&&!allowed.includes(v))throw new Error('Invalid telemetry context category.');
    data[key]=v;
  }
  for(const key of ['trackName','sessionId']) {
    const v=value.data[key];if(v===undefined)continue;
    if(v!==null&&(typeof v!=='string'||v.length<1||v.length>116||/[^\x20-\x7e]/.test(v)))throw new Error('Invalid telemetry context text.');
    data[key]=v;
  }
  data.flags = {};
  for (const key of FLAGS) {
    const v = value.data.flags?.[key];
    if (v !== null && typeof v !== "boolean") throw new Error("Invalid telemetry flag.");
    data.flags[key] = v;
  }
  if (value.data.inPits !== null && typeof value.data.inPits !== "boolean") throw new Error("Invalid pit state.");
  data.inPits = value.data.inPits;
  if (value.status === "live" && (!value.connected || value.stale)) throw new Error("Inconsistent live telemetry.");
  return { schemaVersion: 1, game, source: value.source, status: value.status,
    connected: value.connected, stale: value.stale, sequence: value.sequence,
    timestampMs: value.timestampMs, ageMs: value.ageMs, data,
    reason: typeof value.reason === "string" ? value.reason.slice(0, 500) : null,
    testData: value.testData === true };
}

function createTelemetry({ executable, spawnProcess = spawn, now = Date.now, checkFile = fs.access, testNamespace } = {}) {
  const states = new Map();
  let closed = false;
  const stateFor = (game) => {
    if (!GAMES.has(game)) throw new Error("Unsupported telemetry game.");
    if (!states.has(game)) states.set(game, { game, child: null, starting: false, generation: 0, lease: 0, rate: 20, record: null, received: 0, retryAt: 0, message: "Telemetry is off.", available: true, enabled: false });
    return states.get(game);
  };
  function stop(s) {
    s.generation++;
    s.starting = false;
    const child = s.child;
    s.child = null;
    if (child) { child.stdin?.end(); child.kill(); }
  }
  async function start(s) {
    if (closed || s.child || s.starting || now() < s.retryAt) return;
    s.starting = true;
    const generation = ++s.generation;
    try {
      await checkFile(executable);
      if (closed || generation !== s.generation || now() > s.lease) return;
      s.available = true;
      const args = ["--game", s.game, "--rate", String(s.rate)];
      if (testNamespace) args.push("--test-namespace", testNamespace);
      const child = spawnProcess(executable, args, { windowsHide: true, stdio: ["pipe", "pipe", "pipe"] });
      s.child = child;
      s.message = "Waiting for the sim's shared memory.";
      let buffer = "", stderr = "";
      const fail = (message) => {
        if (generation !== s.generation) return;
        s.message = message; s.retryAt = now() + 5000;
        stop(s);
      };
      child.stdout.setEncoding("utf8");
      child.stdout.on("data", (chunk) => {
        if (generation !== s.generation) return;
        buffer += chunk;
        // Bound both incomplete and complete messages before parsing.
        if (buffer.length > 65536) return fail("Telemetry output exceeded its limit.");
        let newline;
        while ((newline = buffer.indexOf("\n")) >= 0) {
          const line = buffer.slice(0, newline).trim(); buffer = buffer.slice(newline + 1);
          if (!line) continue;
          if (line.length > 16384) return fail("Telemetry record exceeded its limit.");
          try {
            const record = validateRecord(JSON.parse(line), s.game);
            if (record.testData && !testNamespace) throw new Error("Unexpected fixture data.");
            s.record = record; s.received = now();
            s.message = record.reason || (record.status === "live" ? "Live read-only telemetry." : `Telemetry ${record.status}.`);
          } catch { return fail("The telemetry helper returned an invalid record."); }
        }
        if (buffer.length > 16384) fail("Telemetry record exceeded its limit.");
      });
      child.stderr.setEncoding("utf8");
      child.stderr.on("data", (chunk) => { stderr = (stderr + chunk).slice(-4096); });
      child.on("error", () => fail("The telemetry helper could not start."));
      child.on("exit", () => fail(stderr ? "The telemetry helper stopped with an error." : "The telemetry helper stopped."));
      child.stdin?.on("error", () => {});
    } catch {
      if (generation === s.generation) { s.available = false; s.message = "Build or install the telemetry helper to use this feature."; s.retryAt = now() + 5000; }
    } finally { if (generation === s.generation) s.starting = false; }
  }
  function reap() {
    for (const s of states.values()) if ((s.child || s.starting) && now() > s.lease) {
      stop(s); s.message = "Paused while no telemetry view is open.";
    }
  }
  const timer = setInterval(reap, 500);
  timer.unref?.();
  return {
    async status(game, settings = { enabled: false, updateHz: 20 }) {
      if (closed) throw new Error("Telemetry controller is closed.");
      if (typeof settings.enabled !== "boolean" || !Number.isInteger(settings.updateHz) || settings.updateHz < 1 || settings.updateHz > 30) throw new Error("Invalid telemetry settings.");
      const s = stateFor(game);
      s.enabled = settings.enabled;
      if (!settings.enabled) { stop(s); s.record = null; s.message = "Telemetry is off. Enable and save the profile to connect."; }
      else {
        if (s.rate !== settings.updateHz) { stop(s); s.retryAt = 0; }
        s.rate = settings.updateHz; s.lease = now() + 2000;
        await start(s);
      }
      let record = s.record && structuredClone(s.record);
      if (record?.status === "live" && (!s.child || now() - s.received > 1500)) {
        record.status = "stale"; record.stale = true;
        record.ageMs = (record.ageMs || 0) + Math.max(0, now() - s.received);
        record.reason = "No fresh telemetry has arrived.";
      }
      return { available: s.available, running: !!s.child, enabled: s.enabled, requestedHz: s.rate, record, message: s.message };
    },
    reap,
    close() { closed = true; clearInterval(timer); for (const s of states.values()) stop(s); },
  };
}
module.exports = { createTelemetry, validateRecord };
