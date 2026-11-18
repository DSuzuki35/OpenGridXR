import {test} from 'node:test';
import assert from 'node:assert/strict';
import {createDocument, createProfile, validateProfile, parseProfile} from '../src/model.mjs';
import {toRuntimeIni} from '../desktop/runtime-config.cjs';
import {effectiveDocument, moduleDefaults} from '../src/modules.mjs';
import {streamHelmetAppearance} from '../src/helmet.mjs';
import {cameraPreset} from '../src/camera-presets.mjs';

test('camera presets apply upstream tuning and convert total crop to reserve per edge',()=>{
  for(const [id,smoothing,horizon,margin,limit,softness] of [
    ['natural',25,25,4,10,60],['comfort',60,50,6,12,70],['strong',70,85,7.5,12,75]
  ]){
    assert.deepEqual(cameraPreset(id),{stabilize:true,smoothing,horizonLock:horizon,adaptiveHorizon:true,
      reserve:{left:margin,right:margin,top:margin,bottom:margin}, mirror:true,mirrorLimit:limit,mirrorSoftness:softness});
  }
});
test('bypass persists without changing camera tuning and old profiles enable correction',()=>{
  const p=createProfile('iracing');
  p.stream.stabilize=false; p.stream.smoothing=74;
  const ini=()=>toRuntimeIni({profiles:[p],selectedProfileId:p.id});
  assert.equal(parseProfile(JSON.stringify({version:1,profile:p})).stream.stabilize,false);
  assert.match(ini(),/Stabilize=0/);assert.match(ini(),/Smoothing=74/);
  delete p.stream.stabilize; assert.match(ini(),/Stabilize=1/);
  p.stream.stabilize=1; assert.throws(()=>validateProfile(p));
});

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
  for(const patch of [{enabled:1},{scale:201},{offsetX:Infinity},{offsetY:-101},{opacity:-1},{ambientLight:101},{ambientLight:-1},{ambientLight:'70'}]) {
    const p=createProfile();Object.assign(p.stream.helmet,patch);assert.throws(()=>validateProfile(p));
  }
});
test('stream helmet uses independent transforms without modifying headset, mask or grading',()=>{
  const p=createProfile();p.helmet.offsetX=20;p.helmet.opacity=88;p.helmet.visorHotkey=true;
  p.stream.helmet={enabled:true,scale:150,offsetX:-15,offsetY:10,opacity:45};
  const before=structuredClone(p), appearance=streamHelmetAppearance(p);
  assert.equal(appearance.enabled,true);assert.equal(appearance.offsetX,-15);assert.equal(appearance.scale,150);
  assert.equal(appearance.opacity,100);assert.equal(appearance.visorHotkey,true);
  assert.equal(appearance.visorBrightness,100);assert.equal(appearance.stereoOffset,0);
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

test('Dan Suzuki applies the saved LMU stream camera and framing without copying display or helmet settings',()=>{
  const profile=createProfile('lmu');
  const tuning=cameraPreset('dan-suzuki');
  assert.deepEqual(tuning,{
    stabilize:true, smoothing:82.5, horizonLock:21.1, adaptiveHorizon:true, pitchLock:3.3,
    reserve:{left:18,right:18,top:18,bottom:18}, eye:1, offsetX:-77, offsetY:-28.3,
    mirror:true, mirrorLimit:29.3, mirrorSoftness:100, mirrorStretch:31.6,
    preserveFullFrame:true, extraSymmetric:true, extraLeft:0, extraRight:0,
  });
  Object.assign(profile.stream,tuning);
  validateProfile(profile);
  profile.stream.reserve.left=0;
  assert.equal(cameraPreset('dan-suzuki').reserve.left,18);
});

test('stream blur can override or turn off helmet softness independently, with the old look as fallback',()=>{
  const p=createProfile(); p.helmet.shellBlur=3;
  delete p.stream.helmet.shellBlur;
  assert.equal(streamHelmetAppearance(p).shellBlur,3);
  for (const blur of [0,5.5]) {
    p.stream.helmet.shellBlur=blur;
    assert.equal(streamHelmetAppearance(p).shellBlur,blur);
    assert.equal(p.helmet.shellBlur,3); validateProfile(p);
  }
  p.stream.helmet.shellBlur=9; assert.throws(()=>validateProfile(p));
});
