import test from 'node:test';
import assert from 'node:assert/strict';
import * as model from '../src/model.mjs';
import {effectiveDocument, moduleDefaults} from '../src/modules.mjs';
import {helmetPreviewGeometry} from '../src/helmet.mjs';
import {toRuntimeIni, toEnhancementsConfig} from '../desktop/runtime-config.cjs';

const documentFor = p => ({version:1, selectedProfileId:p.id, profiles:[p]});
const legacy = mode => {
  const p = model.createProfile(); delete p.stream.preserveFullFrame; p.mode=mode;
  p.mask.enabled=true; p.view.crop={left:10,right:10,top:10,bottom:10};
  p.stream.extraLeft=20; p.stream.extraRight=10;
  return p;
};

test('new profiles have explicit render-area policy and no global mode',()=>{
  const p=model.createProfile();
  assert.equal(Object.hasOwn(p,'mode'),false);
  assert.equal(p.stream.preserveFullFrame,false);
  assert.equal(model.usesFullRenderArea(p),false);
  model.validateProfile(p);
});

for(const mode of ['driving','streaming']) test(`legacy ${mode} imports preserve geometry and drop global mode on export`,()=>{
  const p=legacy(mode), before=structuredClone(p), full=mode==='streaming';
  model.validateProfile(p); assert.deepEqual(p,before,'validation is read-only');
  const migrated=model.parseProfile(JSON.stringify({version:1,profile:p}));
  assert.equal(migrated.stream.preserveFullFrame,full);
  assert.equal(Object.hasOwn(migrated,'mode'),false);
  assert.deepEqual(p,before,'import does not mutate source');
  assert.deepEqual(model.parseProfile(JSON.stringify(migrated)),migrated);
  for(const profile of [p,migrated]) {
    assert.equal(model.usesFullRenderArea(profile),full);
    assert.equal(model.effectiveMask(profile),!full);
    assert.equal(model.pixelBudget(profile).renderedPercent,full?130:64);
    assert.equal(model.pixelBudget(profile).retainedPercent,64,'crop still frames output');
    assert.equal(helmetPreviewGeometry(profile).contour===null,full);
    const ini=toRuntimeIni(documentFor(profile));
    for(const line of [`Mode=${mode}`,`ExtraLeft=${full?20:0}`,`MaskEnabled=${full?0:1}`,'CropLeft=10']) assert.ok(ini.includes(line),line);
  }
});

test('explicit render-area policy wins over either legacy mode',()=>{
  for(const [mode,full] of [['streaming',false],['driving',true]]) {
    const p=legacy(mode); p.stream.preserveFullFrame=full;
    const migrated=model.parseProfile(JSON.stringify(p));
    assert.equal(migrated.stream.preserveFullFrame,full);
    assert.equal(model.effectiveMask(p),!full);
    assert.match(toRuntimeIni(documentFor(p)),new RegExp(`^Mode=${full?'streaming':'driving'}$`,'m'));
  }
});

test('missing policy defaults false and malformed policy or legacy mode is rejected',()=>{
  const p=legacy('driving'); delete p.mode;
  assert.equal(model.parseProfile(JSON.stringify(p)).stream.preserveFullFrame,false);
  for(const invalid of [1,'false',null]) {
    p.stream.preserveFullFrame=invalid;
    assert.throws(()=>model.parseProfile(JSON.stringify(p)),/full.*frame|render.*area/i);
  }
  delete p.stream.preserveFullFrame; p.mode='unknown';
  assert.throws(()=>model.validateProfile(p),/mode/i);
});

test('module off is conservative without changing saved legacy or explicit policy',()=>{
  for(const p of [legacy('streaming'),{...legacy('streaming'),stream:{...legacy('streaming').stream,preserveFullFrame:true}}]) {
    const doc=documentFor(p), before=structuredClone(doc);
    const effective=effectiveDocument(doc,{...moduleDefaults(),stream:false});
    assert.equal(model.usesFullRenderArea(effective.profiles[0]),false);
    assert.match(toRuntimeIni(effective),/^Mode=driving$/m);
    assert.match(toRuntimeIni(effective),/^ExtraLeft=0$/m);
    assert.deepEqual(doc,before);
  }
});

test('runtime HUD describes configured geometry without a global mode label',()=>{
  const p=model.createProfile();p.stream.preserveFullFrame=true;
  const summary=toEnhancementsConfig(documentFor(p),p.game).core.osd.toolkitSummary;
  assert.match(summary,/full render area/i);
  assert.doesNotMatch(summary,/undefined|Configured: streaming|Configured: driving/);
});
