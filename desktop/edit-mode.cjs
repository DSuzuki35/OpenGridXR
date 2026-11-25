"use strict";
// RaceLab-style overlay editing in the headset, driven by the OpenGridXR-EditInput helper.
//   Tab          start editing (the first overlay is selected, the others dim)
//   Space        next overlay
//   Mouse        move: left/right/up/down; wheel or right-drag: nearer/farther
//   M            switch between moving and rotating; rotating: mouse turns and tilts, right-drag leans, wheel resizes
//   Shift        fine steps
//   Esc (or Tab) finish: then Enter saves, Esc cancels and puts everything back
const { spawn: defaultSpawn } = require("node:child_process");

const KEYS = ["x", "y", "z", "yaw", "pitch", "roll", "widthMeters"];
const DEFAULT_RANGES = { x: [-5, 5], y: [-5, 5], z: [-10, -0.1], yaw: [-180, 180], pitch: [-180, 180], roll: [-180, 180], widthMeters: [0.1, 3] };
// Sensitivity per mouse count / wheel notch; Shift divides by FINE.
const STEP = { move: 0.0005, depth: 0.001, notch: 0.02, turn: 0.1, scale: 1.03 };
const FINE = 5;

const clamp = (v, [lo, hi]) => Math.max(lo, Math.min(hi, v));
const round = (key, v) => Number(v.toFixed(["yaw", "pitch", "roll"].includes(key) ? 2 : 4));

// Pure: one mouse report applied to a pose. mode "move" | "rotate".
function applyMouse(pose, mode, { dx = 0, dy = 0, wheel = 0, right = false, shift = false }, ranges = DEFAULT_RANGES) {
  const f = shift ? 1 / FINE : 1, next = { ...pose };
  const add = (key, delta) => { next[key] = round(key, clamp(next[key] + delta, ranges[key])); };
  if (mode === "move") {
    if (right) add("z", dy * STEP.depth * f);              // right-drag: push the mouse away = farther
    else { add("x", dx * STEP.move * f); add("y", -dy * STEP.move * f); }
    if (wheel) add("z", -wheel * STEP.notch * f);          // wheel up = farther
  } else {
    if (right) add("roll", dx * STEP.turn * f);
    else { add("yaw", dx * STEP.turn * f); add("pitch", -dy * STEP.turn * f); }
    if (wheel) next.widthMeters = round("widthMeters", clamp(next.widthMeters * Math.pow(shift ? 1 + (STEP.scale - 1) / FINE : STEP.scale, wheel), ranges.widthMeters));
  }
  return next;
}

// targets(): null, or { game, carId, items: [{ id, name, pose }] } for the overlays showing in the headset now.
// preview(game, poses /* Map id -> pose */, selectedId, phase): show poses live; phase "edit" | "confirm" | null (end).
// save(game, carId, edits /* [{ id, pose }] */): store the poses in the profile.
function createEditMode({ executable, targets, preview, save, beep = () => {}, onStatus = () => {}, spawn = defaultSpawn,
  ranges = DEFAULT_RANGES, setInterval: every = setInterval, clearInterval: stopEvery = clearInterval, setTimeout: later = setTimeout } = {}) {
  let child = null, buffer = "", closed = false, armed = false, retry = null, attempt = 0, ready = false, error = "";
  let session = null;   // { game, carId, order: [ids], names, original: Map, poses: Map, index, mode, phase, saving }
  const send = (o) => { try { child?.stdin?.write(JSON.stringify(o) + "\n"); } catch { } };
  const report = () => onStatus({ running: !!child, ready, armed, error,
    editing: session ? { game: session.game, selected: session.order[session.index], mode: session.mode, phase: session.phase } : null });

  function start() {
    if (closed || child) return;
    let spawned;
    try { spawned = spawn(executable, [], { windowsHide: true, stdio: ["pipe", "pipe", "pipe"] }); }
    catch (e) { error = e.message; schedule(); report(); return; }
    child = spawned; buffer = ""; ready = false;
    spawned.stdout?.setEncoding?.("utf8");
    spawned.stdout?.on("data", (chunk) => {
      buffer += chunk;
      if (buffer.length > 65536) buffer = buffer.slice(-4096);
      let n;
      while ((n = buffer.indexOf("\n")) >= 0) {
        const line = buffer.slice(0, n).trim(); buffer = buffer.slice(n + 1);
        if (!line) continue;
        let event; try { event = JSON.parse(line); } catch { continue; }
        handle(event);
      }
    });
    const gone = () => {
      if (child !== spawned) return;
      child = null; ready = false;
      if (session) end(false);
      schedule(); report();
    };
    spawned.on?.("exit", gone); spawned.on?.("error", (e) => { error = e.message; gone(); });
    report();
  }
  function schedule() {
    if (closed || retry) return;
    const delay = Math.min(1000 * 2 ** Math.min(attempt++, 5), 30000);
    retry = later(() => { retry = null; start(); }, delay);
    retry?.unref?.();
  }
  function handle(event) {
    if (event.op === "ready") { ready = true; attempt = 0; error = ""; send({ op: "arm", on: armed }); report(); return; }
    if (event.op === "error") { error = String(event.message || "Edit input failed.").slice(0, 200); report(); return; }
    if (event.op === "key") return key(event.key);
    if (event.op === "mouse" && session?.phase === "edit") {
      const id = session.order[session.index];
      session.poses.set(id, applyMouse(session.poses.get(id), session.mode, event, ranges));
      show();
    }
  }
  function show() { if (session) preview(session.game, session.poses, session.order[session.index], session.phase); }
  function begin() {
    const t = targets();
    if (!t || !t.items.length) { beep("unavailable"); return; }
    const original = new Map(t.items.map(item => [item.id, Object.fromEntries(KEYS.map(k => [k, item.pose[k]]))]));
    session = { game: t.game, carId: t.carId || null, order: t.items.map(i => i.id), names: new Map(t.items.map(i => [i.id, i.name])),
      original, poses: new Map([...original].map(([id, p]) => [id, { ...p }])), index: 0, mode: "move", phase: "edit", saving: false };
    send({ op: "edit", on: true });
    beep("start"); show(); report();
  }
  function end(keep) {
    const s = session; if (!s) return;
    session = null;
    send({ op: "edit", on: false });
    preview(s.game, new Map(), null, null);   // after a save the profile already holds the new poses
    report();
  }
  async function finish() {
    const s = session; if (!s || s.saving) return;
    const edits = s.order.filter(id => KEYS.some(k => s.poses.get(id)[k] !== s.original.get(id)[k])).map(id => ({ id, pose: s.poses.get(id) }));
    if (!edits.length) { beep("saved"); end(false); return; }
    s.saving = true;
    try { await save(s.game, s.carId, edits); beep("saved"); end(true); }
    catch (e) { error = String(e?.message || e).slice(0, 200); beep("error"); s.saving = false; s.phase = "edit"; show(); report(); }
  }
  function key(name) {
    if (!session) { if (name === "tab" && armed) begin(); return; }
    if (session.saving) return;
    if (session.phase === "confirm") {
      if (name === "enter") return void finish();
      if (name === "escape") { beep("cancelled"); end(false); return; }
      session.phase = "edit"; show(); report(); return;   // anything else: back to editing
    }
    if (name === "space") { session.index = (session.index + 1) % session.order.length; beep("next"); show(); report(); }
    else if (name === "m") { session.mode = session.mode === "move" ? "rotate" : "move"; beep(session.mode); show(); report(); }
    else if (name === "escape" || name === "tab") { session.phase = "confirm"; beep("confirm"); show(); report(); }
  }
  // Re-check every second whether overlays are showing; Tab is only taken from the game while they are.
  const timer = every(() => {
    const t = targets(), want = !!(t && t.items.length);
    if (session && (!t || t.game !== session.game)) end(false);   // the game or overlays went away
    if (want && !child) start();
    if (want !== armed) { armed = want; send({ op: "arm", on: armed }); report(); }
  }, 1000);
  timer?.unref?.();
  return {
    status: () => ({ running: !!child, ready, armed, error, editing: session ? { selected: session.order[session.index], mode: session.mode, phase: session.phase } : null }),
    key, mouse: (e) => handle({ op: "mouse", ...e }),   // for tests
    close() {
      if (closed) return; closed = true;
      if (session) end(false);
      stopEvery(timer); if (retry) clearTimeout(retry);
      if (child) { send({ op: "quit" }); try { child.stdin?.end?.(); } catch { } const c = child; later(() => { try { c.kill(); } catch { } }, 250); child = null; }
    },
  };
}
module.exports = { createEditMode, applyMouse, DEFAULT_RANGES, STEP, FINE };
