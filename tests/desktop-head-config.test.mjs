import test from 'node:test';
import assert from 'node:assert/strict';
import {createProfile, normalizeProfile} from '../src/model.mjs';
import {createSpatialOverlay} from '../src/spatial-overlays.mjs';
import {toEnhancementsConfig} from '../desktop/runtime-config.cjs';

test('desktop pointer never requests a private gaze action, including migrated profiles',()=>{
  const p=createProfile('lmu');
  p.spatialOverlays.enabled=true;
  p.spatialOverlays.items=[{...createSpatialOverlay(),kind:'desktop',interaction:'gaze',winkClicks:true}];
  const next=normalizeProfile(p),doc={profiles:[next],selectedProfileId:next.id};
  const config=toEnhancementsConfig(doc,'lmu');
  assert.equal(Object.hasOwn(config.core,'desktop_gaze_enabled'),false);
  assert.equal(next.spatialOverlays.items[0].interaction,'head');
  assert.equal(Object.hasOwn(next.spatialOverlays.items[0],'winkClicks'),false);
});
