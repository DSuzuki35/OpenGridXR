import test from 'node:test';
import assert from 'node:assert/strict';
import * as spatial from '../src/spatial-overlays.mjs';
import {createProfile, normalizeProfile, validateProfile} from '../src/model.mjs';
import {listToggleActions,validateBindings,pruneSpatialBindings,toggleProfileAction} from '../src/input-bindings.mjs';
const menu=()=>spatial.createQuickMenuOverlay();
const binding=(id,index=0)=>({actionId:id,deviceId:'dinput:{01234567-89ab-cdef-0123-456789abcdef}',deviceName:'Fixture wheel',kind:'button',index});
test('menu defaults start hidden and use a local private low-rate head-pointed panel',()=>{
  assert.equal(typeof spatial.createQuickMenuOverlay,'function');
  const item=menu();
  assert.deepEqual([item.kind,item.enabled,item.anchor,item.interaction,item.showInStream,item.width,item.height,item.fps,item.x,item.y,item.z,item.widthMeters,item.shortcut,item.selectShortcut],['menu',false,'local','head',false,1024,768,15,0,0,-1,.9,'Ctrl+Alt+F5','Ctrl+Alt+F4']);
  spatial.validateSpatialOverlays({...spatial.spatialDefaults(),items:[item]});
});
test('menu rejects duplicate, stream, head anchor, invalid selector, network and high rate configuration',()=>{
  for(const patch of [{showInStream:true},{anchor:'head'},{interaction:'mouse'},{fps:16},{selectShortcut:'Ctrl+A'},{selectShortcut:9},{url:'https://example.com'},{windowId:'window:3:0'},{displayId:'1'}])assert.throws(()=>spatial.validateSpatialOverlays({...spatial.spatialDefaults(),items:[{...menu(),...patch}]}),JSON.stringify(patch));
  assert.throws(()=>spatial.validateSpatialOverlays({...spatial.spatialDefaults(),items:[menu(),menu()]}));
  spatial.validateSpatialOverlays({...spatial.spatialDefaults(),items:[{...menu(),shortcut:'',selectShortcut:''}]});
});
test('menu shortcut validation includes all toolkit, centering, source and precision shortcuts',()=>{
  assert.equal(typeof spatial.validateSpatialShortcuts,'function');
  const p=createProfile(),item=menu(); p.spatialOverlays.items=[item]; validateProfile(p);
  for(const target of [p.framegen.toggleShortcut,p.overlay.toggleShortcut,p.overlay.cycleShortcut,'Ctrl+F1','Ctrl+F2','Ctrl+F3',item.shortcut]){
    const copy=structuredClone(p);copy.centering.gameShortcut='Ctrl+F1';copy.spatialOverlays.items.push({...spatial.createSpatialOverlay(),kind:'desktop',shortcut:'Ctrl+F2',precisionShortcut:'Ctrl+F3',interaction:'head'});copy.spatialOverlays.items[0].selectShortcut=target;
    assert.throws(()=>validateProfile(copy),/shortcut|conflict/i,target);
  }
});
test('menu normalization preserves unbound keys and enforces private local source settings',()=>{
  const p=createProfile();p.spatialOverlays.items=[{...menu(),anchor:'head',interaction:'mouse',showInStream:true,fps:30,url:'https://example.com',shortcut:'',selectShortcut:''}];
  const item=normalizeProfile(p).spatialOverlays.items[0];
  assert.deepEqual([item.anchor,item.interaction,item.showInStream,item.fps,item.url,item.shortcut,item.selectShortcut],['local','head',false,15,'','','']);
});
test('menu controller selection is transient and prunes all incompatible desktop and broadcast bindings',()=>{
  const p=createProfile(),item=menu();p.spatialOverlays.items=[item];const id=`spatialOverlays.${item.id}.leftClick`;
  const action=listToggleActions(p).find(a=>a.id===id);assert.equal(action?.transient,true);assert.match(action.label,/select/i);assert.equal(action.disabled,false);
  validateBindings({enabled:true,bindings:[binding(id)]},p);assert.throws(()=>toggleProfileAction(p,id),/unavailable/);
  for(const name of ['rightClick','resume','precision','showInStream'])assert.throws(()=>validateBindings({enabled:true,bindings:[binding(`spatialOverlays.${item.id}.${name}`)]},p));
  p.inputBindings.bindings=['leftClick','rightClick','resume','precision','enabled','showInStream'].map((name,i)=>binding(`spatialOverlays.${item.id}.${name}`,i));
  assert.deepEqual(pruneSpatialBindings(p).inputBindings.bindings.map(b=>b.actionId),[id,`spatialOverlays.${item.id}.enabled`]);
  item.kind='url';assert.deepEqual(pruneSpatialBindings(p).inputBindings.bindings.map(b=>b.actionId),[`spatialOverlays.${item.id}.enabled`,`spatialOverlays.${item.id}.showInStream`]);
});
