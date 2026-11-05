import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { createInputController } = require('../desktop/input-controller.cjs');
const input = { deviceId:'dinput:{00000000-0000-0000-0000-000000000001}',kind:'button',index:4 };
const profile = {id:'profile',inputBindings:{enabled:true,bindings:[{...input,deviceName:'Wheel',actionId:'overlay.enabled'}]}};
const doc = {selectedProfileId:'profile',profiles:[profile]};
function setup(options={}) {
  let press,release,held,status; const active=[]; const changes=[];const holds=[];
  const controller = createInputController({
    createHost:({onPress,onRelease,onHeld,onStatus})=>{press=onPress;release=onRelease;held=onHeld;status=onStatus;return {setActive:v=>active.push(v),state:()=>({devices:[{id:input.deviceId,name:'Wheel'}],available:true}),close(){active.push(false)}}},
    listActions:()=>[{id:'overlay.enabled',disabled:false}],
    applyAction:async(...args)=>changes.push(args),applyHoldAction:(...args)=>holds.push(args), ...options,
  });
  controller.update(doc);
  return {controller,press:x=>press(x),release:x=>release?.(x),held:inputs=>held?.({deviceId:input.deviceId,inputs}),status:x=>status(x),active,changes,holds};
}
const tick=()=>new Promise(resolve=>setImmediate(resolve));
const holdSetup=(options={})=>{const f=setup({listActions:()=>[{id:'overlay.enabled',disabled:false,hold:true}],...options});f.controller.editorState({dirty:false,busy:false,profileId:'profile'});return f;};

test('recreating the bindings service never reuses a precision press id',()=>{
  const first=holdSetup();first.press(input);const oldId=first.holds[0][3];first.controller.close();
  const second=holdSetup();second.press(input);assert.ok(second.holds[0][3]>oldId,'spatial host may outlive a bindings service');second.controller.close();
});

test('hold actions renew only on fresh hardware reports and release with the same press id',async(t)=>{
  t.mock.timers.enable({apis:['setTimeout','Date'],now:1000});
  const f=holdSetup();f.held([input]);assert.deepEqual(f.holds,[],'baseline held state is not a press');
  f.press(input);await tick();assert.equal(f.holds.length,1);assert.equal(f.changes.length,0);
  const id=f.holds[0][3];assert.ok(Number.isSafeInteger(id)&&id>0);
  t.mock.timers.tick(100);assert.equal(f.holds.length,1,'cached JS down state must not renew');
  f.held([input]);assert.deepEqual(f.holds.at(-1),['profile','overlay.enabled',true,id]);
  f.release(input);assert.deepEqual(f.holds.at(-1),['profile','overlay.enabled',false,id]);
  f.held([input]);assert.equal(f.holds.length,3);
  f.press(input);assert.ok(f.holds.at(-1)[3]>id);f.controller.close();
});

test('paused holds cannot resume without a release and fresh edge',()=>{
  for(const cancel of [f=>f.controller.editorState({dirty:true,busy:false,profileId:'profile'}),f=>f.controller.captureStart('overlay.enabled','profile'),f=>f.controller.update({...doc,profiles:[{...profile,inputBindings:{...profile.inputBindings,enabled:false}}]}),f=>f.status({available:false,devices:[]})]){
    const f=holdSetup();f.press(input);assert.equal(f.holds[0]?.[2],true);cancel(f);assert.equal(f.holds.at(-1)[2],false);
    const count=f.holds.length;f.controller.captureCancel();f.controller.update(doc);f.controller.editorState({dirty:false,busy:false,profileId:'profile'});
    f.held([input]);f.press(input);assert.equal(f.holds.length,count);
    f.release(input);f.press(input);assert.equal(f.holds.at(-1)[2],true);f.controller.close();
  }
});

test('missing fresh hardware expires a hold without rearming its old press',t=>{
  t.mock.timers.enable({apis:['setTimeout','Date'],now:1000});const f=holdSetup();f.press(input);
  t.mock.timers.tick(301);assert.equal(f.holds.at(-1)[2],false);const count=f.holds.length;
  f.held([input]);f.press(input);assert.equal(f.holds.length,count);f.release(input);f.press(input);assert.equal(f.holds.at(-1)[2],true);f.controller.close();
});

test('sleep or an event-loop stall cannot renew an expired press before its timer runs',t=>{
  t.mock.timers.enable({apis:['setTimeout','Date'],now:1000});const f=holdSetup();f.press(input);
  t.mock.timers.setTime(2000);f.held([input]);assert.equal(f.holds.at(-1)[2],false);
  const count=f.holds.length;f.held([input]);f.press(input);assert.equal(f.holds.length,count);f.controller.close();
});

test('a pending hold apply cannot overwrite its later release',async()=>{
  let finish;const effects=[];const f=holdSetup({applyHoldAction:(p,a,held,id)=>held?new Promise(resolve=>{finish=()=>{effects.push(true);resolve();};}):effects.push(false)});
  f.press(input);assert.ok(finish);f.release(input);assert.deepEqual(effects,[false]);finish();await tick();assert.deepEqual(effects,[false,true,false]);f.controller.close();
});

test('spatial precision cancels on source, module, mode, and profile changes',()=>{
  const actionId='spatialOverlays.00000000-0000-0000-0000-000000000002.precision';
  for(const change of [p=>p.spatialOverlays.enabled=false,p=>p.spatialOverlays.items[0].enabled=false,p=>p.spatialOverlays.items[0].interaction='mouse',p=>p.spatialOverlays.items[0].kind='url']){
    const p={...structuredClone(profile),spatialOverlays:{enabled:true,items:[{id:'00000000-0000-0000-0000-000000000002',enabled:true,kind:'desktop',interaction:'head'}]}};
    p.inputBindings.bindings[0].actionId=actionId;
    const f=holdSetup({listActions:()=>[{id:actionId,hold:true,disabled:false}]});f.controller.update({selectedProfileId:'profile',profiles:[p]});
    f.press(input);assert.equal(f.holds.at(-1)?.[2],true);change(p);f.controller.update({selectedProfileId:'profile',profiles:[p]});assert.equal(f.holds.at(-1)[2],false);
    const count=f.holds.length;f.held([input]);assert.equal(f.holds.length,count);f.controller.close();
  }
  const f=holdSetup();f.press(input);f.controller.update({selectedProfileId:'other',profiles:[{...profile,id:'other'}]});assert.equal(f.holds.at(-1)[2],false);f.controller.close();
});

test('manual clicks on the same desktop preserve and renew precision while the click completes',async()=>{
  const sourceId='00000000-0000-0000-0000-000000000002',precision=`spatialOverlays.${sourceId}.precision`;
  for(const suffix of ['leftClick','rightClick']){
    let done;const click={...input,index:5},actionId=`spatialOverlays.${sourceId}.${suffix}`;
    const p=structuredClone(profile);p.spatialOverlays={enabled:true,items:[{id:sourceId,enabled:true,kind:'desktop',interaction:'head'}]};
    p.inputBindings.bindings=[{...input,deviceName:'Wheel',actionId:precision},{...click,deviceName:'Wheel',actionId}];
    const f=holdSetup({listActions:()=>[{id:precision,hold:true},{id:actionId}],applyAction:()=>new Promise(resolve=>done=resolve)});
    f.controller.update({selectedProfileId:'profile',profiles:[p]});f.press(input);const id=f.holds[0][3];
    f.press(click);await tick();assert.ok(done);assert.equal(f.holds.length,1,'click does not release precision');
    f.held([input,click]);assert.deepEqual(f.holds.at(-1),['profile',precision,true,id]);
    done();await tick();f.release(input);assert.deepEqual(f.holds.at(-1),['profile',precision,false,id]);f.controller.close();
  }
});
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

test('game centering captures button and POV inputs without dispatch, then runs only a saved clean binding', async () => {
  const { createProfile } = await import('../src/model.mjs');
  const { listToggleActions } = await import('../src/input-bindings.mjs');
  for (const pressed of [input, {...input,kind:'pov',index:1,direction:6}]) {
    const p=createProfile('lmu');p.centering.gameShortcut='F1';
    const f=setup({listActions:listToggleActions});
    f.controller.update({selectedProfileId:p.id,profiles:[p]});
    f.controller.editorState({dirty:false,busy:false,profileId:p.id});
    f.controller.captureStart('centering.game',p.id);f.press(pressed);await tick();
    const captured=f.controller.captureStatus();
    assert.equal(captured.actionId,'centering.game');assert.equal(captured.profileId,p.id);
    assert.deepEqual(captured.input,{...pressed,deviceName:'Wheel'});assert.deepEqual(f.changes,[]);
    f.controller.captureCancel();
    f.press(pressed);await tick();assert.deepEqual(f.changes,[],'Capture does not save the binding');
    p.inputBindings.bindings=[{...captured.input,actionId:'centering.game'}];
    f.controller.update({selectedProfileId:p.id,profiles:[p]});
    f.controller.editorState({dirty:true,busy:false,profileId:p.id});
    f.press(pressed);await tick();assert.deepEqual(f.changes,[]);
    f.controller.editorState({dirty:false,busy:false,profileId:p.id});
    f.press(pressed);await tick();assert.deepEqual(f.changes,[[p.id,'centering.game']]);
    p.centering.gameShortcut='';f.controller.update({selectedProfileId:p.id,profiles:[p]});
    assert.equal(f.active.at(-1),false);f.press(pressed);await tick();assert.equal(f.changes.length,1);
    f.controller.close();
  }
});
