import test from "node:test";
import assert from "node:assert/strict";
import { EventEmitter } from "node:events";
import { createRequire } from "node:module";
import { spawn as spawnNative } from "node:child_process";
import { existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
const { createInputHost } = createRequire(import.meta.url)("../desktop/input-host.cjs");
const id = "dinput:{01234567-89ab-cdef-0123-456789abcdef}";
const list = { op: "devices", devices: [{ id, name: "Wheel", buttons: 128, povs: 4 }] };
function harness(options = {}) {
  const processes = [], presses = [], statuses = [];
  const spawn = (exe, args, opts) => {
    const child = new EventEmitter();
    child.stdout = new EventEmitter(); child.stdout.setEncoding = () => {};
    child.stderr = new EventEmitter(); child.stdin = new EventEmitter();
    child.stdin.end = (value) => { child.sent = value; }; child.kill = () => { child.killed = true; };
    child.record = (record) => child.stdout.emit("data", JSON.stringify(record) + "\n");
    Object.assign(child, { exe, args, opts }); processes.push(child); return child;
  };
  const host = createInputHost({ executable: "input.exe", spawn, onPress: (p) => presses.push(p), onStatus: (s) => statuses.push(s), ...options });
  return { host, processes, presses, statuses };
}
test("input host starts only on demand and validates 128-button/four-POV events", () => {
  const h = harness(); assert.equal(h.processes.length, 0); h.host.setActive(true);
  const p = h.processes[0]; assert.equal(p.opts.windowsHide, true); assert.deepEqual(p.args, []);
  const encoded = JSON.stringify(list) + "\n"; p.stdout.emit("data", encoded.slice(0, 19)); p.stdout.emit("data", encoded.slice(19));
  p.record({ op: "press", deviceId: id, kind: "button", index: 127 });
  p.record({ op: "press", deviceId: id, kind: "pov", index: 3, direction: 7 });
  assert.deepEqual(h.presses, [{ deviceId: id, kind: "button", index: 127 }, { deviceId: id, kind: "pov", index: 3, direction: 7 }]);
  const copy = h.host.state(); copy.devices[0].name = "mutated"; assert.equal(h.host.state().devices[0].name, "Wheel");
  h.host.close(); assert.equal(p.sent, '{"op":"quit"}\n'); p.emit("close");
});
test("test mode cannot enumerate hardware unless an explicit fixture is requested", () => {
  const h = harness({ testMode: true }); h.host.setActive(true); assert.equal(h.processes.length, 0); assert.match(h.host.state().error, /test mode/); h.host.close();
  const f = harness({ testMode: true, fixture: true }); f.host.setActive(true); assert.deepEqual(f.processes[0].args, ["--fixture"]); f.host.close(); f.processes[0].emit("close");
});
test("deactivation immediately ignores press events and resets devices", () => {
  const h = harness(); h.host.setActive(true); const p = h.processes[0]; p.record(list); h.host.setActive(false);
  p.record({ op: "press", deviceId: id, kind: "button", index: 0 }); assert.equal(h.presses.length, 0); assert.deepEqual(h.host.state().devices, []);
  p.emit("close"); assert.equal(h.host.state().running, false); h.host.close();
});
test("rapid reactivation waits for a fresh child and ignores the retiring child's events", () => {
  const h = harness(); h.host.setActive(true); const first = h.processes[0]; first.record(list);
  h.host.setActive(false); h.host.setActive(true);
  first.record({ op: "press", deviceId: id, kind: "button", index: 0 }); assert.equal(h.presses.length, 0);
  first.emit("close"); assert.equal(h.processes.length, 2); assert.deepEqual(h.host.state().devices, []);
  h.processes[1].record(list); first.record({ op: "press", deviceId: id, kind: "button", index: 0 }); assert.equal(h.presses.length, 0);
  h.host.close(); h.processes[1].emit("close");
});
test("malformed, oversized and detached-device events fail closed", () => {
  for (const bad of ["x\n", "a".repeat(65537), JSON.stringify({ op: "devices", devices: [list.devices[0], list.devices[0]] }) + "\n", JSON.stringify({ op: "press", deviceId: id, kind: "button", index: 128 }) + "\n"]) {
    const h = harness(); h.host.setActive(true); const p = h.processes[0]; p.record(list); p.stdout.emit("data", bad);
    assert.equal(p.killed, true); assert.equal(h.host.state().available, false); assert.equal(h.presses.length, 0); h.host.close(); p.emit("close");
  }
  const h = harness(); h.host.setActive(true); const p = h.processes[0]; p.record(list); p.record({ op: "devices", devices: [] }); p.record({ op: "press", deviceId: id, kind: "button", index: 0 }); assert.equal(p.killed, true); h.host.close(); p.emit("close");
});
test("child failure retries with backoff only while requested and ignores stale process events", async (t) => {
  t.mock.timers.enable({ apis: ["setTimeout", "Date"], now: 1000 });
  const h = harness(); h.host.setActive(true); const first = h.processes[0]; first.emit("error", new Error("missing")); first.emit("close");
  assert.equal(h.host.state().retryAt, 2000); t.mock.timers.tick(999); assert.equal(h.processes.length, 1); t.mock.timers.tick(1); assert.equal(h.processes.length, 2);
  first.record(list); assert.equal(h.host.state().available, false);
  const second = h.processes[1]; second.emit("close"); assert.equal(h.host.state().retryAt, 4000);
  h.host.setActive(false); t.mock.timers.tick(30000); assert.equal(h.processes.length, 2); h.host.close();
});
const executable = fileURLToPath(new URL("../native/build/Release/OpenGridXR-Input.exe", import.meta.url));
test("real synthetic helper pipe emits controlled input and exits on EOF without hardware access", { skip: process.platform !== "win32" || !existsSync(executable), timeout: 5000 }, async () => {
  const child = spawnNative(executable, ["--fixture"], { windowsHide: true, stdio: ["pipe", "pipe", "pipe"] });
  let output = "";
  child.stdout.setEncoding("utf8"); child.stdout.on("data", (chunk) => { output += chunk; });
  child.stdin.write('{"op":"fixture-press"}\n'); child.stdin.end();
  const code = await new Promise((resolve, reject) => { child.once("error", reject); child.once("close", resolve); });
  assert.equal(code, 0);
  const records = output.trim().split("\n").map(JSON.parse);
  assert.equal(records[0].op, "devices"); assert.equal(records[1].op, "press"); assert.equal(records[1].index, 127);
});
