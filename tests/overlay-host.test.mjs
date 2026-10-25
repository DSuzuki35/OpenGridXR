import test from "node:test";
import assert from "node:assert/strict";
import { EventEmitter } from "node:events";
import { PassThrough } from "node:stream";
import { createRequire } from "node:module";
const { createOverlayHost } = createRequire(import.meta.url)("../desktop/overlay-host.cjs");
function fixture() {
  const child = new EventEmitter(); child.stdout = new PassThrough(); child.stderr = new PassThrough(); child.stdin = new PassThrough(); child.killed = false; child.kill = () => { child.killed = true; }; const commands = [];
  child.stdin.on("data", (chunk) => commands.push(JSON.parse(String(chunk))));
  const refreshes = [];
  const host = createOverlayHost({ executable: "fixture.exe", game: "iracing", onSlotReady: (slot) => refreshes.push(slot), spawnProcess: (_, args, opts) => { assert.deepEqual(args, ["--game", "iracing", "--parent-pid", String(process.pid)]); assert.equal(opts.windowsHide, true); return child; } });
  host.start();
  const status = (available) => child.stdout.write(JSON.stringify({ op: "status", available, capacity: 15, activeOverlayCount: 0, error: "" }) + "\n");
  const texture = () => { const handle = Buffer.alloc(8); handle.writeBigUInt64LE(42n); return { releases: 0, textureInfo: { handle: { ntHandle: handle }, pixelFormat: "bgra", codedSize: { width: 1024, height: 576 }, widgetType: "frame" }, release() { this.releases++; } }; };
  return { host, child, commands, status, texture, refreshes, close: () => { host.close(); child.emit("close"); } };
}
test("GPU source is retained until exact acknowledgment and excess frames are dropped immediately", () => {
  const f = fixture();
  try {
    const unavailable = f.texture(); assert.equal(f.host.submit(0, unavailable), false); assert.equal(unavailable.releases, 1);
    // The first real source paint will redraw after receiver readiness as well.
    f.status(true); const first = f.texture(), dropped = f.texture();
    assert.deepEqual(f.refreshes, [0]); f.refreshes.length = 0;
    assert.equal(f.host.submit(0, first), true); assert.equal(first.releases, 0);
    assert.equal(f.host.submit(0, dropped), false); assert.equal(dropped.releases, 1);
    const command = f.commands.find((c) => c.op === "frame"); assert.equal(command.handle, "42");
    f.child.stdout.write(JSON.stringify({ op: "ack", slot: 0, frameId: command.frameId + 1 }) + "\n"); assert.equal(first.releases, 0);
    f.child.stdout.write(JSON.stringify({ op: "ack", slot: 0, frameId: command.frameId, status: "copied" }) + "\n"); assert.equal(first.releases, 1);
    assert.deepEqual(f.refreshes, [0], "the dropped final static paint is requested again after the in-flight frame completes");
    f.child.stdout.write(JSON.stringify({ op: "ack", slot: 0, frameId: command.frameId, status: "copied" }) + "\n"); assert.equal(first.releases, 1);
    assert.deepEqual(f.refreshes, [0], "duplicate acknowledgment cannot cause a repaint loop");
  } finally { f.close(); }
});
test("native transient drops request a static repaint; terminal errors do not retry", () => {
  const f = fixture();
  try {
    f.status(true); f.host.submit(2, f.texture());
    let frame = f.commands.at(-1);
    f.child.stdout.write(JSON.stringify({ op: "ack", slot: 2, frameId: frame.frameId, status: "dropped" }) + "\n");
    assert.deepEqual(f.refreshes, [2]);
    f.host.submit(2, f.texture()); frame = f.commands.at(-1);
    f.child.stdout.write(JSON.stringify({ op: "ack", slot: 2, frameId: frame.frameId, status: "error", reason: "adapter mismatch" }) + "\n");
    assert.deepEqual(f.refreshes, [2]); assert.equal(f.host.state().frameErrors[2], "adapter mismatch");
  } finally { f.close(); }
});
test("host shutdown releases pending textures only after process close, unsupported formats bypass", () => {
  const f = fixture();
  try {
    f.status(true);
    const unsupported = f.texture(); unsupported.textureInfo.pixelFormat = "rgbaf16";
    assert.equal(f.host.submit(0, unsupported), false); assert.equal(unsupported.releases, 1);
    const retained = f.texture(); f.host.submit(1, retained); f.host.close();
    assert.equal(retained.releases, 0); assert.equal(f.child.killed, true);
    f.child.emit("close"); assert.equal(retained.releases, 1);
  } finally { f.close(); }
});
test("malformed host messages terminate the producer without an unsafe early release", () => {
  const f = fixture();
  try {
    f.status(true); const retained = f.texture(); f.host.submit(0, retained);
    f.child.stdout.write("{bad}\n"); assert.equal(f.child.killed, true); assert.equal(retained.releases, 0);
    f.child.emit("close"); assert.equal(retained.releases, 1);
  } finally { f.close(); }
});
