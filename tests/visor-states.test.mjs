import {test} from 'node:test';
import assert from 'node:assert/strict';
import {createProfile, validateProfile, parseProfile} from '../src/model.mjs';
import * as helmet from '../src/helmet.mjs';
import {toRuntimeIni} from '../desktop/runtime-config.cjs';

test('visor states and raster detail preserve old profiles and reject unsupported values',()=>{
  const p=createProfile();
  assert.equal(p.helmet.visorPosition,'closed');
  assert.equal(p.helmet.visorHotkey,false);
  assert.deepEqual(helmet.helmetRasterSize(p.helmet),{width:2048,height:1152});
  for(const position of ['closed','half','open']) {
    p.helmet.visorPosition=position;p.helmet.visorHotkey=true;p.helmet.detail='detail';
    assert.deepEqual(parseProfile(JSON.stringify({version:1,profile:p})),p);
  }
  assert.deepEqual(helmet.helmetRasterSize(p.helmet),{width:4096,height:2304});
  for(const [key,value] of [['visorPosition','ajar'],['visorHotkey',1],['detail','8192']]) {
    const bad=structuredClone(p);bad.helmet[key]=value;assert.throws(()=>validateProfile(bad));
  }
  for(const key of ['visorPosition','visorHotkey','detail']) delete p.helmet[key];
  assert.doesNotThrow(()=>validateProfile(p));
  assert.deepEqual(helmet.helmetRasterSize(p.helmet),{width:2048,height:1152});
});

test('independent color grading stays active with helmet off or a moving visor',()=>{
  const p=createProfile();p.colorCorrection.enabled=true;p.colorCorrection.preset='sun';
  const config=()=>Number(toRuntimeIni({profiles:[p],selectedProfileId:p.id}).match(/ColorCorrectionEnabled=(\d)/)[1]);
  assert.equal(config(),1);
  p.helmet.enabled=true;
  p.helmet.visorPosition='open';assert.equal(config(),1);
  p.helmet.visorPosition='half';assert.equal(config(),1);
  p.helmet.visorPosition='closed';p.helmet.visorHotkey=true;
  assert.equal(config(),1);
});
