import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
const {createColorCorrection}=createRequire(import.meta.url)('../desktop/color-correction.cjs');
const state={available:true,supported:true,enabled:true,activePreset:'night',effectActive:true,error:''};
test('status and cycle use fixed helper commands and return the acknowledged runtime preset',async()=>{
  const commands=[],controller=createColorCorrection({run:async args=>{commands.push(args);return {stdout:JSON.stringify(state)};}});
  assert.deepEqual(await controller.status('lmu'),state);
  assert.deepEqual(await controller.cycle('lmu'),state);
  assert.deepEqual(commands,[['--color-status','--game','lmu'],['--color-cycle','--game','lmu']]);
  await assert.rejects(()=>controller.cycle('other'));
  controller.close();assert.equal((await controller.status('lmu')).available,false);
  await assert.rejects(()=>controller.cycle('lmu'));
  assert.equal(commands.length,2);
});
test('missing sessions and malformed native replies never claim an active effect',async()=>{
  for(const reply of ['not json',JSON.stringify({...state,activePreset:'invented'}),JSON.stringify({...state,supported:'yes'})]) {
    const c=createColorCorrection({run:async()=>({stdout:reply})});
    assert.equal((await c.status('iracing')).effectActive,false);
    await assert.rejects(()=>c.cycle('iracing'));
  }
  const c=createColorCorrection({run:async()=>{throw Object.assign(Error('failed'),{stdout:JSON.stringify({...state,available:false,error:'No active session'})});}});
  assert.equal((await c.status('lmu')).error,'No active session');
  await assert.rejects(()=>c.cycle('lmu'),/No active session/);
});
test('test mode and editor cancellation cannot dispatch a native color command',async()=>{
  let calls=0;const run=async()=>{calls++;return {stdout:JSON.stringify(state)};};
  const testController=createColorCorrection({testMode:true,run});
  assert.equal((await testController.status('lmu')).effectActive,false);await assert.rejects(()=>testController.cycle('lmu'));
  const c=createColorCorrection({run});await assert.rejects(()=>c.cycle('lmu',{canApply:()=>false}),/Save|editor/);
  assert.equal(calls,0);
});
test('simultaneous cycle requests are bounded while the helper acknowledges',async()=>{
  let finish;const c=createColorCorrection({run:()=>new Promise(resolve=>finish=resolve)});
  const first=c.cycle('lmu');await assert.rejects(()=>c.cycle('lmu'),/pending/);
  finish({stdout:JSON.stringify(state)});assert.deepEqual(await first,state);
});
