import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url);
let createDesktopShortcuts;
try{({createDesktopShortcuts}=require('../desktop/desktop-shortcuts.cjs'));}catch{}
function fixture(){
  const callbacks=new Map(),unregistered=[];
  let editor={dirty:false,busy:false,profileId:'profile'},fail=false;
  const applied=[];
  const service=createDesktopShortcuts({globalShortcut:{register:(key,callback)=>{if(fail)return false;callbacks.set(key,callback);return true;},unregister:key=>{unregistered.push(key);callbacks.delete(key);}},editorState:()=>editor,applyAction:async(...args)=>applied.push(args)});
  const profile={id:'profile',spatialOverlays:{enabled:true,items:[{id:'item',kind:'desktop',shortcut:'Ctrl+Alt+F8',enabled:false}]}};
  return {service,callbacks,unregistered,applied,profile,setEditor:value=>editor=value,fail:()=>fail=true};
}
test('desktop shortcut can enable a hidden desktop and ignores dirty/busy/stale editor state',async()=>{
  assert.equal(typeof createDesktopShortcuts,'function');const f=fixture();f.service.update(f.profile,{enabled:true});
  f.callbacks.get('Ctrl+Alt+F8')();await new Promise(setImmediate);
  assert.deepEqual(f.applied,[['profile','spatialOverlays.item.enabled']]);
  for(const editor of [{dirty:true,profileId:'profile'},{busy:true,profileId:'profile'},{profileId:'different'}]){f.setEditor(editor);f.callbacks.get('Ctrl+Alt+F8')();await new Promise(setImmediate);}
  assert.equal(f.applied.length,1);f.service.close();
});
test('duplicate/unavailable shortcuts report conflicts; disabling unregisters only owned shortcuts',()=>{
  assert.equal(typeof createDesktopShortcuts,'function');const f=fixture();
  f.profile.spatialOverlays.items.push({...f.profile.spatialOverlays.items[0],id:'other'});
  f.service.update(f.profile,{enabled:true});assert.equal(f.callbacks.size,0);assert.equal(f.service.status().errors.length,2);
  f.profile.spatialOverlays.items.pop();f.fail();f.service.update(f.profile,{enabled:true});assert.equal(f.service.status().errors.length,1);
  f.service.close();
});
test('profile switch invalidates old callbacks and releases registrations',async()=>{
  assert.equal(typeof createDesktopShortcuts,'function');const f=fixture();f.service.update(f.profile,{enabled:true});const old=f.callbacks.get('Ctrl+Alt+F8');
  f.service.update(null,{enabled:false});old();await new Promise(setImmediate);assert.equal(f.applied.length,0);assert.equal(f.callbacks.size,0);assert.deepEqual(f.unregistered,['Ctrl+Alt+F8']);
});
test('headset menu visibility and select shortcuts work independently, and hidden selection never turns into a toggle',async()=>{
  const f=fixture();f.profile.spatialOverlays.items=[{id:'menu',kind:'menu',enabled:false,shortcut:'Ctrl+Alt+F5',selectShortcut:'Ctrl+Alt+F4'}];
  f.service.update(f.profile);f.callbacks.get('Ctrl+Alt+F5')();await new Promise(setImmediate);
  f.callbacks.get('Ctrl+Alt+F4')();await new Promise(setImmediate);
  assert.deepEqual(f.applied,[['profile','spatialOverlays.menu.enabled'],['profile','spatialOverlays.menu.leftClick']]);f.service.close();
});
