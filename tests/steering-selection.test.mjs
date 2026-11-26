import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { createDocument, createProfile } from '../src/model.mjs';
import { createSpatialOverlay, steeringInputDefaults } from '../src/spatial-overlays.mjs';
const { steeringProfile } = createRequire(import.meta.url)('../desktop/steering-selection.cjs');
test('assigned axis follows the running game rather than another selected editor profile', () => {
  const document = createDocument(), racing = document.profiles[0], lmu = createProfile();
  racing.game = 'iracing'; lmu.game = 'lmu'; document.profiles.push(lmu);
  document.activeProfiles = { iracing: racing.id, lmu: lmu.id };
  racing.spatialOverlays = { ...racing.spatialOverlays, enabled: true, steeringInput: { ...steeringInputDefaults, source: 'device' },
    items: [{ ...createSpatialOverlay(), enabled:true, steeringFollow:true }] };
  const sessions = [{game:'iracing',active:true,engineLoaded:true}];
  document.selectedProfileId = lmu.id;
  assert.equal(steeringProfile(document,sessions)?.id,racing.id);
  assert.equal(steeringProfile(document,[]),undefined);
  racing.spatialOverlays.enabled=false;
  assert.equal(steeringProfile(document,sessions),undefined);
  racing.spatialOverlays.enabled=true;
  racing.spatialOverlays.items[0].steeringFollow=false;
  assert.equal(steeringProfile(document,sessions),undefined);
  racing.spatialOverlays.items[0].carPlacements=[{steeringFollow:true}];
  assert.equal(steeringProfile(document,sessions)?.id,racing.id);
});
