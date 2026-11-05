const { _electron: electron } = require('playwright-core');
const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const assert = require('node:assert/strict');
(async()=>{
  const dir=await fs.mkdtemp(path.join(os.tmpdir(),'opengrid-updates-ui-'));
  const env={...process.env,OPENGRIDXR_DATA_DIR:dir,OPENGRIDXR_TEST_MODE:'1'};delete env.ELECTRON_RUN_AS_NODE;
  const app=await electron.launch({executablePath:process.env.OPENGRIDXR_TEST_EXE||require('electron'),args:process.env.OPENGRIDXR_TEST_EXE?[]:[path.resolve('.')],env});
  try {
    const page=await app.firstWindow(), errors=[];page.on('pageerror',e=>errors.push(e.message));
    await page.getByRole('button',{name:'Helmet',exact:true}).click();
    await page.getByRole('switch',{name:'Show helmet',exact:true}).click();
    await page.getByRole('group',{name:'Helmet editor section',exact:true}).getByRole('button',{name:'Interior',exact:true}).click();
    for(const style of ['Formula','GT','Open']) {
      await page.getByRole('group',{name:'Helmet style',exact:true}).getByRole('button',{name:style,exact:true}).click();
      const img=page.locator('.helmet-render img'); await img.evaluate(el=>el.decode());
      assert.equal(await img.evaluate(el=>el.naturalWidth),1920);
    }
    await fs.mkdir('test-results',{recursive:true}); await page.screenshot({path:'test-results/helmets-preview.png'});
    await page.getByRole('button',{name:'Setup',exact:true}).click();
    const offline = page.getByRole('button',{name:'Test offline in LMU',exact:true});
    await offline.waitFor();
    assert.equal(await offline.isDisabled(),true, 'Unsaved profile / missing layer blocks launch');
    await assert.rejects(()=>page.evaluate(()=>window.openGrid.launchLmuOffline()),/disabled in test mode/);
    await page.getByText('Install the Windows setup package to enable updates.',{exact:true}).waitFor();
    assert.equal((await page.evaluate(()=>window.openGrid.updateStatus())).phase,'unsupported');
    await assert.rejects(()=>page.evaluate(()=>window.openGrid.installUpdate()),/not ready/);
    assert.deepEqual(errors,[]);
    console.log('Helmet assets and source update isolation passed.');
  } finally {
    // This test intentionally leaves an unsaved helmet draft; bypass its close prompt only in the fixture.
    await app.evaluate(({BrowserWindow})=>{for(const win of BrowserWindow.getAllWindows())win.destroy();}).catch(()=>{});
    await app.close();await fs.rm(dir,{recursive:true,force:true});
  }
})().catch(e=>{console.error(e);process.exitCode=1;});
