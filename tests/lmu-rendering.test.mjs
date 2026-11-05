import {test} from 'node:test';
import assert from 'node:assert/strict';
import {createProfile, validateProfile, parseProfile} from '../src/model.mjs';
import {moduleDefaults, effectiveDocument, validateModuleSettings} from '../src/modules.mjs';
import * as rendering from '../src/lmu-rendering.mjs';

test('quad controls expose legal per-axis limits and clamp only the edited value',()=>{
  for(const provider of ['qvfr','vectorxr']){
    const settings={...rendering.lmuRenderingDefaults(),mode:'quad',provider,peripheralScale:30,focusScale:125,focusWidth:37,focusHeight:53};
    assert.deepEqual(rendering.lmuRenderingLimits(settings),{
      peripheralScale:{min:10,max:46},focusScale:{min:82,max:400},
      focusWidth:{min:24,max:90},focusHeight:{min:24,max:90},
    });
    for(const [key,raw,want] of [['peripheralScale',400,46],['focusScale',25,82],['focusWidth',10,24],['focusHeight',10,24]]){
      const next=rendering.updateLmuRendering(settings,{[key]:raw});
      assert.deepEqual(next,{...settings,[key]:want});
      assert.doesNotThrow(()=>rendering.validateLmuRendering(next));
    }
    assert.deepEqual(rendering.updateLmuRendering(settings,{focusWidth:38}),{...settings,focusWidth:38});
  }
});

test('entering quad mode repairs incompatible inactive preferences without changing focus geometry',()=>{
  const settings={...rendering.lmuRenderingDefaults(),mode:'stereoVrs',provider:'qvfr',peripheralScale:100,focusScale:25,focusWidth:10,focusHeight:20};
  assert.equal(rendering.lmuRenderingLimits(settings),null);
  assert.equal(rendering.lmuRenderingLimits({...settings,mode:'quad',provider:'native'}),null);
  const next=rendering.updateLmuRendering(settings,{mode:'quad'});
  assert.deepEqual(next,{...settings,mode:'quad',peripheralScale:10,focusScale:100});
  assert.doesNotThrow(()=>rendering.validateLmuRendering(next));
  const ordinary={...settings,mode:'quad',provider:'native',focusScale:200};
  assert.deepEqual(rendering.updateLmuRendering(ordinary,{provider:'vectorxr'}),{...ordinary,provider:'vectorxr',peripheralScale:20});
  assert.deepEqual(rendering.updateLmuRendering(settings,{focusWidth:11}),{...settings,focusWidth:11});
});

test('fractional saved profiles retain valid boundary values without inverted slider limits',()=>{
  const settings={...rendering.lmuRenderingDefaults(),mode:'quad',provider:'qvfr',peripheralScale:34.2,focusScale:38,focusWidth:90,focusHeight:90};
  assert.doesNotThrow(()=>rendering.validateLmuRendering(settings));
  const limits=rendering.lmuRenderingLimits(settings);
  for(const [key,range] of Object.entries(limits))assert.ok(range.min<=settings[key]&&settings[key]<=range.max,key);
  assert.equal(limits.focusWidth.min,90);assert.equal(limits.focusHeight.min,90);
  assert.equal(limits.focusScale.min,38);
  assert.deepEqual(rendering.updateLmuRendering(settings,{focusWidth:90}),settings);
  assert.deepEqual(rendering.updateLmuRendering(settings,{provider:'vectorxr'}),{...settings,provider:'vectorxr'});
});

test('LMU rendering is opt-in, portable, and absent in older profiles', () => {
  const p=createProfile('lmu');
  assert.equal(p.lmuRendering?.mode,'off');
  p.lmuRendering.mode='quad'; p.lmuRendering.provider='qvfr';
  assert.deepEqual(parseProfile(JSON.stringify(p)).lmuRendering,p.lmuRendering);
  delete p.lmuRendering; assert.doesNotThrow(()=>validateProfile(p));
});
test('LMU rendering rejects non-LMU activation, unsupported controls and invalid provider geometry', () => {
  const p=createProfile('lmu');
  assert.ok(p.lmuRendering);
  for(const edit of [{dynamicFocus:true},{focusWidth:NaN},{peripheralShadingRate:3},{gaze:'dynamic'},{provider:'other'},{focusOffsetY:51}]) {
    assert.throws(()=>validateProfile({...p,lmuRendering:{...p.lmuRendering,...edit}}));
  }
  assert.throws(()=>validateProfile({...p,game:'iracing',lmuRendering:{...p.lmuRendering,mode:'quad'}}));
  assert.throws(()=>validateProfile({...p,lmuRendering:{...p.lmuRendering,mode:'quad',provider:'qvfr',peripheralScale:100}}));
});
test('VectorXR quad targets must fit within both focus dimensions before a profile can launch',()=>{
  const p=createProfile('lmu');
  const settings={...p.lmuRendering,mode:'quad',provider:'vectorxr',peripheralScale:100,focusScale:100,focusWidth:50,focusHeight:50};
  for(const nativeOuterResolution of [true,false])assert.throws(()=>validateProfile({...p,lmuRendering:{...settings,nativeOuterResolution}}),/VectorXR outer image must fit/);
  assert.doesNotThrow(()=>validateProfile({...p,lmuRendering:{...settings,peripheralScale:50}}),'Equality fits the focus target.');
  assert.throws(()=>validateProfile({...p,lmuRendering:{...settings,peripheralScale:40,focusHeight:30}}),/VectorXR outer image must fit/,'The smaller focus axis limits the outer target.');
  for(const mode of ['off','stereoVrs'])assert.doesNotThrow(()=>validateProfile({...p,lmuRendering:{...settings,mode}}),'Inactive quad preferences do not block other modes.');
});
test('module disabling suppresses LMU hooks without deleting saved preferences; older module files migrate', () => {
  const p=createProfile('lmu'); assert.ok(p.lmuRendering); p.lmuRendering.mode='quad';
  const result=effectiveDocument({profiles:[p]}, {...moduleDefaults(),lmuRendering:false});
  assert.equal(result.profiles[0].lmuRendering.mode,'off'); assert.equal(p.lmuRendering.mode,'quad');
  const old=moduleDefaults(); delete old.lmuRendering;
  assert.equal(validateModuleSettings({version:1,enabled:old}).enabled.lmuRendering,true);
});
