import test from 'node:test';import assert from 'node:assert/strict';import {createRequire} from 'node:module';import {createProfile} from '../src/model.mjs';
const require=createRequire(import.meta.url),{menuActions}=require('../desktop/quick-menu.cjs');
test('saved feature flags cannot enable menu controls absent from the running session',()=>{
 const p=createProfile();p.overlay.enabled=p.colorCorrection.enabled=true;
 const actions=menuActions(p,{}, {live:{available:true,capabilities:{osd:false}},color:{available:true,supported:false,enabled:true}});
 for(const id of ['overlay.enabled','overlay.layout','colorCorrection.cycle'])assert.equal(actions.find(a=>a.id===id).disabled,true,id);
});
