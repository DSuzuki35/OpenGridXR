import {test} from 'node:test';
import assert from 'node:assert/strict';
import {setImmediate as tick} from 'node:timers/promises';
import {createRequire} from 'node:module';
import {EventEmitter} from 'node:events';
import {createDocument,validateDocument} from '../src/model.mjs';
import {createSpatialOverlay} from '../src/spatial-overlays.mjs';
const {createSpatialOverlays} = createRequire(import.meta.url)('../desktop/spatial-overlays.cjs');

test('disabling saved spatial settings during window lookup stops capture without another timer tick',async()=>{
  let finishLookup, deferLookup=false, activeTimers=0;
  const windows=[],hosts=[];
  const selectedWindow={id:'window:123:0',name:'Fixture window'};
  class Window {
    static getAllWindows(){return [];}
    constructor(){
      this.webContents=new EventEmitter();
      for(const method of ['setAudioMuted','setFrameRate','setWindowOpenHandler']) this.webContents[method]=()=>{};
      windows.push(this);
    }
    setBounds(){} loadFile(){return Promise.resolve();} loadURL(){return Promise.resolve();}
    isDestroyed(){return !!this.destroyed;} destroy(){this.destroyed=true;}
  }
  const captureSession={setPermissionRequestHandler(){},setPermissionCheckHandler(){},removeAllListeners(){},on(){},setDisplayMediaRequestHandler(){}};
  const controller=await createSpatialOverlays({
    electron:{BrowserWindow:Window,session:{fromPartition:()=>captureSession},desktopCapturer:{getSources:()=>deferLookup?new Promise(resolve=>{finishLookup=resolve;}):Promise.resolve([selectedWindow])}},
    model:{validateDocument},getSessions:async()=>[{game:'iracing',active:true,engineLoaded:true}],
    setTimer:()=>{activeTimers++;return 1;},clearTimer:()=>{activeTimers--;},
    hostFactory:()=>{
      const host={start(){},running:()=>true,send(){},close(){this.stopped=true;},state:()=>({available:true,capacity:2})};
      hosts.push(host);return host;
    },
  });
  try {
    const doc=createDocument(),item={...createSpatialOverlay(),kind:'window',windowId:selectedWindow.id,enabled:true};
    doc.profiles[0].spatialOverlays={...doc.profiles[0].spatialOverlays,enabled:true,items:[item,{...createSpatialOverlay(),enabled:true,url:'https://example.com/'}]};
    await controller.selectWindow('iracing',item.id,selectedWindow.id);
    deferLookup=true;controller.update(doc);await tick();
    assert.equal(hosts.length,1);assert.equal(typeof finishLookup,'function');
    doc.profiles[0].spatialOverlays.enabled=false;controller.update(doc);
    assert.equal(activeTimers,0);
    assert.ok(hosts.every(host=>host.stopped),'disable stops the helper even while window enumeration is unresolved');
    finishLookup([selectedWindow]);await tick();await tick();
    assert.equal(windows.length,0,'neither the pending window nor the next stale URL source starts');
    assert.equal(controller.status('iracing').running,false);
  } finally {controller.close();}
});

test('closing spatial overlays while session lookup is pending cannot start a late host',async()=>{
  let finish, started=0;
  const controller = await createSpatialOverlays({
    electron:{BrowserWindow:{getAllWindows:()=>[]}}, model:{validateDocument},
    getSessions:()=>new Promise(resolve=>{finish=resolve;}),
    hostFactory:()=>{started++; throw new Error('No host should start after close');},
  });
  const doc=createDocument();
  doc.profiles[0].spatialOverlays={...doc.profiles[0].spatialOverlays,enabled:true,items:[{...createSpatialOverlay(),enabled:true,url:'https://example.com/'}]};
  controller.update(doc);
  controller.close();
  finish([{game:'iracing',active:true,engineLoaded:true}]);
  await tick();
  assert.equal(started,0);
});

test('a newer enable is reconciled after pending work without waiting for a polling tick',async()=>{
  let finishLookup, lookups=0, started=0;
  const controller=await createSpatialOverlays({
    electron:{BrowserWindow:{getAllWindows:()=>[]}},model:{validateDocument},
    getSessions:()=>++lookups===1?new Promise(resolve=>{finishLookup=resolve;}):Promise.resolve([{game:'iracing',active:true,engineLoaded:true}]),
    setTimer:()=>1,clearTimer:()=>{},
    hostFactory:()=>({start(){started++;},running:()=>true,state:()=>({available:false}),close(){}}),
  });
  try {
    const doc=createDocument();
    doc.profiles[0].spatialOverlays={...doc.profiles[0].spatialOverlays,enabled:true,items:[{...createSpatialOverlay(),enabled:true,url:'https://example.com/'}]};
    controller.update(doc);
    doc.profiles[0].spatialOverlays.enabled=false;controller.update(doc);
    doc.profiles[0].spatialOverlays.enabled=true;controller.update(doc);
    finishLookup([]);await tick();await tick();
    assert.equal(started,1,'the queued enable refreshes session state and starts the current helper');
    assert.equal(controller.status('iracing').running,true);
  } finally {controller.close();}
});

test('spatial session polling exists only while saved sources are enabled',async()=>{
  let activeTimers=0;
  const controller = await createSpatialOverlays({
    electron:{BrowserWindow:{getAllWindows:()=>[]}}, model:{validateDocument}, getSessions:async()=>[],
    setTimer:()=>{activeTimers++;return 1;},clearTimer:()=>{activeTimers--;},
  });
  try {
    assert.equal(activeTimers,0);
    const doc=createDocument();
    controller.update(doc); assert.equal(activeTimers,0);
    doc.profiles[0].spatialOverlays={...doc.profiles[0].spatialOverlays,enabled:true,items:[{...createSpatialOverlay(),enabled:true,url:'https://example.com/'}]};
    controller.update(doc); assert.equal(activeTimers,1);
    await tick();
    doc.profiles[0].spatialOverlays.enabled=false;
    controller.update(doc); assert.equal(activeTimers,0);
    doc.profiles[0].spatialOverlays.enabled=true;
    controller.update(doc); assert.equal(activeTimers,1);
  } finally {controller.close();}
  assert.equal(activeTimers,0);
});
