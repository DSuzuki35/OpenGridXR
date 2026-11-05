const {_electron}=require('playwright-core'),fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict');
(async()=>{
 const directory=await fs.mkdtemp(path.join(os.tmpdir(),'opengrid-visor-export-')),env={...process.env,OPENGRIDXR_DATA_DIR:directory,OPENGRIDXR_TEST_MODE:'1'};delete env.ELECTRON_RUN_AS_NODE;let app;
 try{
  app=await _electron.launch({executablePath:process.env.OPENGRIDXR_TEST_EXE||require('electron'),args:process.env.OPENGRIDXR_TEST_EXE?[]:[path.resolve('.')],env,timeout:60000});const page=await app.firstWindow();
  await app.evaluate(({dialog},directory)=>{dialog.showSaveDialog=async(_window,options)=>({canceled:false,filePath:directory+'/'+options.defaultPath});},directory);
  for(const [style,format,filename]of [['gt','psd','visor-template-gt.psd'],['open','guide','visor-guide-open.png'],['formula','png','visor-template.png']]){
   const result=await page.evaluate(request=>window.openGrid.downloadVisorTemplate(request),{style,format});assert.equal(result.saved,true);assert.deepEqual(await fs.readFile(path.join(directory,filename)),await fs.readFile(path.resolve('public/templates/visor',filename)));
  }
  const fonts=await page.evaluate(()=>window.openGrid.listSystemFonts());assert.equal(fonts.available,true,fonts.error);assert.ok(fonts.families.includes('Segoe UI'));
  const rejected=await app.evaluate(async({BrowserWindow,app})=>{const foreign=new BrowserWindow({show:false,webPreferences:{preload:app.getAppPath()+'/desktop/preload.cjs',sandbox:true,contextIsolation:true,nodeIntegration:false}});try{await foreign.loadURL('about:blank');return await foreign.webContents.executeJavaScript('window.openGrid.downloadVisorTemplate({style:"gt",format:"psd"}).then(()=>false,e=>e.message.includes("Untrusted"))');}finally{foreign.destroy();}});assert.equal(rejected,true);
  console.log('PASS: real trusted editor exports PNG/PSD/guide from app assets, installed fonts enumerate, foreign window IPC rejected.');
 }finally{if(app){await app.evaluate(({BrowserWindow})=>BrowserWindow.getAllWindows().forEach(w=>w.destroy())).catch(()=>{});await app.close();}await fs.rm(directory,{recursive:true,force:true});}
})().catch(error=>{console.error(error);process.exitCode=1;});
