import test from "node:test";
import assert from "node:assert/strict";
import { createSpatialOverlay, createQuickMenuOverlay, spatialDefaults, validateSpatialOverlays, validateSpatialShortcuts, effectiveOverlayRates } from "../src/spatial-overlays.mjs";
import { createProfile, normalizeProfile, parseProfile } from "../src/model.mjs";
import * as spatial from "../src/spatial-overlays.mjs";

test("assigned steering axis persists with a 900 degree default and validates device settings", () => {
  assert.equal(spatial.steeringInputDefaults.lockDegrees, 900);
  const settings = spatialDefaults();
  settings.steeringInput = { ...spatial.steeringInputDefaults, source: "device", deviceId: "dinput:{00000000-0000-0000-0000-000000000001}", axis: "rx", invert: true };
  const profile = createProfile(); profile.spatialOverlays = settings;
  assert.deepEqual(parseProfile(JSON.stringify({version:1,profile})).spatialOverlays.steeringInput, settings.steeringInput);
  for (const patch of [{source:"auto"},{deviceId:"random"},{axis:"button"},{lockDegrees:89},{lockDegrees:2521},{lockDegrees:NaN},{lockDegrees:"900"},{invert:1}])
    assert.throws(() => validateSpatialOverlays({...settings,steeringInput:{...settings.steeringInput,...patch}}));
  assert.doesNotThrow(() => validateSpatialOverlays({...settings,steeringInput:{...spatial.steeringInputDefaults, source:"device"}}));
  assert.doesNotThrow(() => validateSpatialOverlays(spatialDefaults()));
});

test("driver seating follows car placement while old profiles inherit the default fit", () => {
  const item = { ...createSpatialOverlay(), wheelModel: "fpe2", driverEnabled: true, driverSeatHeight: -.12, driverLegLength: .9 };
  const saved = spatial.updateCarPlacement(item, "iracing-123", { driverSeatDistance: .04, driverShoulderWidth: 1.1 });
  const profile = createProfile(); profile.spatialOverlays.items = [saved];
  const loaded = parseProfile(JSON.stringify({ version: 1, profile })).spatialOverlays.items[0];
  assert.equal(spatial.overlayDriverFit(loaded, "iracing-123").driverSeatHeight, -.12);
  assert.equal(spatial.overlayDriverFit(loaded, "iracing-123").driverSeatDistance, .04);
  assert.equal(spatial.overlayDriverFit(loaded, "iracing-456").driverSeatDistance, 0);
  assert.equal(spatial.overlayDriverFit(loaded, "iracing-123").driverLegLength, .9);
  delete loaded.carPlacements[0].driverSeatHeight;
  assert.equal(spatial.overlayDriverFit(loaded, "iracing-123").driverSeatHeight, -.12);
  const changed = spatial.updateCarPlacement(loaded, "iracing-123", { x: .25 });
  assert.equal(spatial.overlayDriverFit(changed, "iracing-123").driverSeatHeight, -.12);
  assert.equal(item.carPlacements, undefined);
});

test("driver seating rejects invalid values in default and saved car placements", () => {
  for (const [key, bounds] of Object.entries(spatial.driverFitRanges || {})) {
    for (const value of [bounds[0] - .01, bounds[1] + .01, NaN, Infinity, "0", null]) {
      const settings = spatialDefaults();
      const item = { ...createSpatialOverlay(), wheelModel: "fpe2", [key]: value };
      settings.items = [item];
      assert.throws(() => validateSpatialOverlays(settings), key);
      delete item[key];
      settings.items = [spatial.updateCarPlacement(item, "iracing-1", { [key]: value })];
      assert.throws(() => validateSpatialOverlays(settings), `${key} per car`);
    }
  }
  assert.equal(Object.keys(spatial.driverFitRanges).length, 6);
});

test("menu visibility and selection reserve default and custom color chords, but allow explicit unbinding", () => {
  for (const field of ["shortcut", "selectShortcut"]) {
    for (const colorCorrection of [undefined, {}, { cycleShortcut: "Ctrl+Alt+F6" }, { enabled: false, cycleShortcut: "F4" }]) {
      const profile = createProfile();
      profile.colorCorrection = colorCorrection;
      const menu = createQuickMenuOverlay();
      menu[field] = colorCorrection?.cycleShortcut ?? "Ctrl+Alt+F6";
      profile.spatialOverlays.items = [menu];
      assert.throws(() => validateSpatialShortcuts(profile), /conflicts/, `${field} reserves ${menu[field]}`);
      profile.colorCorrection = { cycleShortcut: "" };
      assert.doesNotThrow(() => validateSpatialShortcuts(profile), "an explicitly unbound color chord is free");
    }
  }
});
test("spatial profiles cap slots, URL schemes, dimensions and placement", () => {
  const settings = spatialDefaults();
  settings.items = Array.from({ length: 15 }, () => createSpatialOverlay());
  validateSpatialOverlays(settings);
  settings.items.push(createSpatialOverlay()); assert.throws(() => validateSpatialOverlays(settings)); settings.items.pop();
  settings.items[0].url = "file:///C:/secret"; assert.throws(() => validateSpatialOverlays(settings));
  settings.items[0].url = "http://localhost:8888/dashboard"; validateSpatialOverlays(settings);
  settings.items[0].width = 4000; assert.throws(() => validateSpatialOverlays(settings)); settings.items[0].width = 1024;
  settings.items[0].z = 1; assert.throws(() => validateSpatialOverlays(settings)); settings.items[0].z = -1;
  settings.items[1].id = settings.items[0].id; assert.throws(() => validateSpatialOverlays(settings));
});
test("stream visibility is private by default and legacy overlays remain valid", () => {
  const settings = spatialDefaults(); settings.items.push(createSpatialOverlay());
  assert.equal(settings.items[0].showInStream, false);
  settings.items[0].showInStream = true; validateSpatialOverlays(settings);
  settings.items[0].showInStream = "true"; assert.throws(() => validateSpatialOverlays(settings));
  delete settings.items[0].showInStream; validateSpatialOverlays(settings);
});
test("cockpit occlusion round-trips per dashboard and defaults off for legacy overlays", () => {
  const profile = createProfile(), dashboard = createSpatialOverlay(), legacy = createSpatialOverlay();
  assert.equal(dashboard.cockpitOcclusion, false);
  dashboard.cockpitOcclusion = true;
  delete legacy.cockpitOcclusion;
  profile.spatialOverlays.items = [dashboard, legacy];
  const imported = parseProfile(JSON.stringify({ version: 1, profile }));
  assert.deepEqual(imported.spatialOverlays.items.map(item => item.cockpitOcclusion), [true, false]);
  assert.deepEqual(parseProfile(JSON.stringify({ version: 1, profile: normalizeProfile(imported) })), imported);
  assert.equal(Object.hasOwn(legacy, "cockpitOcclusion"), false);
  for (const value of ["true", 1, null]) {
    dashboard.cockpitOcclusion = value;
    assert.throws(() => parseProfile(JSON.stringify(profile)));
  }
  dashboard.cockpitOcclusion = true;
  for (const kind of ["desktop", "window", "menu"]) {
    dashboard.kind = kind;
    assert.equal(normalizeProfile(profile).spatialOverlays.items[0].cockpitOcclusion, false);
  }
});
test("pixel budget scales all enabled sources without charging disabled overlays", () => {
  const settings = spatialDefaults(); settings.enabled = true; settings.pixelBudgetMP = 15;
  settings.items = Array.from({ length: 15 }, () => ({ ...createSpatialOverlay(), enabled: true, width: 1920, height: 1080, fps: 30 }));
  const rates = effectiveOverlayRates(settings);
  assert.ok(rates.reduce((sum, fps, i) => sum + fps * settings.items[i].width * settings.items[i].height, 0) <= 15e6);
  assert.ok(rates.some((fps) => fps === 0), "budget may suspend lowest-priority sources below 1 fps");
  settings.items[0].enabled = false;
  assert.equal(effectiveOverlayRates(settings)[0], 0);
  settings.enabled = false; assert.ok(effectiveOverlayRates(settings).every((fps) => fps === 0));
});

test("per-car dashboard poses round-trip independently and unknown cars keep the base pose", () => {
  const profile = createProfile(), base = createSpatialOverlay();
  const ferrari = spatial.updateCarPlacement(base, "iracing-123", { x: 0.45, steeringFollow: true, pivotY: -0.12 });
  const saved = spatial.updateCarPlacement(ferrari, "iracing-456", { x: -0.75, widthMeters: 0.34 });
  profile.spatialOverlays.items = [saved, createSpatialOverlay()];
  const items = parseProfile(JSON.stringify({ version: 1, profile })).spatialOverlays.items;
  assert.equal(spatial.overlayPlacement(items[0], "iracing-123").x, 0.45);
  assert.equal(spatial.overlayPlacement(items[0], "iracing-123").pivotY, -0.12);
  assert.equal(spatial.overlayPlacement(items[0], "iracing-456").widthMeters, 0.34);
  assert.equal(spatial.overlayPlacement(items[0], "iracing-456").steeringFollow, false);
  for (const car of [null, "iracing-987"]) assert.equal(spatial.overlayPlacement(items[0], car).x, 0);
  assert.equal(spatial.overlayPlacement(items[1], "iracing-123").x, 0);
  assert.equal(base.carPlacements?.length || 0, 0);
  for (const patch of [{x:12}, {carId:""}, {pivotZ:100}, {steeringFollow:1}, {anchor:"guess"}]) {
    const bad = structuredClone(profile); Object.assign(bad.spatialOverlays.items[0].carPlacements[0], patch);
    assert.throws(() => parseProfile(JSON.stringify(bad)));
  }
});
test("desktop fields stay optional for old profiles and validate explicit monitor interaction", () => {
  const item = createSpatialOverlay(), settings = { ...spatialDefaults(), items: [item] };
  assert.equal(item.displayId, "");
  assert.equal(item.shortcut, "");
  assert.equal(item.interaction, "mouse");
  assert.equal(item.winkClicks, undefined);
  assert.equal(item.precisionScale, 1.75);
  assert.equal(item.precisionShortcut, "");
  Object.assign(item, { kind: "desktop", displayId: "-42", shortcut: "Ctrl+Alt+F12", interaction: "gaze" });
  validateSpatialOverlays(settings);
  for (const patch of [{ displayId: 42 }, { displayId: "screen:42:0" }, { shortcut: "Ctrl+A" }, { interaction: "automatic" }, { winkClicks: 1 }]) {
    const copy = structuredClone(settings); Object.assign(copy.items[0], patch);
    assert.throws(() => validateSpatialOverlays(copy));
  }
  item.kind = "url";
  for (const key of ["displayId", "shortcut", "interaction", "winkClicks"]) delete item[key];
  validateSpatialOverlays(settings);
});

test("legacy desktop eye input migrates to head pointing and drops wink settings", () => {
  const profile = createProfile();
  profile.spatialOverlays.items = [{ ...createSpatialOverlay(), kind: "desktop", interaction: "gaze", winkClicks: true }];
  const next = normalizeProfile(profile);
  assert.equal(next.spatialOverlays.items[0].interaction, "head");
  assert.equal(Object.hasOwn(next.spatialOverlays.items[0], "winkClicks"), false);
  assert.equal(profile.spatialOverlays.items[0].interaction, "gaze", "migration does not mutate the imported object");
});

test("precision zoom and hold shortcut accept bounded optional settings", () => {
  const item = { ...createSpatialOverlay(), kind: "desktop", interaction: "head", precisionScale: 2.25, precisionShortcut: "Ctrl+F7" };
  const settings = { ...spatialDefaults(), items: [item] };
  validateSpatialOverlays(settings);
  for (const patch of [{ precisionScale: 0.9 }, { precisionScale: 3.1 }, { precisionScale: "2" }, { precisionShortcut: "Ctrl+A" }, { precisionShortcut: 42 }, { shortcut: "Ctrl+F7" }]) {
    const copy = structuredClone(settings); Object.assign(copy.items[0], patch);
    assert.throws(() => validateSpatialOverlays(copy));
  }
  delete item.precisionScale; delete item.precisionShortcut;
  validateSpatialOverlays(settings);
});
