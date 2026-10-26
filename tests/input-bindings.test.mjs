import test from "node:test";
import assert from "node:assert/strict";
import { createProfile, validateProfile } from "../src/model.mjs";
import { createSpatialOverlay } from "../src/spatial-overlays.mjs";
import { bindingDefaults, validateBindings, listToggleActions, toggleProfileAction } from "../src/input-bindings.mjs";

const deviceId = "dinput:{01234567-89ab-cdef-0123-456789abcdef}";
const button = (actionId = "overlay.enabled", index = 3) => ({ actionId, deviceId, deviceName: "Isolated test wheel", kind: "button", index });
const removed = ["mask.enabled", "mask.linked", "helmet.enabled", "stream.mirror", "stream.fullscreen", "stream.extraSymmetric", "telemetry.enabled", "overlay.showPerformance", "overlay.showGraph", "overlay.showRuntime", "overlay.showApp", "overlay.showClock", "overlay.showFramegen", "overlay.showToolkit"];

test("bindings are optional and reject malformed or duplicate physical controls", () => {
  const p = createProfile();
  delete p.inputBindings;
  assert.doesNotThrow(() => validateProfile(p));
  p.inputBindings = bindingDefaults();
  p.inputBindings.bindings.push(button());
  assert.doesNotThrow(() => validateProfile(p));
  for (const patch of [
    { deviceId: "device-0" }, { kind: "axis" }, { index: 128 }, { index: -1 },
    { kind: "pov", index: 4, direction: 0 }, { kind: "pov", index: 0, direction: 8 },
    { actionId: "__proto__.enabled" }, { deviceName: "x\n" },
  ]) assert.throws(() => validateBindings({ enabled: true, bindings: [{ ...button(), ...patch }] }, p));
  assert.throws(() => validateBindings({ enabled: true, bindings: [button(), button("framegen.enabled")] }, p), /already|duplicate/i);
  assert.doesNotThrow(() => validateBindings({ enabled: true, bindings: [button(), { ...button("framegen.enabled"), kind: "pov", index: 0, direction: 7 }] }, p));
  assert.throws(() => validateBindings({ enabled: true, bindings: Array.from({ length: 129 }, (_, index) => button("overlay.enabled", index % 128)) }, p));
});

test("controller menu offers only quick driving actions and existing overlay visibility", () => {
  const p = createProfile();
  const source = createSpatialOverlay();
  p.spatialOverlays.items.push(source);
  assert.deepEqual(new Set(listToggleActions(p).map(action => action.id)), new Set([
    "framegen.enabled", "overlay.enabled", "overlay.layout", "spatialOverlays.enabled", `spatialOverlays.${source.id}.enabled`,
  ]));
  assert.throws(() => toggleProfileAction(p, `spatialOverlays.${crypto.randomUUID()}.enabled`));
  assert.throws(() => validateBindings({ enabled: true, bindings: [button(`spatialOverlays.${crypto.randomUUID()}.enabled`)] }, p));
});

test("old setup bindings load compatibly but cannot execute or mutate a profile", () => {
  const p = createProfile();
  const source = createSpatialOverlay();
  p.spatialOverlays.items.push(source);
  const obsolete = [...removed, `spatialOverlays.${source.id}.showInStream`];
  p.inputBindings.bindings = obsolete.map((action, index) => button(action, index));
  const before = structuredClone(p);
  assert.doesNotThrow(() => validateProfile(p));
  for (const action of obsolete) assert.throws(() => toggleProfileAction(p, action), /unavailable/i, action);
  assert.deepEqual(p, before);
  assert.equal(toggleProfileAction(p, "overlay.enabled").overlay.enabled, true);
});

test("driving toggles preserve other settings and initialize optional defaults", () => {
  const p = createProfile();
  const source = createSpatialOverlay();
  p.spatialOverlays.items.push(source);
  const before = structuredClone(p);
  assert.equal(toggleProfileAction(p, "framegen.enabled").framegen.enabled, true);
  assert.equal(toggleProfileAction(p, "overlay.enabled").overlay.enabled, true);
  assert.equal(toggleProfileAction(p, "spatialOverlays.enabled").spatialOverlays.enabled, true);
  const shown = toggleProfileAction(p, `spatialOverlays.${source.id}.enabled`);
  assert.equal(shown.spatialOverlays.items[0].enabled, !source.enabled);
  assert.equal(shown.spatialOverlays.items[0].showInStream, source.showInStream);
  assert.deepEqual(p, before);
  delete p.framegen;
  delete p.overlay;
  assert.equal(toggleProfileAction(p, "framegen.enabled").framegen.enabled, true);
  assert.equal(toggleProfileAction(p, "overlay.enabled").overlay.enabled, true);
  assert.throws(() => toggleProfileAction({ ...p, stream: { ...p.stream, extraLeft: 99 } }, "overlay.enabled"));
});

test("overlay mode binding cycles expanded and compact while preserving chosen content", () => {
  const p = createProfile();
  p.overlay.showClock = true;
  p.overlay.showGraph = false;
  const compact = toggleProfileAction(p, "overlay.layout");
  assert.deepEqual(compact.overlay, { ...p.overlay, layout: "compact" });
  const expanded = toggleProfileAction(compact, "overlay.layout");
  assert.deepEqual(expanded.overlay, p.overlay);
  delete p.overlay;
  assert.equal(toggleProfileAction(p, "overlay.layout").overlay.layout, "compact");
});
