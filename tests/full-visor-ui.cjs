const {_electron}=require('playwright-core');const fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict');
(async()=>{
 const dir=await fs.mkdtemp(path.join(os.tmpdir(),'opengrid-full-visor-ui-'));const env={...process.env,OPENGRIDXR_DATA_DIR:dir,OPENGRIDXR_TEST_MODE:'1'};delete env.ELECTRON_RUN_AS_NODE;let app;
 try{
  app=await _electron.launch({executablePath:process.env.OPENGRIDXR_TEST_EXE||require('electron'),args:process.env.OPENGRIDXR_TEST_EXE?[]:[path.resolve('.')],env,timeout:60000});
  await app.evaluate(({ipcMain})=>{global.templateCalls=[];ipcMain.removeHandler('opengrid:downloadVisorTemplate');ipcMain.handle('opengrid:downloadVisorTemplate',(_,request)=>{global.templateCalls.push(request);return {saved:true,fileName:'Fixture template.'+request.format};});ipcMain.removeHandler('opengrid:listSystemFonts');ipcMain.handle('opengrid:listSystemFonts',()=>({available:true,families:['Arial','Comic Sans MS','Segoe UI'],error:''}));});
  const page=await app.firstWindow();page.setDefaultTimeout(5000);
  await page.getByRole('button',{name:'Helmet',exact:true}).click();
  await page.getByRole('group',{name:'Helmet editor section'}).getByRole('button',{name:'Graphic',exact:true}).click();
  const area=page.getByRole('group',{name:'Graphic layout',exact:true});await area.getByRole('button',{name:'Custom',exact:true}).click();
  await page.getByRole('button',{name:'PNG template',exact:true}).click();await page.getByRole('button',{name:'PSD template',exact:true}).click();await page.getByRole('button',{name:'PNG guide',exact:true}).click();
  assert.deepEqual(await app.evaluate(()=>global.templateCalls),['png','psd','guide'].map(format=>({style:'formula',format})));
  await page.getByLabel('Upload visor graphic',{exact:true}).setInputFiles(path.resolve('public/templates/visor/generic-example.png'));
  await page.getByRole('img',{name:'Your visor graphic',exact:true}).waitFor();
  await page.getByRole('switch',{name:'Mirror graphic',exact:true}).click();
  await page.getByRole('group',{name:'Helmet editor section'}).getByRole('button',{name:'Sunstrip',exact:true}).click();
  await page.getByRole('switch',{name:'Upper sunstrip',exact:true}).click();
  await page.getByLabel('Letter font',{exact:true}).selectOption('custom');await page.getByLabel('System font',{exact:true}).selectOption('Comic Sans MS');
  await page.getByRole('button',{name:'Save profile',exact:true}).click();await page.getByRole('button',{name:'Profile saved',exact:true}).waitFor();
  const state=await page.evaluate(()=>window.openGrid.load()),p=state.document.profiles.find(p=>p.id===state.document.selectedProfileId);
  assert.equal(p.helmet.visor.graphicPlacement,'full');assert.ok(p.helmet.visor.graphic.startsWith('data:image/png;base64,'));assert.equal(p.helmet.visor.graphicMirror,false);assert.equal(p.helmet.visor.mirror,true);assert.equal(p.helmet.visor.enabled,false);assert.equal(p.helmet.visor.font,'custom');assert.equal(p.helmet.visor.customFont,'Comic Sans MS');
  await page.reload();await page.getByRole('button',{name:'Helmet',exact:true}).click();await page.getByRole('group',{name:'Helmet editor section'}).getByRole('button',{name:'Sunstrip',exact:true}).click();
  assert.equal(await page.getByLabel('Letter font',{exact:true}).inputValue(),'custom');assert.equal(await page.getByLabel('System font',{exact:true}).inputValue(),'Comic Sans MS');
  await page.getByRole('group',{name:'Helmet editor section'}).getByRole('button',{name:'Graphic',exact:true}).click();assert.equal(await area.getByRole('button',{name:'Custom',exact:true}).getAttribute('aria-pressed'),'true');
  console.log('PASS: Custom full visor mode, current-style PNG/PSD/guide downloads, independent strip switch, custom system font and saved reload.');
 }finally{if(app){await app.evaluate(({BrowserWindow})=>BrowserWindow.getAllWindows().forEach(w=>w.destroy())).catch(()=>{});await app.close();}await fs.rm(dir,{recursive:true,force:true});}
})().catch(error=>{console.error(error);process.exitCode=1;});
