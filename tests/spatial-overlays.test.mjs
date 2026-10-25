import test from "node:test";
import assert from "node:assert/strict";
import { createSpatialOverlay, spatialDefaults, validateSpatialOverlays, effectiveOverlayRates } from "../src/spatial-overlays.mjs";
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
