const {_electron}=require('playwright-core');const fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict');
(async()=>{
 const directory=await fs.mkdtemp(path.join(os.tmpdir(),'opengrid-menu-integration-'));
 const env={...process.env,OPENGRIDXR_DATA_DIR:directory,OPENGRIDXR_TEST_MODE:'1'};delete env.ELECTRON_RUN_AS_NODE;
 let app;
 try{
  app=await _electron.launch({executablePath:process.env.OPENGRIDXR_TEST_EXE||require('electron'),args:process.env.OPENGRIDXR_TEST_EXE?[]:[path.resolve('.')],env,timeout:60000});await app.firstWindow();
  await app.evaluate(async electron=>{
   const root=electron.app.getAppPath(),load=process.getBuiltinModule('module').createRequire(root+'/desktop/main.cjs');
   const model=load(root+'/src/model.mjs'),{createQuickMenuOverlay}=load(root+'/src/spatial-overlays.mjs');
   const doc=model.createDocument(),profile=doc.profiles[0],item={...createQuickMenuOverlay(),enabled:true};
   profile.spatialOverlays={...profile.spatialOverlays,enabled:true,items:[item]};
   global.menuFixture={doc,item,applied:[],commands:[],sequence:0,frames:0};const fixture=global.menuFixture;
   fixture.controller=await load(root+'/desktop/spatial-overlays.cjs').createSpatialOverlays({electron,executable:'fixture',model,
    getSessions:async()=>[{game:'iracing',active:true,engineLoaded:true}],
    getMenuState:()=>({profileName:'Demo profile',game:'iRacing',busy:false,message:'Ready',actions:[{id:'overlay.enabled',label:'Toggle performance HUD',disabled:false},{id:'close',label:'Close menu',disabled:false}]}),
    onMenuAction:async({actionId,canApply})=>{if(!canApply())throw Error('Stale source');fixture.applied.push(actionId);if(actionId==='close'){item.enabled=false;fixture.controller.update(doc);}return {message:'Applied '+actionId};},
    hostFactory:options=>{fixture.nativePointer=options.onUiPointer;return{start(){},running:()=>true,close(){},state:()=>({available:true,capacity:15,slots:[{slot:0,state:1,frameId:1}]}),send:command=>{fixture.commands.push(command);return true;},submit:(_,texture)=>{fixture.frames++;texture.release();return true;}};},
   });
   electron.ipcMain.removeHandler('opengrid:quickMenuAction');electron.ipcMain.handle('opengrid:quickMenuAction',(event,id)=>fixture.controller.menuAction(event,id));
   fixture.controller.update(doc);
  });
  let page;const deadline=Date.now()+12000;
  while(Date.now()<deadline){page=app.context().pages().find(p=>p.url().includes('quick-menu.html'));if(page)break;await new Promise(r=>setTimeout(r,50));}
  assert.ok(page,'Offscreen built-in menu source loaded');page.setDefaultTimeout(4000);
  await page.getByRole('button',{name:'Toggle performance HUD',exact:true}).waitFor();
  await page.waitForFunction(()=>!document.querySelector('[data-action="overlay.enabled"]').disabled);
  assert.equal(await page.evaluate(()=>typeof window.openGrid),'undefined');assert.equal(await page.evaluate(()=>typeof process),'undefined');
  assert.deepEqual(await page.evaluate(()=>Object.keys(window.openGridMenu).sort()),['onState','runAction']);
  await assert.rejects(page.evaluate(()=>window.openGridMenu.runAction('overlay.enabled')),/fresh|click|select/i);
  const send=async(event,button,patch={})=>{
   const bounds=await page.getByRole('button',{name:button,exact:true}).boundingBox();
   return app.evaluate((_,{event,bounds,patch})=>{const f=global.menuFixture,c=f.commands.find(c=>c.op==='configure');f.nativePointer({op:'ui-pointer',slot:0,event,uiToken:c.uiToken,u:(bounds.x+bounds.width/2)/1024,v:(bounds.y+bounds.height/2)/768,session:'10',focusEpoch:'2',configRevision:'3',sourceRevision:'4',sourceGeneration:'5',recenter:'6',tickMs:String(Math.floor(process.getBuiltinModule('os').uptime()*1000)),sequence:String(++f.sequence),...patch});},{event,bounds,patch});
  };
  await send('move','Toggle performance HUD');await page.waitForFunction(()=>document.querySelector('[data-action="overlay.enabled"]').matches(':hover'));
  await send('click','Toggle performance HUD');await page.waitForFunction(()=>document.querySelector('[role="status"]').textContent.includes('Applied'));
  assert.deepEqual(await app.evaluate(()=>global.menuFixture.applied),['overlay.enabled']);
  await send('click','Toggle performance HUD',{uiToken:'22222222-2222-4222-8222-222222222222'});await new Promise(r=>setTimeout(r,50));
  assert.deepEqual(await app.evaluate(()=>global.menuFixture.applied),['overlay.enabled']);
  await send('leave','Toggle performance HUD');await page.waitForFunction(()=>document.getElementById('head-cursor').hidden);
  await assert.rejects(page.evaluate(()=>window.openGridMenu.runAction('overlay.enabled')),/fresh|click|select/i);
  await send('click','Close menu');await page.waitForEvent('close').catch(()=>{});
  assert.deepEqual(await app.evaluate(()=>global.menuFixture.applied),['overlay.enabled','close']);
  assert.ok(await app.evaluate(()=>global.menuFixture.frames)>0,'Actual shared GPU textures were painted');
  console.log('PASS: packaged-capable offscreen GPU menu native hover/select -> isolated preload -> authorized action; foreign/stale clicks blocked; close disposes source.');
 }finally{if(app){await app.evaluate(({BrowserWindow})=>{global.menuFixture?.controller?.close();for(const w of BrowserWindow.getAllWindows())w.destroy();}).catch(()=>{});await app.close();}await fs.rm(directory,{recursive:true,force:true});}
})().catch(error=>{console.error(error);process.exitCode=1;});
