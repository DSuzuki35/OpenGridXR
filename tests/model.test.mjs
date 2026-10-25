import { test } from "node:test";
import assert from "node:assert/strict";
import {
  createDocument,
  createProfile,
  validateDocument,
  validateProfile,
  polygonArea,
  isSimplePolygon,
  pixelBudget,
  contourPreset,
  effectiveMask,
  parseProfile,
} from "../src/model.mjs";

test("neutral defaults keep full render area and separate the two games", () => {
  const doc = createDocument();
  validateDocument(doc);
  assert.deepEqual(
    doc.profiles.map((p) => p.game),
    ["iracing", "lmu"],
  );
  assert.equal(pixelBudget(doc.profiles[0]).renderedPercent, 100);
  doc.profiles[0].mask.left[0].x = 0.4;
  assert.notEqual(doc.profiles[1].mask.left[0].x, 0.4);
});
test("pixel estimate uses area and square of render scale, never an FPS claim", () => {
  const p = createProfile("iracing");
  p.view.crop = { left: 10, right: 10, top: 10, bottom: 10 };
  p.view.renderScale = 150;
  assert.equal(pixelBudget(p).retainedPercent, 64);
  assert.equal(pixelBudget(p).renderedPercent, 144);
});
test("self crossing and collinear contours cannot be imported", () => {
  const p = createProfile("lmu");
  p.mask.left = [
    { x: 0, y: 0 },
    { x: 1, y: 1 },
    { x: 0, y: 1 },
    { x: 1, y: 0 },
  ];
  assert.equal(isSimplePolygon(p.mask.left), false);
  assert.throws(() => validateProfile(p), /contour/i);
  p.mask.left = [
    { x: 0.1, y: 0.1 },
    { x: 0.5, y: 0.5 },
    { x: 0.9, y: 0.9 },
  ];
  assert.throws(() => validateProfile(p), /contour/i);
});
test("stream mode preserves saved contour while disabling its render mask", () => {
  const p = createProfile("iracing");
  p.mask.enabled = true;
  assert.equal(effectiveMask(p), true);
  const saved = JSON.stringify(p.mask);
  p.mode = "streaming";
  assert.equal(effectiveMask(p), false);
  assert.equal(JSON.stringify(p.mask), saved);
});
test("stream render budget preserves source crop and counts extra scenery", () => {
  const p = createProfile("iracing");
  p.mode = "streaming";
  p.view.crop = { left: 10, right: 10, top: 10, bottom: 10 };
  p.stream.extraLeft = 20;
  p.stream.extraRight = 10;
  assert.equal(pixelBudget(p).renderedPercent, 130);
  assert.equal(pixelBudget(p).retainedPercent, 64);
});
test("invalid numbers, cropped-away view and unsupported games are rejected", () => {
  for (const mutate of [
    (p) => (p.view.renderScale = NaN),
    (p) => (p.view.crop.left = Infinity),
    (p) => (p.view.crop = { left: 40, right: 40, top: 0, bottom: 0 }),
    (p) => (p.game = "anything"),
    (p) => (p.stream.width = 90000),
    (p) => (p.mask.padding = -1),
    (p) => (p.stream.eye = 2),
    (p) => (p.helmet.opacity = "100"),
  ]) {
    const p = createProfile("iracing");
    mutate(p);
    assert.throws(() => validateProfile(p));
  }
});
test("duplicate profile ids and invalid selected profile are rejected", () => {
  const doc = createDocument();
  doc.profiles[1].id = doc.profiles[0].id;
  assert.throws(() => validateDocument(doc), /duplicate/i);
  const second = createDocument();
  second.selectedProfileId = "missing";
  assert.throws(() => validateDocument(second), /selected/i);
});
test("preset contours are valid independent polygons with positive area", () => {
  for (const name of ["formula", "gt", "open"]) {
    const points = contourPreset(name);
    assert.ok(isSimplePolygon(points));
    assert.ok(polygonArea(points) > 0.04);
    const p = createProfile("iracing");
    p.mask.left = points;
    validateProfile(p);
  }
});
test("prototype pollution keys and excessive point counts fail at import boundary", () => {
  const doc = createDocument();
  doc.profiles[0].extra = JSON.parse('{"__proto__":{"admin":true}}');
  assert.throws(() => validateDocument(doc), /unsafe/i);
  const p = createProfile("iracing");
  p.mask.left = Array(65).fill({ x: 0.1, y: 0.1 });
  assert.throws(() => validateProfile(p), /contour/i);
});
test("profile exchange refuses unknown envelope versions", () => {
  assert.throws(
    () =>
      parseProfile(
        JSON.stringify({ version: 999, profile: createProfile("iracing") }),
      ),
    /version/i,
  );
  assert.equal(
    parseProfile(JSON.stringify({ version: 1, profile: createProfile("lmu") }))
      .game,
    "lmu",
  );
});

test("symmetric extra area is opt-in and old profile exchange stays unchanged", () => {
  const profile = createProfile("iracing");
  assert.equal(profile.stream.extraSymmetric, false);
  delete profile.stream.extraSymmetric;
  profile.stream.extraLeft = 23;
  profile.stream.extraRight = 4;
  const original = JSON.stringify(profile);
  validateProfile(profile);
  assert.equal(
    JSON.stringify(profile),
    original,
    "Validation must not migrate existing data",
  );
  const imported = parseProfile(JSON.stringify({ version: 1, profile }));
  assert.equal(
    JSON.stringify(imported),
    original,
    "Import must retain asymmetric legacy data",
  );
});

test("symmetric imports require a boolean flag and matching validated sides", () => {
  const profile = createProfile("lmu");
  for (const flag of ["true", 1, null]) {
    profile.stream.extraSymmetric = flag;
    assert.throws(() => parseProfile(JSON.stringify(profile)), /symmetric/i);
  }
  profile.stream.extraSymmetric = true;
  profile.stream.extraLeft = 17;
  profile.stream.extraRight = 8;
  const original = JSON.stringify(profile);
  assert.throws(
    () => parseProfile(original),
    /symmetric.*equal|equal.*symmetric/i,
  );
  assert.equal(
    JSON.stringify(profile),
    original,
    "Inconsistent imports must not be silently repaired",
  );
  profile.stream.extraRight = 17;
  assert.deepEqual(
    parseProfile(JSON.stringify({ version: 1, profile })),
    profile,
  );
});
