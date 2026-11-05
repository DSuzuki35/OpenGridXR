import test from "node:test";
import assert from "node:assert/strict";
import { createSpatialOverlay, createQuickMenuOverlay, spatialDefaults, validateSpatialOverlays, validateSpatialShortcuts, effectiveOverlayRates } from "../src/spatial-overlays.mjs";
import { createProfile, normalizeProfile } from "../src/model.mjs";

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
