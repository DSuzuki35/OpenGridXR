import test from 'node:test';
import assert from 'node:assert/strict';
import {EventEmitter} from 'node:events';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url);
let createQuickMenuGateway;
try { ({createQuickMenuGateway}=require('../desktop/quick-menu.cjs')); } catch {}
const token='11111111-1111-4111-8111-111111111111';
function fixture(){
  assert.equal(typeof createQuickMenuGateway,'function');
  let now=1000,current=true,busy=false,allowed=true;
  const sent=[],input=[],applied=[];
  const contents=new EventEmitter();contents.mainFrame={};contents.isDestroyed=()=>false;
  contents.send=(...args)=>sent.push(args);contents.sendInputEvent=value=>input.push(value);
  const gateway=createQuickMenuGateway({now:()=>now,nativeNow:()=>now,setTimer:()=>1,clearTimer:()=>{}});
  const source=gateway.register({contents,token,game:'lmu',profileId:'profile',id:'menu',width:1024,height:768,
    isCurrent:()=>current,getState:()=>({profileName:'Test',game:'lmu',busy,actions:[{id:'close',label:'Close menu',disabled:!allowed}]}),
    runAction:async(id,{canApply})=>{assert.ok(canApply());applied.push(id);return {message:'Applied'};}});
  let seq=0;
  const pointer=(event='move',patch={})=>({op:'ui-pointer',slot:0,event,u:.4,v:.5,uiToken:token,session:'10',focusEpoch:'2',configRevision:'3',sourceRevision:'4',sourceGeneration:'5',recenter:'6',tickMs:'1000',sequence:String(++seq),...patch});
  return {gateway,source,contents,sent,input,applied,pointer,evt:{sender:contents,senderFrame:contents.mainFrame},time:n=>now=n,current:v=>current=v,busy:v=>busy=v,allowed:v=>allowed=v};
}
test('local menu cannot run privileged actions without one fresh native click from its exact main frame',async()=>{
  const f=fixture();await assert.rejects(f.gateway.run(f.evt,'close'),/select|click/i);
  f.source.pointer(f.pointer('click'));
  await assert.rejects(f.gateway.run({...f.evt,senderFrame:{}},'close'),/untrusted/i);
  await f.gateway.run(f.evt,'close');assert.deepEqual(f.applied,['close']);
  await assert.rejects(f.gateway.run(f.evt,'close'),/select|click/i);
  assert.deepEqual(f.input.map(e=>e.type),['mouseMove','mouseDown','mouseUp']);f.gateway.close();
});
test('hidden/replaced sources, stale clicks, focus loss and changed epochs cancel authorization',async()=>{
  for(const invalidate of [f=>f.current(false),f=>f.time(1201),f=>f.source.pointer(f.pointer('leave')),f=>f.source.pointer(f.pointer('move',{focusEpoch:'9'}))]){
    const f=fixture();f.source.pointer(f.pointer('click'));invalidate(f);
    await assert.rejects(f.gateway.run(f.evt,'close'));assert.equal(f.applied.length,0);f.gateway.close();
  }
});
test('foreign tokens, replayed pointer sequences and arbitrary or disabled action ids cannot execute',async()=>{
  const f=fixture();f.source.pointer(f.pointer('click',{uiToken:'foreign'}));
  await assert.rejects(f.gateway.run(f.evt,'close'));
  const event=f.pointer('click');f.source.pointer(event);await f.gateway.run(f.evt,'close');
  f.source.pointer(event);await assert.rejects(f.gateway.run(f.evt,'close'));
  f.source.pointer(f.pointer('click'));await assert.rejects(f.gateway.run(f.evt,'installRuntime'),/unavailable/i);
  f.source.pointer(f.pointer('click'));f.allowed(false);await assert.rejects(f.gateway.run(f.evt,'close'),/unavailable/i);
  assert.deepEqual(f.applied,['close']);f.gateway.close();
});
test('record disposal denies old preload sender and releases visible-only refresh timer',async()=>{
  const f=fixture();f.source.pointer(f.pointer('click'));f.source.close();
  await assert.rejects(f.gateway.run(f.evt,'close'),/untrusted/i);
  f.gateway.close();assert.ok(f.sent.length>0);
});
test('a click delayed in the native pipe cannot acquire a new freshness window on receipt',async()=>{
  const f=fixture();f.time(1500);f.source.pointer(f.pointer('click',{tickMs:'1000'}));
  await assert.rejects(f.gateway.run(f.evt,'close'),/fresh|click|select/i);assert.equal(f.applied.length,0);f.gateway.close();
});
