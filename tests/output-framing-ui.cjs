const {_electron:electron}=require('playwright-core');
const fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict');
(async()=>{
  const dir=await fs.mkdtemp(path.join(os.tmpdir(),'opengrid-framing-ui-'));
  const {createDocument}=await import('../src/model.mjs');
  const doc=createDocument();await fs.writeFile(path.join(dir,'profiles.json'),JSON.stringify(doc));
  const env={...process.env,OPENGRIDXR_DATA_DIR:dir,OPENGRIDXR_TEST_MODE:'1'};delete env.ELECTRON_RUN_AS_NODE;
  let app;
  try {
    app=await electron.launch({executablePath:process.env.OPENGRIDXR_TEST_EXE||require('electron'),
      args:process.env.OPENGRIDXR_TEST_EXE?[]:[path.resolve('.')],env,timeout:60000});
    const page=await app.firstWindow();page.setDefaultTimeout(20000);
    const errors=[];page.on('pageerror',error=>errors.push(error.message));
    await page.getByRole('navigation',{name:'Main navigation'}).getByRole('button',{name:'Stream',exact:true}).click();
    await app.evaluate(({ipcMain})=>{
      globalThis.framingMessages=[];
      ipcMain.on('opengrid:previewOutputFraming',(_event,value)=>globalThis.framingMessages.push({value,at:Date.now()}));
    });
    const waitFor=async predicate=>{
      const deadline=Date.now()+5000;
      while(Date.now()<deadline){const list=await app.evaluate(()=>globalThis.framingMessages);if(predicate(list))return list;await new Promise(r=>setTimeout(r,25));}
      throw Error('Expected real preview IPC did not arrive');
    };
    const field=name=>page.getByRole('spinbutton',{name:`${name} value`,exact:true});
    const before=await fs.readFile(path.join(dir,'profiles.json'),'utf8');
    await field('Global reserve').fill('13');await field('Horizontal framing').fill('31');await field('Vertical framing').fill('6');
    await waitFor(list=>list.at(-1)?.value?.offsetX===31&&list.at(-1)?.value?.offsetY===6);
    assert.equal((await app.evaluate(()=>globalThis.framingMessages.at(-1))).value.reserve.left,13);
    assert.equal(await fs.readFile(path.join(dir,'profiles.json'),'utf8'),before,'dragging never saves the profile');
    // Default Electron background throttling must still renew this short lease.
    await app.evaluate(({BrowserWindow})=>BrowserWindow.getAllWindows()[0].minimize());
    const hiddenStart=Date.now();await new Promise(r=>setTimeout(r,3200));
    const hidden=await app.evaluate(()=>globalThis.framingMessages.filter(m=>m.value));
    const hiddenTimes=hidden.filter(m=>m.at>=hiddenStart-1000).map(m=>m.at);
    assert.ok(hiddenTimes.length>=3,'minimized editor continues heartbeat');
    assert.ok(Math.max(...hiddenTimes.slice(1).map((at,i)=>at-hiddenTimes[i]))<2000,'minimized heartbeat stays within native lease');
    await app.evaluate(({BrowserWindow})=>{const w=BrowserWindow.getAllWindows()[0];w.restore();w.show();});
    await page.getByRole('button',{name:'Discard framing changes',exact:true}).click();
    await waitFor(list=>list.at(-1)?.value===null);
    assert.equal(await field('Horizontal framing').inputValue(),String(doc.profiles[0].stream.offsetX));
    assert.equal(await fs.readFile(path.join(dir,'profiles.json'),'utf8'),before);
    await field('Horizontal framing').fill('44');await waitFor(list=>list.at(-1)?.value?.offsetX===44);
    await page.locator('#game-select').selectOption('lmu');await waitFor(list=>list.at(-1)?.value===null);
    await page.locator('#game-select').selectOption('iracing');await waitFor(list=>list.at(-1)?.value?.offsetX===44);
    await page.getByRole('button',{name:'Save profile',exact:true}).click();
    await page.getByRole('button',{name:'Profile saved',exact:true}).waitFor();await waitFor(list=>list.at(-1)?.value===null);
    assert.equal(JSON.parse(await fs.readFile(path.join(dir,'profiles.json'),'utf8')).profiles[0].stream.offsetX,44);
    assert.deepEqual(errors,[]);
    console.log('Real Electron UI IPC: unsaved reserve/framing preview, minimized heartbeat, discard, game switch and save reset passed; no game/output channel opened.');
  } finally {
    if(app){await app.evaluate(({BrowserWindow})=>{for(const w of BrowserWindow.getAllWindows())w.destroy();});await app.close();}
    await fs.rm(dir,{recursive:true,force:true});
  }
})().catch(error=>{console.error(error);process.exitCode=1;});
