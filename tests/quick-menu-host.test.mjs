import test from 'node:test';import assert from 'node:assert/strict';import {EventEmitter} from 'node:events';import {PassThrough} from 'node:stream';import {createRequire} from 'node:module';
const {createOverlayHost}=createRequire(import.meta.url)('../desktop/overlay-host.cjs');
test('native UI pointer events retain exact identities, reject malformed events and stop after host close',()=>{
 const child=new EventEmitter();for(const key of ['stdin','stdout','stderr'])child[key]=new PassThrough();child.kill=()=>{};
 const events=[];const host=createOverlayHost({executable:'fixture',game:'lmu',spawnProcess:()=>child,onUiPointer:event=>events.push(event)});host.start();
 const event={op:'ui-pointer',slot:0,event:'click',u:.5,v:.5,uiToken:'11111111-1111-4111-8111-111111111111',session:'18446744073709551614',focusEpoch:'2',configRevision:'3',sourceRevision:'4',sourceGeneration:'5',recenter:'6',tickMs:'7000',sequence:'1'};
 const send=e=>child.stdout.write(JSON.stringify(e)+'\n');
 send(event);assert.deepEqual(events,[event]);send({...event,slot:15});send({...event,u:2});send({...event,session:123});assert.equal(events.length,1);
 host.close();send(event);assert.equal(events.length,1);child.emit('close');
});
