const {_electron:electron}=require('playwright-core');
const fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict');
(async()=>{
  const {createDocument}=await import('../src/model.mjs');
  const directory=await fs.mkdtemp(path.join(os.tmpdir(),'opengrid-diagnostic-ui-'));
  const document=createDocument();document.selectedProfileId=document.profiles[1].id;
  await fs.writeFile(path.join(directory,'profiles.json'),JSON.stringify(document));
  await fs.mkdir(path.join(directory,'runtime'));
  const diagnostic={version:1,pid:process.pid,timestampMs:Date.now(),state:'unavailable',reason:'Engine data reference mismatch; incompatible game build',cpuAffinity:'skipped: unsupported CPU topology',reentryCount:3,outerTimeoutCount:2};
  await fs.writeFile(path.join(directory,'runtime','OpenGridXR-lmu-diagnostics.json'),JSON.stringify(diagnostic));
  const env={...process.env,OPENGRIDXR_DATA_DIR:directory,OPENGRIDXR_TEST_MODE:'1'};delete env.ELECTRON_RUN_AS_NODE;
  const executable=process.env.OPENGRIDXR_TEST_EXECUTABLE||process.env.OPENGRIDXR_TEST_EXE;
  let app;
  try {
    app=await electron.launch({executablePath:executable||require('electron'),args:executable?[]:[path.resolve('.')],env,timeout:60000});
    const page=await app.firstWindow();page.setDefaultTimeout(10000);
    await page.getByRole('navigation',{name:'Main navigation'}).getByRole('button',{name:'LMU quad views & VRS',exact:true}).click();
    await page.getByText('Engine data reference mismatch; incompatible game build',{exact:false}).waitFor();
    await page.getByText(/set LMU rendering mode to Off.*restart LMU/i).waitFor();
    await page.getByText('CPU preference outcome: skipped: unsupported CPU topology',{exact:true}).waitFor();
    await page.getByText('Guarded reentries: 3 · Outer image timeouts: 2',{exact:true}).waitFor();
    assert.equal((await page.evaluate(()=>window.openGrid.lmuStatus())).session,null);
    diagnostic.pid=2147483647;
    await fs.writeFile(path.join(directory,'runtime','OpenGridXR-lmu-diagnostics.json'),JSON.stringify(diagnostic));
    await page.getByText('Last LMU launch failed; the process has exited.',{exact:false}).waitFor();
    assert.equal((await page.evaluate(()=>window.openGrid.lmuStatus())).diagnostics.activeProcess,false);
    await fs.mkdir(path.resolve('test-results'),{recursive:true});
    await page.screenshot({path:path.resolve('test-results/lmu-startup-failure.png'),fullPage:true});
    Object.assign(diagnostic,{pid:process.pid,timestampMs:Date.now(),state:'ready',reason:'Engine hooks verified',warning:'Temporary full-resolution fallback: native outer data unavailable',nativeOuterRefusalCount:1});
    await fs.writeFile(path.join(directory,'runtime','OpenGridXR-lmu-diagnostics.json'),JSON.stringify(diagnostic));
    await page.getByText('Native outer fallback: Temporary full-resolution fallback: native outer data unavailable',{exact:true}).waitFor();
    await page.getByText('Native outer fallbacks observed: 1',{exact:true}).waitFor();
    assert.equal(await page.getByText(/LMU rendering hooks are unavailable|Last LMU launch failed/).count(),0);
    diagnostic.timestampMs=Date.now();diagnostic.nativeOuterRefusalCount=2;
    await fs.writeFile(path.join(directory,'runtime','OpenGridXR-lmu-diagnostics.json'),JSON.stringify(diagnostic));
    await page.getByText('Native outer fallbacks observed: 2',{exact:true}).waitFor();
    assert.equal((await page.evaluate(()=>window.openGrid.lmuStatus())).diagnostics.state,'ready');
    assert.equal(await page.getByText(/set LMU rendering mode to Off/).count(),0);
    await page.screenshot({path:path.resolve('test-results/lmu-native-outer-warning.png'),fullPage:true});
    console.log('LMU failed-startup recovery and nonfatal native-outer fallback verified.');
  } finally {
    if(app){await app.evaluate(({BrowserWindow})=>{for(const window of BrowserWindow.getAllWindows())window.destroy();});await app.close();}
    await fs.rm(directory,{recursive:true,force:true});
  }
})().catch(error=>{console.error(error);process.exitCode=1;});
