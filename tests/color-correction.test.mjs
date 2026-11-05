import test from 'node:test';
import assert from 'node:assert/strict';
import {createProfile, normalizeProfile, parseProfile, validateProfile} from '../src/model.mjs';
import {effectiveDocument, moduleDefaults, validateModuleSettings, moduleForAction} from '../src/modules.mjs';
import {listToggleActions, toggleProfileAction, validateBindings} from '../src/input-bindings.mjs';

test('new profiles have independent disabled neutral color correction', () => {
  const p = createProfile();
  assert.deepEqual(p.colorCorrection, {enabled:false,preset:'neutral',cycleShortcut:'Ctrl+Alt+F6',presets:{
    sun:{brightness:75,contrast:100,saturation:95,gamma:100},night:{brightness:110,contrast:95,saturation:95,gamma:135},custom:{brightness:100,contrast:100,saturation:100,gamma:100}}});
  assert.equal(Object.hasOwn(p.helmet,'visorBrightness'),false);
});

test('legacy closed fixed effective grading migrates once, retaining helmet and head controls', () => {
  const p=createProfile(); delete p.colorCorrection;
  Object.assign(p.helmet,{enabled:true,visorBrightness:75,visorContrast:110,visorSaturation:80,headMotion:.77,vehicleMotion:.65});
  const before=structuredClone(p), next=parseProfile(JSON.stringify(p));
  assert.equal(next.colorCorrection.enabled,true); assert.equal(next.colorCorrection.preset,'custom');
  assert.deepEqual(next.colorCorrection.presets.custom,{brightness:75,contrast:110,saturation:80,gamma:100});
  assert.equal(Object.hasOwn(next.helmet,'visorBrightness'),false);
  assert.equal(next.helmet.headMotion,.77); assert.equal(next.helmet.vehicleMotion,.65);
  assert.deepEqual(p,before); assert.deepEqual(normalizeProfile(next),next);
});

test('inactive legacy values remain disabled Custom and identity never activates grading', () => {
  for(const patch of [{enabled:false},{visorPosition:'half'},{visorPosition:'open'},{visorHotkey:true}]) {
    const p=createProfile();delete p.colorCorrection;Object.assign(p.helmet,{enabled:true,visorBrightness:70,...patch});
    const next=normalizeProfile(p);assert.equal(next.colorCorrection.enabled,false);assert.equal(next.colorCorrection.preset,'custom');assert.equal(next.colorCorrection.presets.custom.brightness,70);
  }
  const p=createProfile();delete p.colorCorrection;p.helmet.enabled=true;
  assert.equal(normalizeProfile(p).colorCorrection.preset,'neutral');
  const explicit=createProfile();explicit.helmet.visorBrightness=60;
  assert.equal(normalizeProfile(explicit).colorCorrection.preset,'neutral');
  const moduleOff=createProfile();delete moduleOff.colorCorrection;Object.assign(moduleOff.helmet,{enabled:true,visorBrightness:70});
  const preserved=normalizeProfile(moduleOff,{helmetModuleEnabled:false});
  assert.equal(preserved.colorCorrection.enabled,false);assert.equal(preserved.colorCorrection.preset,'custom');
  assert.equal(preserved.colorCorrection.presets.custom.brightness,70);
  assert.deepEqual(parseProfile(JSON.stringify(moduleOff),{helmetModuleEnabled:false}),preserved);
});

test('color module preference defaults on for older files and stays independent of helmet', () => {
  const old=moduleDefaults();delete old.colorCorrection;
  assert.equal(validateModuleSettings({version:1,enabled:old}).enabled.colorCorrection,true);
  const p=createProfile();p.colorCorrection.enabled=true;p.colorCorrection.preset='sun';
  const doc={profiles:[p]}, disabledHelmet=effectiveDocument(doc,{...moduleDefaults(),helmet:false});
  assert.equal(disabledHelmet.profiles[0].colorCorrection.enabled,true);
  const disabledColor=effectiveDocument(doc,{...moduleDefaults(),colorCorrection:false});
  assert.equal(disabledColor.profiles[0].colorCorrection.enabled,false);
  assert.equal(disabledColor.profiles[0].colorCorrection.cycleShortcut,'');
  assert.equal(p.colorCorrection.enabled,true);assert.equal(moduleForAction('colorCorrection.cycle'),'colorCorrection');
});

test('cycle is a transient action that accepts bindings and never mutates startup settings', () => {
  const p=createProfile();const action=listToggleActions(p).find(a=>a.id==='colorCorrection.cycle');
  assert.equal(action.transient,true);assert.equal(action.restartRequired,false);
  assert.equal(action.disabled,true);p.colorCorrection.enabled=true;
  assert.equal(listToggleActions(p).find(a=>a.id===action.id).disabled,false);
  const binding={actionId:action.id,deviceId:'dinput:{01234567-89ab-cdef-0123-456789abcdef}',deviceName:'Wheel',kind:'button',index:0};
  assert.doesNotThrow(()=>validateBindings({enabled:true,bindings:[binding]},p));
  const before=structuredClone(p);assert.throws(()=>toggleProfileAction(p,action.id),/unavailable/);assert.deepEqual(p,before);
});

test('preset validation, fixed neutral bypass, and gamma lift use finite bounded values', async () => {
  const {colorCorrectionDefaults,validateColorCorrection,resolvedColorPreset,colorPresetIsIdentity,gradeColorRgb}=await import('../src/color-correction.mjs');
  const s=colorCorrectionDefaults();assert.deepEqual(resolvedColorPreset(s,'neutral'),{brightness:100,contrast:100,saturation:100,gamma:100});
  s.presets.sun.brightness=50;assert.equal(colorCorrectionDefaults().presets.sun.brightness,75);
  for(const [key,bad] of [['brightness',49],['contrast',151],['saturation',-1],['gamma',201],['gamma',NaN]]) {
    const invalid=colorCorrectionDefaults();invalid.presets.custom[key]=bad;assert.throws(()=>validateColorCorrection(invalid));
  }
  for(const cycleShortcut of ['Alt+F13','Ctrl+A','F0','Shift+Ctrl+F6']) assert.throws(()=>validateColorCorrection({...s,cycleShortcut}));
  assert.throws(()=>validateColorCorrection({...s,preset:'rain'}));
  assert.throws(()=>validateColorCorrection({...s,presets:{...s.presets,neutral:{brightness:0}}}));
  assert.throws(()=>resolvedColorPreset(s,'rain'));
  const rgb=[.02,.2,.7],neutral=resolvedColorPreset(s,'neutral');
  assert.equal(colorPresetIsIdentity(neutral),true);assert.deepEqual(gradeColorRgb(rgb,neutral),rgb);
  const gamma={...neutral,gamma:135};const lifted=gradeColorRgb(rgb,gamma);
  assert.ok(lifted[0]>rgb[0] && lifted[1]>rgb[1]);assert.deepEqual(gradeColorRgb([0,0,0],gamma),[0,0,0]);
  const desaturated=gradeColorRgb([.02,.12,.3],{...gamma,saturation:0});
  const expected=.2126*Math.pow(.02,100/135)+.7152*Math.pow(.12,100/135)+.0722*Math.pow(.3,100/135);
  assert.ok(desaturated.every(value=>Math.abs(value-expected)<1e-12),'Gamma is applied before the saturation luma calculation');
  const p=createProfile();p.colorCorrection=s;assert.doesNotThrow(()=>validateProfile(p));
});
