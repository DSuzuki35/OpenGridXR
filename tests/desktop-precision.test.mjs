import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {createDocument} from '../src/model.mjs';
import {createSpatialOverlay} from '../src/spatial-overlays.mjs';
const {createDesktopPrecision} = createRequire(import.meta.url)('../desktop/desktop-precision.cjs');

function fixture() {
  const doc=createDocument(),profile=doc.profiles[0],item={...createSpatialOverlay(),kind:'desktop',interaction:'head',enabled:true};
  profile.spatialOverlays={...profile.spatialOverlays,enabled:true,items:[item]};
  let editor={profileId:profile.id,dirty:false,busy:false},enabled=true;
  const sent=[],controller={desktopAction(...args){sent.push(args);}},action=`spatialOverlays.${item.id}.precision`;
  const gate=createDesktopPrecision({getDocument:()=>doc,getEditor:()=>editor,isEnabled:()=>enabled,getSpatial:()=>controller});
  return {doc,profile,item,action,sent,gate,edit:value=>editor={...editor,...value},enable:value=>enabled=value};
}
test('precision leases use cached saved state and keep a matching release available after editor changes',()=>{
  const f=fixture(),before=structuredClone(f.doc);
  assert.equal(f.gate.apply(f.profile.id,f.action,true,1),true);
  assert.equal(f.gate.apply(f.profile.id,f.action,true,1),true);
  f.edit({dirty:true});
  assert.equal(f.gate.apply(f.profile.id,f.action,true,1),false);
  f.gate.apply(f.profile.id,f.action,false,1);
  assert.equal(f.sent.at(-1)[3].held,false);
  assert.deepEqual(f.doc,before,'lease renewal does not mutate or save profiles');
});
test('stale releases and repeated old presses cannot replace a new precision press',()=>{
  const f=fixture();
  f.gate.apply(f.profile.id,f.action,true,1);f.gate.apply(f.profile.id,f.action,false,1);
  assert.equal(f.gate.apply(f.profile.id,f.action,true,1),false);
  f.gate.apply(f.profile.id,f.action,true,2);
  const count=f.sent.length;f.gate.apply(f.profile.id,f.action,false,1);
  assert.equal(f.sent.length,count);
  f.gate.close();assert.deepEqual(f.sent.at(-1)[3],{held:false,pressId:2});
});
test('precision rejects hidden, mouse, foreign profile, disabled modules and invalid hold records',()=>{
  for(const change of [f=>f.item.enabled=false,f=>f.item.interaction='mouse',f=>f.profile.spatialOverlays.enabled=false,f=>f.enable(false),f=>f.edit({profileId:'other'})]) {
    const f=fixture();change(f);assert.equal(f.gate.apply(f.profile.id,f.action,true,1),false);assert.equal(f.sent.length,0);
  }
  const f=fixture();
  for(const [held,id] of [[true,0],[true,1.5],['yes',1],[true,Infinity]]) assert.equal(f.gate.apply(f.profile.id,f.action,held,id),false);
  assert.equal(f.sent.length,0);
});
