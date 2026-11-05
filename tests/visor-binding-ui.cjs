const {_electron}=require('playwright-core'),fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict');
(async()=>{
 const root=path.resolve('.'),directory=await fs.mkdtemp(path.join(os.tmpdir(),'opengrid-visor-binding-'));
 const {createDocument}=await import(require('node:url').pathToFileURL(path.join(root,'src/model.mjs')));
 const document=createDocument();document.profiles[0].helmet.enabled=true;document.profiles[0].helmet.visorHotkey=true;
 await fs.writeFile(path.join(directory,'profiles.json'),JSON.stringify(document));
 const env={...process.env,OPENGRIDXR_DATA_DIR:directory,OPENGRIDXR_TEST_MODE:'1',OPENGRIDXR_INPUT_TEST_FIXTURE:'1'};delete env.ELECTRON_RUN_AS_NODE;let app;
 try{
  app=await _electron.launch({executablePath:process.env.OPENGRIDXR_TEST_EXE||require('electron'),args:process.env.OPENGRIDXR_TEST_EXE?[]:[root],env,timeout:60000});
  const page=await app.firstWindow();page.setDefaultTimeout(15000);
  await app.evaluate((_,directory)=>{
   const fs=process.getBuiltinModule('node:fs'),path=process.getBuiltinModule('node:path'),root=path.join(directory,'runtime','enhancements-data','runtime');
   const session=process.pid+'-'+Date.now()+'-visor-fixture',statusFile=path.join(root,'status',session+'.json'),commandFile=path.join(root,'control',session+'.json');
   fs.mkdirSync(path.dirname(statusFile),{recursive:true});globalThis.visorCommands=[];let ack=0;
   const tick=()=>{
    try{const request=JSON.parse(fs.readFileSync(commandFile));if(request.revision>ack){globalThis.visorCommands.push(request);ack=request.revision;}}catch{}
    try{fs.writeFileSync(statusFile+'.tmp',JSON.stringify({protocolVersion:1,sessionId:session,processId:process.pid,application:'iRacingSim64DX11.exe',updatedAtUnixMilliseconds:Date.now(),acknowledgedRevision:ack,capabilities:{drivingControls:true,visorCycle:true}}));fs.renameSync(statusFile+'.tmp',statusFile);}catch{}
   };tick();globalThis.visorFixtureTimer=setInterval(tick,20);
  },directory);
  await page.getByRole('button',{name:'Helmet',exact:true}).click();
  await page.getByRole('button',{name:'Bind controller: Cycle visor position',exact:true}).click();
  await page.waitForFunction(async()=>(await window.openGrid.inputCaptureStatus()).state==='listening');
  const press=()=>app.evaluate(()=>globalThis.opengridInputFixturePress({deviceId:'dinput:{01234567-89ab-cdef-0123-456789abcdef}',kind:'button',index:12}));
  await press();await page.getByText('Binding added to your draft. Save the profile to use it while driving.',{exact:true}).waitFor();
  await page.getByRole('button',{name:'Done',exact:true}).click();await page.getByRole('button',{name:'Save profile',exact:true}).click();await page.getByRole('button',{name:'Profile saved',exact:true}).waitFor();
  const saved=await page.evaluate(()=>window.openGrid.load());assert.equal(saved.document.profiles[0].inputBindings.bindings[0].actionId,'helmet.visorCycle');
  await press();
  const deadline=Date.now()+5000;while(await app.evaluate(()=>globalThis.visorCommands.length)<1&&Date.now()<deadline)await new Promise(resolve=>setTimeout(resolve,25));
  assert.equal(await app.evaluate(()=>globalThis.visorCommands.length),1);
  assert.equal((await app.evaluate(()=>globalThis.visorCommands[0])).drivingAction,'helmet.visor-cycle');
  await page.waitForFunction(async()=>!(await window.openGrid.inputStatus()).paused);
  const after=await page.evaluate(()=>window.openGrid.load());assert.equal(after.revision,saved.revision);assert.equal(after.document.profiles[0].helmet.visorPosition,saved.document.profiles[0].helmet.visorPosition);
  await page.reload();await page.getByRole('button',{name:'Helmet',exact:true}).click();
  assert.match(await page.getByRole('button',{name:'Bind controller: Cycle visor position',exact:true}).getAttribute('title'),/Button 13/);
  await page.getByLabel('Sunstrip text',{exact:true}).fill('UNSAVED');await page.waitForFunction(async()=>(await window.openGrid.inputStatus()).paused);
  await press();await new Promise(resolve=>setTimeout(resolve,100));assert.equal(await app.evaluate(()=>globalThis.visorCommands.length),1);
  console.log('PASS: helmet visor binding learns, saves/reloads, dispatches a session command without changing startup state, and pauses for unsaved edits.');
 }finally{if(app){await app.evaluate(({BrowserWindow})=>{clearInterval(globalThis.visorFixtureTimer);BrowserWindow.getAllWindows().forEach(w=>w.destroy());}).catch(()=>{});await app.close();}await fs.rm(directory,{recursive:true,force:true});}
})().catch(error=>{console.error(error);process.exitCode=1;});
