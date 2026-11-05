import test from 'node:test';import assert from 'node:assert/strict';import {EventEmitter} from 'node:events';import {createRequire} from 'node:module';import {setImmediate as tick} from 'node:timers/promises';
import {createDocument,validateDocument} from '../src/model.mjs';import {createQuickMenuOverlay} from '../src/spatial-overlays.mjs';
const {createSpatialOverlays}=createRequire(import.meta.url)('../desktop/spatial-overlays.cjs');
test('built-in menu has isolated preload/permissions, source-bound native input and immediate teardown on hide',async()=>{
 const windows=[],commands=[],applied=[];let pointerCallback;
 class Window{
  static getAllWindows(){return [];}
  constructor(options){this.options=options;this.destroyed=false;this.webContents=new EventEmitter();this.webContents.mainFrame={};
   for(const key of ['setAudioMuted','setFrameRate','setWindowOpenHandler','invalidate','sendInputEvent','send'])this.webContents[key]=()=>{};
   this.webContents.isDestroyed=()=>this.destroyed;windows.push(this);
  }
  setBounds(){}isDestroyed(){return this.destroyed;}destroy(){this.destroyed=true;}
  async loadFile(file){this.file=file;this.webContents.emit('did-finish-load');}
 }
 const permissions=[];
 const controller=await createSpatialOverlays({electron:{BrowserWindow:Window,desktopCapturer:{getSources:async()=>{throw Error('Menu must not enumerate desktop');}},session:{fromPartition:()=>({setPermissionRequestHandler(fn){fn(null,'display-capture',yes=>permissions.push(yes));},setPermissionCheckHandler(){},removeAllListeners(){},on(){},setDisplayMediaRequestHandler(){}})}},
  executable:'fixture',model:{validateDocument},getSessions:async()=>[{game:'iracing',active:true,engineLoaded:true}],setTimer:()=>1,clearTimer:()=>{},
  getMenuState:()=>({profileName:'Test',actions:[{id:'close',label:'Close',disabled:false}]}),onMenuAction:async({actionId,canApply})=>{assert.ok(canApply());applied.push(actionId);},
  hostFactory:options=>{pointerCallback=options.onUiPointer;return{start(){},running:()=>true,close(){},state:()=>({available:true,capacity:15,slots:[{slot:0,state:1,frameId:1}]}),send:command=>{commands.push(command);return true;},submit:()=>true};}});
 try{
  const doc=createDocument(),profile=doc.profiles[0],item={...createQuickMenuOverlay(),enabled:true};profile.spatialOverlays.enabled=true;profile.spatialOverlays.items=[item];controller.update(doc);
  for(let i=0;i<8&&!windows.length;i++)await tick();
  assert.equal(windows.length,1);const w=windows[0];assert.match(w.file,/quick-menu\.html$/);assert.match(w.options.webPreferences.preload,/quick-menu-preload\.cjs$/);
  assert.equal(w.options.webPreferences.sandbox,true);assert.deepEqual(permissions,[false]);
  w.webContents.emit('paint',{texture:{release(){}}});
  const config=commands.find(c=>c.op==='configure');assert.equal(config.desktopMode,2);assert.equal(config.showInStream,false);assert.ok(config.uiToken);
  const event={op:'ui-pointer',slot:0,event:'click',u:.5,v:.5,uiToken:config.uiToken,session:'10',focusEpoch:'2',configRevision:'3',sourceRevision:'4',sourceGeneration:'5',recenter:'6',tickMs:String(Math.floor((await import('node:os')).uptime()*1000)),sequence:'1'};
  pointerCallback(event);
  const request=controller.prepareMenuSelect('iracing',item.id);
  assert.throws(()=>controller.menuSelect('iracing',item.id,{...request,requestedAt:Date.now()-201}),/stale|changed/i);
  controller.menuSelect('iracing',item.id,request);assert.equal(commands.at(-1).session,'10');assert.equal(commands.at(-1).focusEpoch,'2');
  pointerCallback({...event,event:'move',sequence:'2',recenter:'7'});assert.throws(()=>controller.menuSelect('iracing',item.id,request),/stale|changed/);
  pointerCallback({...event,sequence:'3'});
  await controller.menuAction({sender:w.webContents,senderFrame:w.webContents.mainFrame},'close');assert.deepEqual(applied,['close']);
  item.enabled=false;controller.update(doc);assert.ok(w.destroyed);
  pointerCallback({...event,sequence:'4'});await assert.rejects(controller.menuAction({sender:w.webContents,senderFrame:w.webContents.mainFrame},'close'));
  assert.throws(()=>controller.menuSelect('iracing',item.id,request));
 }finally{controller.close();}
});
