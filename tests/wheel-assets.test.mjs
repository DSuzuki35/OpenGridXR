import test from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { createHash } from "node:crypto";
import * as spatial from "../src/spatial-overlays.mjs";
import { builtin } from "../desktop/wheel-import.cjs";

test("Artura selects the authored display and the independent SimHub device", () => {
  const wheel = spatial.wheelModels.find(model => model.id === "ascher-artura");
  assert.ok(wheel);
  assert.equal(wheel.width, .3);
  assert.ok(builtin.includes(wheel.id));
  assert.equal(wheel.screenHeight, 608);
  assert.equal(spatial.hasWheelScreen(wheel.id), true);
  assert.equal(spatial.hasWheelLights(wheel.id), true);
  assert.equal(spatial.wheelLightDevice(wheel.id, 1), 6);
  for (const [index, id] of ["hyper-p1", "hyper-sl", "gxl", "gt-max", "fpe2"].entries())
    assert.equal(spatial.wheelLightDevice(id), index + 1);
  assert.equal(spatial.wheelLightDevice("a".repeat(64), 3), 3);
  spatial.validateSpatialOverlays({enabled: true, pixelBudgetMP: 60,
    items: [{...spatial.createSpatialOverlay(), wheelModel: wheel.id, wheelDevice:6, wheelLights:true}]});
});

test("bundled Artura matches its manifest and the native mesh contract", async () => {
  const directory = new URL("../assets/wheels/", import.meta.url);
  const manifest = JSON.parse(await readFile(new URL("ascher-artura.conversion.json", directory)));
  const mapping = JSON.parse(await readFile(new URL("ascher-artura.mapping.json", directory)));
  const bytes = await readFile(new URL(manifest.file, directory));
  assert.equal(bytes.length, manifest.bytes);
  assert.equal(createHash("sha256").update(bytes).digest("hex"), manifest.sha256);
  assert.equal(bytes.toString("ascii", 0, 8), "AGMESH1\0");
  assert.equal(bytes.readUInt32LE(8), 5);
  const vertices = bytes.readUInt32LE(12), indices = bytes.readUInt32LE(16);
  const width = bytes.readUInt32LE(20), height = bytes.readUInt32LE(24);
  assert.equal(bytes.readUInt32LE(28), 0);
  assert.ok(vertices > 0 && vertices <= 1_000_000);
  assert.ok(indices > 0 && indices <= 3_000_000 && indices % 3 === 0);
  assert.equal(indices / 3, manifest.triangles);
  assert.equal(vertices, manifest.vertices);
  assert.deepEqual([width, height], manifest.atlas);
  assert.equal(bytes.length, 32 + vertices * 36 + indices * 4 + width * height * 4);
  const minimum = [Infinity, Infinity, Infinity], maximum = [-Infinity, -Infinity, -Infinity];
  const colors = new Set(), groups = new Map(), screenUv = [Infinity, Infinity, -Infinity, -Infinity];
  for (let i = 0; i < vertices; i++) {
    const offset = 32 + 36 * i;
    const value = Array.from({length: 9}, (_, k) => bytes.readFloatLE(offset + k * 4));
    assert.ok(value.every(Number.isFinite));
    for (let axis = 0; axis < 3; axis++) {
      assert.ok(Math.abs(value[axis]) <= 10);
      minimum[axis] = Math.min(minimum[axis], value[axis]);
      maximum[axis] = Math.max(maximum[axis], value[axis]);
    }
    assert.ok(Math.abs(Math.hypot(...value.slice(3, 6)) - 1) < 1e-5);
    assert.ok(value.slice(6, 8).every(n => n >= 0 && n <= 1));
    assert.ok(value[8] === 0 || value[8] === 200 || mapping.led_controls.some(control => value[8] === 201 + control.physical_index));
    groups.set(value[8], (groups.get(value[8]) || 0) + 1);
    if (value[8] === 200) {
      screenUv[0] = Math.min(screenUv[0], value[6]); screenUv[1] = Math.min(screenUv[1], value[7]);
      screenUv[2] = Math.max(screenUv[2], value[6]); screenUv[3] = Math.max(screenUv[3], value[7]);
    }
    const pixel = Math.min(height-1, Math.floor(value[7]*height))*width + Math.min(width-1, Math.floor(value[6]*width));
    if (value[8] !== 200) colors.add(bytes.readUInt32LE(32 + 36 * vertices + 4 * indices + pixel * 4));
  }
  assert.ok(Math.abs(maximum[0] - minimum[0] - 1) < 1e-6);
  assert.deepEqual([minimum, maximum], manifest.normalized_bounds);
  assert.equal(colors.size, 16, "all supplied diffuse colors plus red/blue controls survive");
  const overrides = JSON.parse(await readFile(new URL("ascher-artura.colors.json", directory)));
  for (const control of overrides.controls) {
    const {slot, rgb} = overrides.palette[control.color];
    for (let vertex = control.first_triangle * 3; vertex < (control.first_triangle + control.triangles) * 3; vertex++) {
      const offset = 32 + vertex * 36;
      assert.equal(bytes.readFloatLE(offset + 32), 0, "colored controls retain non-emissive group");
      assert.equal(bytes.readFloatLE(offset + 24), ((slot % 4) * 16 + 8) / width);
      assert.equal(bytes.readFloatLE(offset + 28), (Math.floor(slot / 4) * 16 + 8) / height);
    }
    const pixel = (Math.floor(slot / 4) * 16 + 8) * width + (slot % 4) * 16 + 8;
    assert.deepEqual([...bytes.subarray(32 + 40 * vertices + pixel * 4, 32 + 40 * vertices + pixel * 4 + 4)], [...rgb, 255]);
  }
  assert.equal(groups.get(200), 42, "the CAD glass supplies exactly fourteen fitted display triangles");
  assert.ok(screenUv.every((value, i) => Math.abs(value - (i < 2 ? 0 : 1)) < 1e-6));
  assert.equal(mapping.led_controls.length, 35, "21 shift diffusers and 14 button symbols");
  assert.equal(manifest.led_mapping.physical_pixels, 63, "retain gaps in the SimHub physical packet");
  for (const control of mapping.led_controls)
    assert.equal(groups.get(201 + control.physical_index), control.source_spans.reduce((n, span) => n + span.triangles * 3, 0));
  for (let i = 0; i < indices; i++) assert.ok(bytes.readUInt32LE(32 + vertices * 36 + i * 4) < vertices);
});
