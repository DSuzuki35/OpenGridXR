import test from "node:test";
import assert from "node:assert/strict";
import { createProfile, validateProfile } from "../src/model.mjs";
import { createSpatialOverlay } from "../src/spatial-overlays.mjs";
import {
  bindingDefaults,
  validateBindings,
  listToggleActions,
  toggleProfileAction,
} from "../src/input-bindings.mjs";

const deviceId = "dinput:{01234567-89ab-cdef-0123-456789abcdef}";
const button = (actionId = "stream.mirror", index = 3) => ({
  actionId,
  deviceId,
  deviceName: "Isolated test wheel",
  kind: "button",
  index,
});

test("bindings are optional and reject malformed or duplicate physical controls", () => {
  const p = createProfile();
  delete p.inputBindings;
  assert.doesNotThrow(() => validateProfile(p));
  p.inputBindings = bindingDefaults();
  p.inputBindings.bindings.push(button());
  assert.doesNotThrow(() => validateProfile(p));
  for (const patch of [
    { deviceId: "device-0" },
    { kind: "axis" },
    { index: 128 },
    { index: -1 },
    { kind: "pov", index: 4, direction: 0 },
    { kind: "pov", index: 0, direction: 8 },
    { actionId: "__proto__.enabled" },
    { deviceName: "x\n" },
  ]) {
    assert.throws(() =>
      validateBindings(
        { enabled: true, bindings: [{ ...button(), ...patch }] },
        p,
      ),
    );
  }
  assert.throws(
    () =>
      validateBindings(
        { enabled: true, bindings: [button(), button("mask.enabled")] },
        p,
      ),
    /already|duplicate/i,
  );
  assert.doesNotThrow(() =>
    validateBindings(
      {
        enabled: true,
        bindings: [
          button(),
          { ...button("mask.enabled"), kind: "pov", index: 0, direction: 7 },
        ],
      },
      p,
    ),
  );
  assert.throws(() =>
    validateBindings(
      {
        enabled: true,
        bindings: Array.from({ length: 129 }, (_, index) =>
          button("stream.mirror", index % 128),
        ),
      },
      p,
    ),
  );
});

test("every profile toggle has a stable action and dynamic source actions require that source", () => {
  const p = createProfile();
  const source = createSpatialOverlay();
  p.spatialOverlays.items.push(source);
  const ids = new Set(listToggleActions(p).map((action) => action.id));
  for (const id of [
    "mask.enabled",
    "mask.linked",
    "helmet.enabled",
    "stream.mirror",
    "stream.fullscreen",
    "stream.extraSymmetric",
    "framegen.enabled",
    "overlay.enabled",
    "telemetry.enabled",
    "spatialOverlays.enabled",
    ...Object.keys(p.overlay)
      .filter((key) => key.startsWith("show"))
      .map((key) => `overlay.${key}`),
    `spatialOverlays.${source.id}.enabled`,
    `spatialOverlays.${source.id}.showInStream`,
  ])
    assert.ok(ids.has(id), id);
  assert.equal(listToggleActions(p).length, ids.size);
  const next = toggleProfileAction(
    p,
    `spatialOverlays.${source.id}.showInStream`,
  );
  assert.equal(next.spatialOverlays.items[0].showInStream, true);
  assert.equal(p.spatialOverlays.items[0].showInStream, false);
  assert.throws(() =>
    toggleProfileAction(p, `spatialOverlays.${crypto.randomUUID()}.enabled`),
  );
});

test("input actions preserve symmetry and create neutral optional groups without mutating input", () => {
  const p = createProfile();
  p.stream.extraLeft = 4;
  p.stream.extraRight = 17;
  const next = toggleProfileAction(p, "stream.extraSymmetric");
  assert.deepEqual(
    [next.stream.extraLeft, next.stream.extraRight, next.stream.extraSymmetric],
    [17, 17, true],
  );
  assert.deepEqual(
    [p.stream.extraLeft, p.stream.extraRight, p.stream.extraSymmetric],
    [4, 17, false],
  );
  const unlinked = toggleProfileAction(next, "stream.extraSymmetric");
  assert.deepEqual(
    [
      unlinked.stream.extraLeft,
      unlinked.stream.extraRight,
      unlinked.stream.extraSymmetric,
    ],
    [17, 17, false],
  );
  delete p.framegen;
  delete p.overlay;
  delete p.telemetry;
  assert.equal(
    toggleProfileAction(p, "framegen.enabled").framegen.enabled,
    true,
  );
  assert.equal(
    toggleProfileAction(p, "overlay.showClock").overlay.showClock,
    true,
  );
  assert.deepEqual(toggleProfileAction(p, "telemetry.enabled").telemetry, {
    enabled: true,
    updateHz: 20,
  });
  assert.throws(() =>
    toggleProfileAction(
      { ...p, stream: { ...p.stream, extraLeft: 99 } },
      "stream.mirror",
    ),
  );
});

test("linking eyes uses the same deterministic left contour for a background action", () => {
  const p = createProfile();
  p.mask.linked = false;
  p.mask.right[0].x += 0.01;
  const next = toggleProfileAction(p, "mask.linked");
  assert.equal(next.mask.linked, true);
  assert.deepEqual(next.mask.left, next.mask.right);
  assert.notEqual(next.mask.left, next.mask.right);
  assert.equal(p.mask.linked, false);
});
