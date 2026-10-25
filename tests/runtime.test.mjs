import { test } from "node:test";
import assert from "node:assert/strict";
import {
  sessionState,
  validateManifest,
  outputArgs,
  assertSavedOutputProfile,
} from "../desktop/runtime.cjs";
import { createProfile } from "../src/model.mjs";
test("stale or dead session status cannot claim a connected headset", () => {
  const s = {
    pid: 42,
    updatedUnixMs: 50000,
    active: true,
    game: "iracing",
    frames: 20,
  };
  assert.equal(sessionState(s, 60001, () => true).active, false);
  assert.equal(sessionState(s, 50001, () => false).active, false);
  assert.equal(
    sessionState({ ...s, active: false }, 50001, () => true).active,
    false,
  );
  assert.equal(sessionState(s, 50001, () => true).active, true);
  assert.equal(
    sessionState({ ...s, updatedUnixMs: 999999 }, 50001, () => true).active,
    false,
  );
});
test("launch rejects saved profile whose unsaved selection would use another runtime profile", () => {
  const a = createProfile("iracing"),
    b = createProfile("iracing");
  a.mode = "streaming";
  b.mode = "streaming";
  const doc = {
    version: 1,
    selectedProfileId: a.id,
    profiles: [a, b],
    activeProfiles: { iracing: a.id },
  };
  assert.throws(
    () => assertSavedOutputProfile(doc, b),
    /save.*select|selected.*saved/i,
  );
  assert.doesNotThrow(() => assertSavedOutputProfile(doc, a));
});
test("capture-card output uses selected physical monitor and fullscreen without scaling canvas", () => {
  const p = createProfile("iracing");
  p.stream.displayId = "2";
  p.stream.fullscreen = true;
  const displays = [
    {
      id: "1",
      primary: true,
      bounds: { x: 0, y: 0, width: 2560, height: 1440 },
    },
    {
      id: "2",
      primary: false,
      bounds: { x: -3840, y: 0, width: 3840, height: 2160 },
    },
  ];
  assert.deepEqual(outputArgs(p, displays), [
    "--display",
    "-3840,0,3840,2160",
    "--fullscreen",
  ]);
  p.stream.displayId = "missing";
  assert.throws(() => outputArgs(p, displays), /display.*available/i);
});
test("installer accepts only the owned layer manifest and relative DLL filename", () => {
  const m = {
    api_layer: {
      name: "XR_APILAYER_OPENGRIDXR",
      library_path: ".\\OpenGridXR.dll",
    },
  };
  assert.doesNotThrow(() => validateManifest(m));
  assert.throws(() =>
    validateManifest({ api_layer: { ...m.api_layer, name: "FOREIGN_LAYER" } }),
  );
  assert.throws(() =>
    validateManifest({
      api_layer: { ...m.api_layer, library_path: "C:\\Windows\\foreign.dll" },
    }),
  );
});
