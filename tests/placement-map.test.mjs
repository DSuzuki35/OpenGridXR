import test from "node:test";
import assert from "node:assert/strict";
import { mapPosition, mapPatch, nudgePosition, nudgeDashboard } from "../src/placement-map.mjs";
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

test("dashboard keyboard changes the chosen axis, with millimeter and tenth-degree precision", () => {
  const pose = { x: 0.122, y: 1, z: -1, widthMeters: 0.4, yaw: 17.3, pitch: -2, roll: 0 };
  for (const [key, modifiers, expected] of [
    ["ArrowRight", {shiftKey:true}, {x:0.123}],
    ["ArrowDown", {}, {y:0.975}],
    ["ArrowUp", {ctrlKey:true}, {z:-1.025}],
    ["ArrowDown", {altKey:true, shiftKey:true}, {widthMeters:0.399}],
    ["ArrowRight", {ctrlKey:true}, {yaw:18.3}],
    ["ArrowLeft", {ctrlKey:true, shiftKey:true}, {yaw:17.2}],
    ["ArrowLeft", {altKey:true}, {roll:-1}],
    ["ArrowRight", {altKey:true, shiftKey:true}, {roll:0.1}],
    ["ArrowUp", {ctrlKey:true, altKey:true}, {pitch:-1}],
    ["ArrowDown", {ctrlKey:true, altKey:true, shiftKey:true}, {pitch:-2.1}],
    ["ArrowLeft", {ctrlKey:true, altKey:true}, null],
    ["ArrowUp", {metaKey:true}, null],
  ]) assert.deepEqual(nudgeDashboard(pose, {key, ...modifiers}), expected);
  assert.deepEqual(nudgeDashboard({...pose, pitch:179.7}, {key:"ArrowUp", ctrlKey:true, altKey:true}), {pitch:180});
});
