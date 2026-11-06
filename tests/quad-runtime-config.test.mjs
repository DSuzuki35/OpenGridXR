import {test} from 'node:test';
import assert from 'node:assert/strict';
import {createProfile} from '../src/model.mjs';
import {toEnhancementsConfig,toRuntimeIni} from '../desktop/runtime-config.cjs';
import {sessionState} from '../desktop/runtime.cjs';

test('automatic runtime INI carries optimizations and separate machine CPU preference',()=>{
 const p=createProfile('lmu');p.lmuRendering={...p.lmuRendering,mode:'quad',cpuUploadReuse:true,cpuOuterReuse:true,outerSsr:true,outerSmoothing:3};
 const doc={profiles:[p],selectedProfileId:p.id};
 const ini=toRuntimeIni(doc,{lmuPreferences:{cpuCacheAffinity:false}});
 for(const line of ['LmuCpuUploadReuse=1','LmuCpuUploadBatch=0','LmuCpuOuterReuse=1','LmuOuterSsr=1','LmuCpuCacheAffinity=0','LmuSmoothing=0'])assert.ok(ini.includes(line),line);
 assert.match(toRuntimeIni(doc),/LmuCpuCacheAffinity=0/);
 assert.match(toRuntimeIni(doc,{lmuPreferences:{cpuCacheAffinity:true}}),/LmuCpuCacheAffinity=1/);
 assert.equal(p.lmuRendering.cpuCacheAffinity,undefined);
});

test('embedded QuadView is selected only for LMU VectorXR and forces engine availability',()=>{
 const p=createProfile('lmu'); p.framegen.enabled=false;p.overlay.enabled=false;p.helmet.enabled=false;p.spatialOverlays.enabled=false;
 p.lmuRendering={mode:'quad',provider:'vectorxr',gaze:'fixed',peripheralScale:25,focusScale:250,focusWidth:46,focusHeight:47};
 const doc={profiles:[p],selectedProfileId:p.id};
 let c=toEnhancementsConfig(doc,'lmu');
 assert.equal(c.modules.quadviews.enabled,true);assert.equal(c.core.enabled,true);
 assert.equal(c.modules.quadviews.defaults.peripheralScale,.25);assert.equal(c.modules.quadviews.defaults.focusScale,2.5);
 assert.equal(c.modules.quadviews.defaults.focusHorizontalSizePercent,46);assert.equal(c.modules.quadviews.defaults.focusVerticalSizePercent,47);
 assert.equal(c.modules.quadviews.defaults.trackingMode,'head');
 assert.match(toRuntimeIni(doc),/EnhancementsEnabled=1/);
 for(const provider of ['native','qvfr']) {p.lmuRendering.provider=provider;assert.equal(toEnhancementsConfig(doc,'lmu').modules.quadviews.enabled,false);}
 p.lmuRendering.provider='vectorxr';p.lmuRendering.mode='stereoVrs';assert.equal(toEnhancementsConfig(doc,'lmu').modules.quadviews.enabled,false);
 p.lmuRendering.mode='quad';p.game='iracing';assert.equal(toEnhancementsConfig(doc,'iracing').modules.quadviews.enabled,false);
});

test('rendered quad FOV status is live, bounded, mode-specific and not inferred from configuration',()=>{
 const q={mode:'quad',provider:'qvfr',hookStatus:'ready',frameId:14,focusFovValid:true,focusLeftHorizontal:45,focusLeftVertical:43,focusRightHorizontal:46,focusRightVertical:44,outerSizes:[{width:806,height:754},{width:806,height:754}],focusSizes:[{width:3578,height:3540},{width:3578,height:3540}],captureState:'ready',streamingExpansionUnsupported:false,renderedFov:Array.from({length:4},()=>({angleLeft:-.7,angleRight:.7,angleUp:.6,angleDown:-.6}))};
 const s={game:'lmu',pid:42,active:true,updatedUnixMs:1000,lmuRendering:q};
 assert.deepEqual(sessionState(s,1001,()=>true).lmuRendering,q);
 for(const captureState of ['ready-focus','ready-outer-only']) assert.equal(sessionState({...s,lmuRendering:{...q,captureState}},1001,()=>true).lmuRendering.captureState,captureState);
 assert.equal(sessionState(s,12000,()=>true).lmuRendering,null);
 assert.equal(sessionState(s,1001,()=>false).lmuRendering,null);
 assert.equal(sessionState({...s,game:'iracing'},1001,()=>true).lmuRendering,null);
 assert.equal(sessionState({...s,lmuRendering:{...q,focusLeftHorizontal:Infinity}},1001,()=>true).lmuRendering,null);
 assert.equal(sessionState({...s,lmuRendering:{...q,renderedFov:[]}},1001,()=>true).lmuRendering,null);
 const vrs={...q,mode:'stereoVrs',focusFovValid:false,focusSizes:[{width:0,height:0},{width:0,height:0}],renderedFov:[]};
 assert.equal(sessionState({...s,lmuRendering:vrs},1001,()=>true).lmuRendering.focusFovValid,false);
});
