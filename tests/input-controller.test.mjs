import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { createInputController } = require('../desktop/input-controller.cjs');
const input = { deviceId:'dinput:{00000000-0000-0000-0000-000000000001}',kind:'button',index:4 };
const profile = {id:'profile',inputBindings:{enabled:true,bindings:[{...input,deviceName:'Wheel',actionId:'overlay.enabled'}]}};
const doc = {selectedProfileId:'profile',profiles:[profile]};
function setup(options={}) {
  let press; const active=[]; const changes=[];
  const controller = createInputController({
    createHost:({onPress})=>{press=onPress;return {setActive:v=>active.push(v),state:()=>({devices:[{id:input.deviceId,name:'Wheel'}],available:true}),close(){active.push(false)}}},
    listActions:()=>[{id:'overlay.enabled',disabled:false}],
    applyAction:async(...args)=>changes.push(args), ...options,
  });
  controller.update(doc);
  return {controller,press:x=>press(x),active,changes};
}
const tick=()=>new Promise(resolve=>setImmediate(resolve));
test('input actions require clean matching editor and never replay ignored presses',async()=>{
  const f=setup(); f.press(input); await tick(); assert.equal(f.changes.length,0);
  f.controller.editorState({dirty:false,busy:false,profileId:'profile'});
  f.press(input); await tick(); assert.equal(f.changes.length,1);
  f.controller.editorState({dirty:true,busy:false,profileId:'profile'});
  f.press(input); await tick(); assert.equal(f.changes.length,1);
  f.controller.editorState({dirty:false,busy:false,profileId:'other'});
  f.press(input); await tick(); assert.equal(f.changes.length,1);
  f.controller.close();
});
test('learning consumes a press without toggling and stops an otherwise unused helper',async()=>{
  const f=setup(); f.controller.update({...doc,profiles:[{id:'profile'}]});
  f.controller.captureStart('overlay.enabled','profile');
  assert.equal(f.active.at(-1),true);
  f.press(input); await tick();
  assert.equal(f.changes.length,0);
  assert.equal(f.controller.captureStatus().state,'captured');
  assert.equal(f.controller.captureStatus().input.deviceName,'Wheel');
  assert.equal(f.active.at(-1),false); f.controller.close();
});
test('actions are bounded while saving, and closed controllers ignore late events',async()=>{
  let done; const f=setup({applyAction:()=>new Promise(resolve=>done=resolve)});
  f.controller.editorState({dirty:false,busy:false,profileId:'profile'});
  f.press(input); f.press(input); await tick(); assert.ok(done);
  assert.equal(f.controller.status().paused,true);
  f.controller.close(); done(); await tick(); f.press(input);
  assert.equal(f.active.at(-1),false);
});
test('a completed binding dialog consumes further presses until explicitly closed',async()=>{
  const f=setup(); f.controller.editorState({dirty:false,busy:false,profileId:'profile'});
  f.controller.captureStart('overlay.enabled','profile'); f.press(input); f.press(input); await tick();
  assert.equal(f.changes.length,0); assert.equal(f.controller.status().paused,true);
  f.controller.captureCancel(); f.press(input); await tick(); assert.equal(f.changes.length,1); f.controller.close();
});
test('test mode never starts hardware; disabled actions and malformed input are ignored',async()=>{
  const f=setup({testMode:true,listActions:()=>[{id:'overlay.enabled',disabled:true}]});
  f.controller.editorState({dirty:false,busy:false,profileId:'profile'});
  f.controller.captureStart('overlay.enabled','profile'); assert.equal(f.active.at(-1),false);
  f.press({...input,index:128}); assert.equal(f.controller.captureStatus().state,'listening');
  f.controller.captureCancel(); f.press(input); await tick(); assert.equal(f.changes.length,0); f.controller.close();
});

test('obsolete saved bindings neither start a reader nor dispatch configuration writes', async () => {
  const { createProfile } = await import('../src/model.mjs');
  const { listToggleActions } = await import('../src/input-bindings.mjs');
  const p = createProfile();
  p.inputBindings.bindings = [{ ...input, deviceName: 'Wheel', actionId: 'mask.linked' }];
  const f = setup({ listActions: listToggleActions });
  f.controller.update({ selectedProfileId: p.id, profiles: [p] });
  f.controller.editorState({ dirty: false, busy: false, profileId: p.id });
  assert.equal(f.active.at(-1), false);
  f.press(input);
  await tick();
  assert.deepEqual(f.changes, []);
  f.controller.close();
});
