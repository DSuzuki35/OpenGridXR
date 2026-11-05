import {test} from 'node:test';
import assert from 'node:assert/strict';
import {createDocument, createProfile, validateProfile, parseProfile} from '../src/model.mjs';
import {toRuntimeIni} from '../desktop/runtime-config.cjs';
import {effectiveDocument, moduleDefaults} from '../src/modules.mjs';
import {streamHelmetAppearance} from '../src/helmet.mjs';

test('stream controls round trip and emit independent runtime settings', () => {
  const p=createProfile('lmu');
  Object.assign(p.stream,{adaptiveHorizon:true,pitchLock:37.5,mirror:true,mirrorLimit:12.5,mirrorSoftness:62,mirrorStretch:48});
  p.stream.helmet={enabled:true,scale:175,offsetX:12,offsetY:-18,opacity:75};
  const read=parseProfile(JSON.stringify({version:1,profile:p}));
  assert.deepEqual(read.stream,p.stream);
  const ini=toRuntimeIni({profiles:[read],selectedProfileId:read.id});
  for(const line of ['AdaptiveHorizon=1','PitchLock=37.5','MirrorLimit=12.5','MirrorSoftness=62','MirrorStretch=48','EnhancementsEnabled=0','ColorCorrectionEnabled=0']) assert.ok(ini.includes(line),line);
});
test('old profiles keep legacy mirror behavior and accept absent new controls',()=>{
  const p=createProfile('iracing');
  for(const k of ['adaptiveHorizon','pitchLock','mirrorLimit','mirrorSoftness','mirrorStretch','helmet']) delete p.stream[k];
  validateProfile(p);
  const ini=toRuntimeIni({profiles:[p],selectedProfileId:p.id});
  for(const line of ['AdaptiveHorizon=0','PitchLock=0','MirrorLimit=-1','MirrorSoftness=0','MirrorStretch=0']) assert.ok(ini.includes(line),line);
});
test('new stream controls reject non-finite values, invalid types and out of range values',()=>{
  for(const [key,value] of [['adaptiveHorizon',1],['pitchLock',NaN],['mirrorLimit',51],['mirrorSoftness',-1],['mirrorStretch','60']]) {
    const p=createProfile();p.stream[key]=value;assert.throws(()=>validateProfile(p));
  }
  for(const patch of [{enabled:1},{scale:201},{offsetX:Infinity},{offsetY:-101},{opacity:-1}]) {
    const p=createProfile();Object.assign(p.stream.helmet,patch);assert.throws(()=>validateProfile(p));
  }
});
test('stream helmet uses independent transforms without modifying headset, mask or grading',()=>{
  const p=createProfile();p.helmet.offsetX=20;p.helmet.opacity=88;p.helmet.visorHotkey=true;
  p.stream.helmet={enabled:true,scale:150,offsetX:-15,offsetY:10,opacity:45};
  const before=structuredClone(p), appearance=streamHelmetAppearance(p);
  assert.equal(appearance.enabled,true);assert.equal(appearance.offsetX,-15);assert.equal(appearance.scale,150);
  assert.equal(appearance.opacity,100);assert.equal(appearance.visorHotkey,false);
  assert.equal(appearance.visorBrightness,100);assert.equal(appearance.headMotion,0);
  assert.deepEqual(p,before);
});
test('disabling either stream or helmet module suppresses stream helmet without deleting its settings',()=>{
  const doc=createDocument();doc.profiles[0].stream.helmet.enabled=true;
  for(const id of ['stream','helmet']){
    const effective=effectiveDocument(doc,{...moduleDefaults(),[id]:false});
    assert.equal(effective.profiles[0].stream.helmet.enabled,false);
    assert.equal(doc.profiles[0].stream.helmet.enabled,true);
  }
});
