import test from 'node:test';
import assert from 'node:assert/strict';
import {createDocument} from '../src/model.mjs';
import {effectiveDocument,moduleDefaults} from '../src/modules.mjs';
import {toRuntimeIni,toEnhancementsConfig} from '../desktop/runtime-config.cjs';

test('VR color settings serialize independently of helmet visibility and leave stream settings unchanged',()=>{
  const doc=createDocument(),p=doc.profiles[0];
  p.helmet.enabled=false;p.framegen.enabled=false;p.overlay.enabled=false;p.spatialOverlays.enabled=false;
  p.colorCorrection.enabled=true;p.colorCorrection.preset='night';
  const stream=structuredClone(p.stream),ini=toRuntimeIni(doc).split('[lmu]')[0];
  assert.match(ini,/^ColorCorrectionEnabled=1$/m);assert.match(ini,/^ColorCorrectionPreset=night$/m);
  assert.match(ini,/^ColorCorrectionShortcut=Ctrl\+Alt\+F6$/m);
  assert.match(ini,/^ColorNightGamma=135$/m);assert.match(ini,/^ColorSunBrightness=75$/m);
  assert.doesNotMatch(ini,/HelmetVisor/);
  assert.match(ini,/^EnhancementsEnabled=1$/m);
  assert.equal(toEnhancementsConfig(doc,'iracing').core.enabled,true);
  assert.deepEqual(p.stream,stream);
});
test('Helmet module off preserves independent color while Color module off disables it',()=>{
  const doc=createDocument();doc.profiles[0].colorCorrection.enabled=true;
  const modules={...moduleDefaults(),helmet:false};
  assert.match(toRuntimeIni(effectiveDocument(doc,modules)).split('[lmu]')[0],/^ColorCorrectionEnabled=1$/m);
  modules.colorCorrection=false;
  assert.match(toRuntimeIni(effectiveDocument(doc,modules)).split('[lmu]')[0],/^ColorCorrectionEnabled=0$/m);
});
