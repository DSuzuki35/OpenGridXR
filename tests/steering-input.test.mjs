import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {existsSync} from 'node:fs';
const require=createRequire(import.meta.url),modulePath=new URL('../desktop/steering-input.cjs',import.meta.url);
const id='dinput:{01234567-89ab-cdef-0123-456789abcdef}';
const settings={source:'device',deviceId:id,axis:'x',lockDegrees:900,invert:false};
function api(){assert.ok(existsSync(modulePath),'Steering controller service must exist');return require('../desktop/steering-input.cjs');}
function setup(){const {createSteeringInputController}=api();let callbacks,state={available:false,devices:[],error:''};const calls=[],activation=[];
 const host={configureSteering:v=>calls.push(v),setActive:v=>activation.push(v),state:()=>structuredClone(state),close(){this.closed=true;}};
 const controller=createSteeringInputController({createHost:o=>{callbacks=o;return host;}});
 return {controller,host,calls,activation,report(v){state={...state,...v};callbacks.onStatus(state);},sample(v){callbacks.onAxes(v);}};
}
test('Artura dial tracking leases the existing helper without a steering assignment',()=>{
 const h=setup();h.controller.configure(undefined,false,true);
 assert.equal(h.activation.at(-1),true);assert.equal(h.calls.at(-1).enabled,false);assert.equal(h.calls.at(-1).discover,false);
 h.controller.configure(undefined,false,false);assert.equal(h.activation.at(-1),false);
 h.controller.configure(undefined,false,true);h.controller.close();assert.equal(h.activation.at(-1),false);
 assert.throws(()=>setup().controller.configure(undefined,false,'yes'),/dial activation/);
});
test('steering tokens are stable and separate every profile setting',()=>{const {steeringSettingsToken}=api();const token=steeringSettingsToken(settings);assert.ok(token>0 && token<=0xffffffff);assert.equal(token,steeringSettingsToken({...settings}));for(const changed of [{axis:'y'},{invert:true},{lockDegrees:1080},{source:'telemetry'},{deviceId:''}]) assert.notEqual(token,steeringSettingsToken({...settings,...changed}));});
test('steering discovery lease expires and releases immediately without an active assignment',t=>{
 t.mock.timers.enable({apis:['setTimeout','Date'],now:10000});const h=setup();h.controller.configure({source:'telemetry'},true);assert.equal(h.activation.at(-1),false);
 h.controller.status({discover:true});assert.equal(h.activation.at(-1),true);assert.equal(h.calls.at(-1).discover,true);assert.equal(h.calls.at(-1).enabled,false);
 t.mock.timers.tick(29999);assert.equal(h.activation.at(-1),true);t.mock.timers.tick(1);assert.equal(h.activation.at(-1),false);
 h.controller.status({discover:true});h.controller.releaseDiscovery();assert.equal(h.activation.at(-1),false);h.controller.close();assert.equal(h.host.closed,true);
});
test('configured steering survives discovery release; samples expire and detach clears them',t=>{
 t.mock.timers.enable({apis:['setTimeout','Date'],now:10000});const h=setup();h.controller.configure(settings,true);assert.equal(h.calls.at(-1).enabled,true);assert.equal(h.activation.at(-1),true);
 h.report({available:true,devices:[{id,name:'Wheel',axes:['x','rz']}]});h.sample({deviceId:id,axes:{x:1},sampledAt:10000});
 assert.deepEqual(h.controller.status().samples,{[id]:{x:1}});h.controller.status({discover:true});h.controller.releaseDiscovery();assert.equal(h.activation.at(-1),true);
 t.mock.timers.tick(251);assert.deepEqual(h.controller.status().samples,{});h.sample({deviceId:id,axes:{x:-1},sampledAt:10251});
 h.report({devices:[]});assert.deepEqual(h.controller.status().samples,{});assert.match(h.controller.status().error,/unavailable/i);
 h.report({available:true,devices:[{id,name:'Wheel',axes:['x']}]});h.sample({deviceId:id,axes:{x:0},sampledAt:10251});assert.equal(h.controller.status().error,'');
 assert.equal(h.controller.status().samples[id].x,0);h.controller.configure(settings,false);assert.equal(h.calls.at(-1).enabled,false);assert.equal(h.activation.at(-1),false);h.controller.close();
});
test('unassigned and invalid settings stay neutral; closed controller cannot restart discovery',()=>{
 const h=setup();h.controller.configure({...settings,deviceId:''},true);assert.equal(h.calls.at(-1).enabled,false);assert.equal(h.controller.status().error,'');
 h.controller.configure({...settings,lockDegrees:Infinity,axis:'bogus'},true);assert.equal(h.calls.at(-1).lockDegrees,900);assert.equal(h.calls.at(-1).axis,'x');
 h.controller.close();const length=h.activation.length;h.controller.status({discover:true});assert.equal(h.activation.length,length);
});
