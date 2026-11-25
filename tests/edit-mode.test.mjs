import test from "node:test";
import assert from "node:assert/strict";
import { EventEmitter } from "node:events";
import { createRequire } from "node:module";
const { createEditMode, applyMouse } = createRequire(import.meta.url)("../desktop/edit-mode.cjs");

const pose = (x = 0) => ({ x, y: -0.35, z: -1, yaw: 0, pitch: 0, roll: 0, widthMeters: 0.6 });

function harness({ items = [{ id: "a", name: "Dash", pose: pose(0) }, { id: "b", name: "Relative", pose: pose(0.5) }], save } = {}) {
  const processes = [], previews = [], beeps = [], saves = [];
  let tick = null, showing = items;
  const spawn = (exe, args, opts) => {
    const child = new EventEmitter();
    child.stdout = new EventEmitter(); child.stdout.setEncoding = () => {};
    child.stdin = { lines: [], write(l) { this.lines.push(JSON.parse(l)); }, end() {} };
    child.kill = () => { child.killed = true; };
    child.record = (r) => child.stdout.emit("data", JSON.stringify(r) + "\n");
    Object.assign(child, { exe, args, opts }); processes.push(child); return child;
  };
  const mode = createEditMode({
    executable: "edit.exe", spawn,
    targets: () => (showing.length ? { game: "iracing", carId: null, items: showing } : null),
    preview: (game, poses, selected, phase) => previews.push({ game, poses: new Map(poses), selected, phase }),
    save: save || (async (game, carId, edits) => { saves.push({ game, carId, edits }); }),
    beep: (k) => beeps.push(k),
    setInterval: (fn) => { tick = fn; return { unref() {} }; }, clearInterval: () => {}, setTimeout: (fn) => ({ unref() {} }),
  });
  return { mode, processes, previews, beeps, saves, tick: () => tick(), hide: () => { showing = []; } };
}

test("the helper starts and Tab is armed only while overlays are showing", () => {
  const h = harness();
  assert.equal(h.processes.length, 0);
  h.tick();
  const p = h.processes[0];
  assert.equal(p.exe, "edit.exe"); assert.equal(p.opts.windowsHide, true);
  p.record({ op: "ready" });
  assert.deepEqual(p.stdin.lines.at(-1), { op: "arm", on: true });
  h.hide(); h.tick();
  assert.deepEqual(p.stdin.lines.at(-1), { op: "arm", on: false });
});

test("Tab, mouse, Space, M, then Esc and Enter save only what moved", async () => {
  const h = harness(); h.tick(); const p = h.processes[0]; p.record({ op: "ready" });
  p.record({ op: "key", key: "tab" });
  assert.deepEqual(p.stdin.lines.at(-1), { op: "edit", on: true });
  assert.equal(h.previews.at(-1).selected, "a"); assert.equal(h.previews.at(-1).phase, "edit");
  p.record({ op: "mouse", dx: 200, dy: -100, wheel: 0, right: false, shift: false });
  let a = h.previews.at(-1).poses.get("a");
  assert.equal(a.x, 0.1); assert.equal(a.y, -0.3);                  // right and up
  p.record({ op: "mouse", dx: 0, dy: 0, wheel: 2, right: false, shift: false });
  assert.equal(h.previews.at(-1).poses.get("a").z, -1.04);          // wheel up: farther
  p.record({ op: "key", key: "space" });
  assert.equal(h.previews.at(-1).selected, "b");
  p.record({ op: "key", key: "space" });
  assert.equal(h.previews.at(-1).selected, "a");                    // wraps around
  p.record({ op: "key", key: "m" });
  p.record({ op: "mouse", dx: 100, dy: -50, wheel: 0, right: false, shift: false });
  a = h.previews.at(-1).poses.get("a");
  assert.equal(a.yaw, 10); assert.equal(a.pitch, 5);
  p.record({ op: "key", key: "escape" });
  assert.equal(h.previews.at(-1).phase, "confirm");
  p.record({ op: "mouse", dx: 500, dy: 0, wheel: 0, right: false, shift: false });   // ignored while asking
  assert.equal(h.previews.at(-1).poses.get("a").x, 0.1);
  p.record({ op: "key", key: "enter" });
  await new Promise((r) => setImmediate(r));
  assert.equal(h.saves.length, 1);
  assert.deepEqual(h.saves[0].edits.map((e) => e.id), ["a"]);
  assert.equal(h.saves[0].edits[0].pose.yaw, 10);
  assert.deepEqual(p.stdin.lines.at(-1), { op: "edit", on: false });
  assert.equal(h.previews.at(-1).phase, null);
  assert.ok(h.beeps.includes("saved"));
});

test("Esc twice cancels without saving and puts the overlays back", () => {
  const h = harness(); h.tick(); const p = h.processes[0]; p.record({ op: "ready" });
  p.record({ op: "key", key: "tab" });
  p.record({ op: "mouse", dx: 300, dy: 0, wheel: 0, right: false, shift: false });
  p.record({ op: "key", key: "escape" }); p.record({ op: "key", key: "escape" });
  assert.equal(h.saves.length, 0);
  assert.equal(h.previews.at(-1).phase, null); assert.equal(h.previews.at(-1).poses.size, 0);
  assert.ok(h.beeps.includes("cancelled"));
});

test("any other key while asking goes back to editing; a failed save keeps editing", async () => {
  const h = harness({ save: async () => { throw new Error("Save or discard profile edits in OpenGrid first."); } });
  h.tick(); const p = h.processes[0]; p.record({ op: "ready" });
  p.record({ op: "key", key: "tab" }); p.record({ op: "key", key: "escape" }); p.record({ op: "key", key: "space" });
  assert.equal(h.previews.at(-1).phase, "edit");
  p.record({ op: "mouse", dx: 10, dy: 0 });
  p.record({ op: "key", key: "tab" }); p.record({ op: "key", key: "enter" });
  await new Promise((r) => setImmediate(r));
  assert.equal(h.mode.status().editing.phase, "edit");
  assert.match(h.mode.status().error, /Save or discard/);
});

test("overlays disappearing ends the edit without saving", () => {
  const h = harness(); h.tick(); const p = h.processes[0]; p.record({ op: "ready" });
  p.record({ op: "key", key: "tab" }); h.hide(); h.tick();
  assert.equal(h.mode.status().editing, null);
  assert.deepEqual(p.stdin.lines.filter((l) => l.op === "edit").at(-1), { op: "edit", on: false });
});

test("mouse steps: shift is fine, right-drag moves in depth and leans, wheel resizes when rotating, limits hold", () => {
  const base = pose(0);
  assert.equal(applyMouse(base, "move", { dx: 100, shift: true }).x, 0.01);
  assert.equal(applyMouse(base, "move", { dy: -100, right: true }).z, -1.1);           // push the mouse away: farther
  assert.equal(applyMouse(base, "rotate", { dx: 50, right: true }).roll, 5);
  assert.equal(applyMouse(base, "rotate", { wheel: 1 }).widthMeters, 0.618);
  assert.equal(applyMouse(base, "move", { dx: 1e6 }).x, 5);
  assert.equal(applyMouse({ ...base, z: -0.12 }, "move", { wheel: -5 }).z, -0.1);
});
