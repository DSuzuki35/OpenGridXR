import test from "node:test";
import assert from "node:assert/strict";
import { mapPosition, mapPatch, nudgePosition } from "../src/placement-map.mjs";
import { createSpatialOverlay, spatialDefaults, validateSpatialOverlays } from "../src/spatial-overlays.mjs";

test("top view round trips depth and horizontal position without changing height", () => {
  const item = { x: 2.5, y: 1.2, z: -5.05 };
  assert.deepEqual(mapPosition(item, "top"), { x: 465, y: 145 });
  assert.deepEqual(mapPatch({ x: 465, y: 145 }, "top"), { x: 2.5, z: -5.05 });
});

test("front view puts positive height above the origin and preserves depth", () => {
  assert.deepEqual(mapPosition({ x: -2.5, y: 2.5, z: -2 }, "front"), { x: 195, y: 87.5 });
  assert.deepEqual(mapPatch({ x: 195, y: 87.5 }, "front"), { x: -2.5, y: 2.5 });
});

test("dragging outside either map clamps to validated overlay bounds", () => {
  for (const [view, point, expected] of [
    ["top", { x: -999, y: -999 }, { x: -5, z: -10 }],
    ["top", { x: 999, y: 999 }, { x: 5, z: -0.1 }],
    ["front", { x: -999, y: -999 }, { x: -5, y: 5 }],
    ["front", { x: 999, y: 999 }, { x: 5, y: -5 }],
  ]) {
    const patch = mapPatch(point, view);
    assert.deepEqual(patch, expected);
    assert.doesNotThrow(() => validateSpatialOverlays({ ...spatialDefaults(), items: [{ ...createSpatialOverlay(), ...patch }] }));
  }
});

test("arrow keys move the visible axes by one centimeter or ten with shift", () => {
  const item = { x: 0, y: 0.2, z: -1 };
  assert.deepEqual(nudgePosition(item, "top", "ArrowUp"), { z: -1.01 });
  assert.deepEqual(nudgePosition(item, "front", "ArrowUp", true), { y: 0.3 });
  assert.deepEqual(nudgePosition(item, "front", "ArrowLeft"), { x: -0.01 });
  assert.deepEqual(nudgePosition({ ...item, x: 5 }, "top", "ArrowRight"), { x: 5 });
  assert.equal(nudgePosition(item, "top", "Tab"), null);
});
