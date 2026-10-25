import test from "node:test";
import assert from "node:assert/strict";
import { EventEmitter } from "node:events";
import { PassThrough } from "node:stream";
import { createRequire } from "node:module";
const { createTelemetry, validateRecord } = createRequire(import.meta.url)("../desktop/telemetry.cjs");
const packet = () => ({ schemaVersion: 1, game: "iracing", source: "iracing-sdk", status: "live", connected: true, stale: false, sequence: 1, timestampMs: 1000, ageMs: 0, data: { speedMps: 60, gear: 3, rpm: 8000, throttle: 0.5, brake: 0, clutch: 0, fuelLiters: 30, lap: 2, lapsCompleted: 1, lapTimeSeconds: 20, lastLapSeconds: 90, bestLapSeconds: 90, flags: { yellow: false, blue: false, green: true, white: false, checkered: false, red: false, black: false }, inPits: false }, reason: null });
function fixture() {
  let clock = 1000; const children = [];
  const controller = createTelemetry({ executable: "fixture.exe", checkFile: async () => {}, now: () => clock,
    spawnProcess: (_, args, opts) => {
      assert.equal(opts.windowsHide, true);
      const child = new EventEmitter(); child.stdout = new PassThrough(); child.stderr = new PassThrough(); child.stdin = new PassThrough(); child.args = args; child.killed = false; child.kill = () => { child.killed = true; }; children.push(child); return child;
    } });
  return { controller, children, advance: (ms) => { clock += ms; } };
}
test("telemetry is demand-driven, rate bounded, stale after silence and stopped without consumers", async () => {
  const f = fixture(); const on = { enabled: true, updateHz: 20 };
  try {
    assert.equal((await f.controller.status("iracing")).running, false);
    assert.equal(f.children.length, 0);
    await f.controller.status("iracing", on);
    assert.deepEqual(f.children[0].args, ["--game", "iracing", "--rate", "20"]);
    f.children[0].stdout.write(JSON.stringify(packet()) + "\n");
    assert.equal((await f.controller.status("iracing", on)).record.data.speedMps, 60);
    f.advance(1600);
    assert.equal((await f.controller.status("iracing", on)).record.status, "stale");
    f.advance(2100); f.controller.reap();
    assert.equal(f.children[0].killed, true);
    await assert.rejects(f.controller.status("iracing", { enabled: true, updateHz: 31 }));
    await assert.rejects(f.controller.status("other", on));
  } finally { f.controller.close(); }
});
test("malformed and oversized telemetry stops the helper, with bounded restart backoff", async () => {
  const f = fixture(), on = { enabled: true, updateHz: 20 };
  try {
    await f.controller.status("iracing", on);
    f.children[0].stdout.write("x".repeat(16385));
    assert.equal(f.children[0].killed, true);
    await f.controller.status("iracing", on); assert.equal(f.children.length, 1);
    f.advance(5100); await f.controller.status("iracing", on);
    const wrong = packet(); wrong.data.rpm = "9000";
    f.children[1].stdout.write(JSON.stringify(wrong) + "\n");
    assert.equal(f.children[1].killed, true);
    const impossible = packet(); impossible.stale = true;
    assert.throws(() => validateRecord(impossible, "iracing"));
  } finally { f.controller.close(); }
});
test("telemetry controls rate replacement and rejects fixture data in normal operation", async () => {
  const f = fixture();
  try {
    await f.controller.status("iracing", { enabled: true, updateHz: 20 });
    await f.controller.status("iracing", { enabled: true, updateHz: 10 });
    assert.equal(f.children[0].killed, true); assert.equal(f.children.length, 2);
    const record = packet(); record.testData = true;
    f.children[1].stdout.write(JSON.stringify(record) + "\n");
    assert.equal(f.children[1].killed, true);
    assert.equal((await f.controller.status("iracing")).record, null);
  } finally { f.controller.close(); }
});
