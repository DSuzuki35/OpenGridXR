const {_electron:electron}=require('playwright-core');
const fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict');
(async()=>{
 const dir=await fs.mkdtemp(path.join(os.tmpdir(),'opengrid-reserve-'));
 const env={...process.env,OPENGRIDXR_DATA_DIR:dir,OPENGRIDXR_TEST_MODE:'1'};delete env.ELECTRON_RUN_AS_NODE;
 const app=await electron.launch({executablePath:process.env.OPENGRIDXR_TEST_EXE||require('electron'),args:process.env.OPENGRIDXR_TEST_EXE?[]:[path.resolve('.')],env});
 try{
  const page=await app.firstWindow(),errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.getByRole('button',{name:'Stream',exact:true}).click();
  assert.equal(await page.getByText('Open by design.',{exact:true}).count(),0);
  await page.getByRole('spinbutton',{name:'Global reserve value',exact:true}).fill('12');
  await page.locator('.reserve-details summary').click();
  for(const side of ['left','right','top','bottom'])assert.equal(await page.getByRole('spinbutton',{name:`Reserve ${side}`,exact:true}).inputValue(),'12');
  await page.getByRole('spinbutton',{name:'Reserve left',exact:true}).fill('20');
  await page.getByText(/Fine-tune individual sides.*Individual values/).waitFor();
  assert.equal(await page.getByRole('spinbutton',{name:'Reserve right',exact:true}).inputValue(),'12');
  await page.getByRole('button',{name:'Save profile',exact:true}).click();
  await page.getByRole('button',{name:'Profile saved',exact:true}).waitFor();
  let state=await page.evaluate(()=>window.openGrid.load());assert.deepEqual(state.document.profiles[0].stream.reserve,{left:20,right:12,top:12,bottom:12});
  await page.getByRole('spinbutton',{name:'Global reserve value',exact:true}).fill('7');
  for(const side of ['left','right','top','bottom'])assert.equal(await page.getByRole('spinbutton',{name:`Reserve ${side}`,exact:true}).inputValue(),'7');
  await fs.mkdir('test-results',{recursive:true});await page.screenshot({path:'test-results/reserve-global.png'});
  assert.deepEqual(errors,[]);console.log('Global reserve, independent fine tuning, persistence and removed sidebar card passed.');
 }finally{await app.evaluate(({BrowserWindow})=>{for(const w of BrowserWindow.getAllWindows())w.destroy();}).catch(()=>{});await app.close();await fs.rm(dir,{recursive:true,force:true});}
})().catch(e=>{console.error(e);process.exitCode=1;});
