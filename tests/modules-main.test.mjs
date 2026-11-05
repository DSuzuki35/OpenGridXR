import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import {EventEmitter} from 'node:events';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import {moduleDefaults} from '../src/modules.mjs';

async function fixture(t, options={}) {
  const dir=await fs.mkdtemp(path.join(os.tmpdir(),'opengrid-module-main-'));
  t.after(()=>fs.rm(dir,{recursive:true,force:true}));
  const enabled=Object.fromEntries(Object.keys(moduleDefaults()).map(id=>[id,false]));
  await fs.writeFile(path.join(dir,'module-settings.json'),JSON.stringify({version:1,enabled}));
  const app=new EventEmitter();
  Object.assign(app,{isPackaged:false,requestSingleInstanceLock:()=>true,whenReady:()=>Promise.resolve(),setName(){},setPath(){},getPath:()=>dir,getVersion:()=> '1.0.0',quit(){}});
  let ready, failed, window, created=0, stopped=0, applyBinding;
  const centers=[];
  const initialized=new Promise((resolve,reject)=>{ready=resolve;failed=reject;});
  const handlers=new Map(), imported=[];
  class BrowserWindow extends EventEmitter {
    constructor(){super();window=this;this.webContents=new EventEmitter();Object.assign(this.webContents,{mainFrame:{},isDestroyed:()=>false,setWindowOpenHandler(){},send(){}});}
    isDestroyed(){return false;} async loadFile(){ready();}
  }
  const electron={app,BrowserWindow,ipcMain:{handle:(name,fn)=>handlers.set(name,fn)},dialog:{showErrorBox:(_title,error)=>failed(new Error(error))},shell:{},screen:{getAllDisplays:()=>[]}};
  const source=new URL('../desktop/main.cjs',import.meta.url), require=createRequire(source);
  const localRequire=name=>{
    imported.push(name);
    if(name==='electron')return electron;
    if(name==='electron-updater')return {autoUpdater:Object.assign(new EventEmitter(),{setFeedURL(){}})};
    if(name==='./telemetry.cjs')return {createTelemetry:()=>{created++;return {status:async()=>({running:true}),close:()=>{stopped++;}};}};
    if(options.bindings && name==='./runtime.cjs')return {createRuntime:args=>require(name).createRuntime({...args,testMode:true})};
    if(options.bindings && name==='./game-control.cjs')return {createGameControl:()=>({center:(...args)=>{centers.push(args);return {message:'fixture'};},cancelIfInvalid(){},close(){}})};
    if(options.bindings && name==='./input-controller.cjs')return {createInputController:args=>{applyBinding=args.applyAction;return require(name).createInputController(args);}};
    if(options.bindings && name==='./input-host.cjs')return {createInputHost:()=>({setActive(){},state:()=>({running:true,devices:[]}),close(){}})};
    return require(name);
  };
  new Function('require','__dirname','process',await fs.readFile(source,'utf8'))(localRequire,path.dirname(fileURLToPath(source)),{...process,env:{...process.env,OPENGRIDXR_TEST_MODE:options.bindings && !options.testMode?'0':'1',OPENGRIDXR_DATA_DIR:dir}});
  await initialized;
  t.after(()=>{app.emit('before-quit',{preventDefault(){}});app.emit('will-quit');});
  return {dir,imported,centers,applyBinding:(...args)=>applyBinding(...args),hasHandler:name=>handlers.has(`opengrid:${name}`),counts:()=>({created,stopped}),invoke:(name,...args)=>{
    const handler=handlers.get(`opengrid:${name}`); assert.ok(handler,`IPC ${name} is registered`);
    return handler({sender:window.webContents,senderFrame:window.webContents.mainFrame},...args);
  }};
}

test('centering controller action uses saved game shortcut without saving a toggle and rejects stale editor state', async t=>{
  const f=await fixture(t,{bindings:true});
  const state=await f.invoke('load'),p=state.document.profiles[0];
  p.game='lmu';p.centering={gameShortcut:'Ctrl+F1',delaySeconds:2};
  p.inputBindings.bindings=[{actionId:'centering.game',deviceId:'dinput:{00000000-0000-0000-0000-000000000001}',deviceName:'Wheel',kind:'pov',index:0,direction:4}];
  await f.invoke('save',state.document,state.revision);
  await f.invoke('inputEditorState',{dirty:false,busy:false,profileId:p.id});
  const settings=await f.invoke('getModuleSettings');settings.enabled.bindings=true;
  await f.invoke('setModuleSettings',settings);
  const before=await f.invoke('load');
  await f.applyBinding(p.id,'centering.game');
  assert.equal(f.centers.length,1);
  assert.deepEqual(f.centers[0].slice(0,2),['lmu',{gameShortcut:'Ctrl+F1',delaySeconds:2}]);
  assert.equal(f.centers[0][2].canApply(),true);
  const after=await f.invoke('load');assert.deepEqual(after.document,before.document);assert.equal(after.revision,before.revision);
  await f.invoke('inputEditorState',{dirty:true,busy:false,profileId:p.id});
  assert.equal(f.centers[0][2].canApply(),false);
  await assert.rejects(f.applyBinding(p.id,'centering.game'),/save|discard/i);
  await f.invoke('inputEditorState',{dirty:false,busy:false,profileId:'other-profile'});
  await assert.rejects(f.applyBinding(p.id,'centering.game'),/save|discard|profile/i);
  assert.equal(f.centers.length,1);
});

test('desktop starts without optional services and enforces module gates at IPC boundaries',async t=>{
  const {invoke,imported}=await fixture(t);
  for(const name of ['telemetry','spatial-overlays','benchmark-controller','input-controller','helmet-assets']) assert.ok(!imported.includes(`./${name}.cjs`),`${name} should remain unloaded`);
  assert.equal((await invoke('getModuleSettings')).enabled.stream,false);
  for(const [name,args] of [['launchOutput',[]],['benchmarkStart',[]],['inputCaptureStart',['overlay.enabled','profile']],['listOverlayWindows',[]]]) await assert.rejects(invoke(name,...args),/disabled/i);
  await assert.rejects(invoke('setModuleSettings',{version:1,enabled:moduleDefaults()}),/edits|save|busy/i);
});
test('retired iRacing INI management has no registered IPC or loaded backend',async t=>{
 const {hasHandler,imported}=await fixture(t);
 for(const name of ['nativeSettings','chooseNativeSettings','previewNativeSettings','applyNativeSettings','restoreNativeSettings','openNativeBackups'])assert.equal(hasHandler(name),false,name);
 assert.equal(imported.includes('./native-settings.cjs'),false);
 assert.equal(hasHandler('previewLmuLaunch'),true);
});

test('desktop disables running telemetry immediately and reopens it only on demand',async t=>{
  const {invoke,counts,dir}=await fixture(t);
  const state=await invoke('load');
  state.document.profiles[0].telemetry.enabled=true;
  await invoke('save',state.document,state.revision);
  await invoke('inputEditorState',{dirty:false,busy:false,profileId:state.document.selectedProfileId});
  const settings=await invoke('getModuleSettings'); settings.enabled.telemetry=true;
  await invoke('setModuleSettings',settings);
  assert.deepEqual(counts(),{created:0,stopped:0});
  await invoke('telemetryStatus','iracing');
  assert.deepEqual(counts(),{created:1,stopped:0});
  settings.enabled.telemetry=false;
  await invoke('setModuleSettings',settings);
  assert.deepEqual(counts(),{created:1,stopped:1});
  assert.equal((await invoke('telemetryStatus','iracing')).running,false);
  settings.enabled.telemetry=true;
  await invoke('setModuleSettings',settings);
  await invoke('telemetryStatus','iracing');
  assert.deepEqual(counts(),{created:2,stopped:1});
  const profiles=JSON.parse(await fs.readFile(path.join(dir,'profiles.json'),'utf8'));
  assert.equal(profiles.profiles[0].telemetry.enabled,true);
});
