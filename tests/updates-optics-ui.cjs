const { _electron: electron } = require('playwright-core');
const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const assert = require('node:assert/strict');
(async()=>{
  const dir=await fs.mkdtemp(path.join(os.tmpdir(),'opengrid-optics-ui-'));
  const target=path.join(dir,'fixtures','appData','CustomHeadset','settings.json');
  const original=Buffer.from('{"dreamAir":{"eyeRotation":2,"autoEyeRotation":false,"fovZoom":1,"flatFovZoom":1},"dreamAirSE":{"eyeRotation":3,"fovZoom":1},"keep":9007199254740993}\r\n');
  await fs.mkdir(path.dirname(target),{recursive:true}); await fs.writeFile(target,original);
  const env={...process.env,OPENGRIDXR_DATA_DIR:dir,OPENGRIDXR_TEST_MODE:'1'};delete env.ELECTRON_RUN_AS_NODE;
  const app=await electron.launch({executablePath:process.env.OPENGRIDXR_TEST_EXE||require('electron'),args:process.env.OPENGRIDXR_TEST_EXE?[]:[path.resolve('.')],env});
  try {
    const page=await app.firstWindow(), errors=[];page.on('pageerror',e=>errors.push(e.message));
    await page.getByRole('button',{name:'Runtime optics',exact:true}).click();
    await page.locator('#optics-variant').selectOption('neutral'); await page.locator('#optics-headset').selectOption('dreamAir');
    await page.getByRole('button',{name:'Read selected configuration',exact:true}).click();
    await page.getByRole('spinbutton',{name:'Eye rotation draft'}).fill('4');
    await page.getByRole('button',{name:'Review optics changes',exact:true}).click();
    const apply=page.getByRole('button',{name:'Apply reviewed optics',exact:true});
    await apply.waitFor(); assert.equal(await apply.isDisabled(),true);
    assert.deepEqual(await fs.readFile(target),original);
    await page.locator('.optics-confirmation input').check(); await apply.click();
    await page.getByText(/Configuration saved with an exact backup/).waitFor();
    assert.equal(JSON.parse(await fs.readFile(target)).dreamAir.eyeRotation,4);
    assert.match(await fs.readFile(target,'utf8'),/9007199254740993/);
    const options=await page.locator('#optics-backup option').evaluateAll(nodes=>nodes.map(n=>n.value).filter(Boolean));
    await page.locator('#optics-backup').selectOption(options[0]);
    await page.getByRole('button',{name:'Review full-file restore',exact:true}).click();
    await page.locator('.optics-confirmation input').check();
    await page.getByRole('button',{name:'Restore reviewed file',exact:true}).click();
    await page.getByText(/Configuration saved with an exact backup/).waitFor();
    assert.deepEqual(await fs.readFile(target),original);
    await page.getByRole('spinbutton',{name:'Eye rotation draft'}).fill('5');
    await page.getByRole('button',{name:'Review optics changes',exact:true}).click(); await apply.waitFor();
    await page.getByRole('spinbutton',{name:'Eye rotation draft'}).fill('6'); assert.equal(await apply.count(),0);
    await page.locator('#optics-headset').selectOption('dreamAirSE');
    assert.equal(await page.getByRole('spinbutton',{name:'Eye rotation draft'}).count(),0);
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
    await page.getByText('Install the Windows setup package to enable updates.',{exact:true}).waitFor();
    assert.equal((await page.evaluate(()=>window.openGrid.updateStatus())).phase,'unsupported');
    await assert.rejects(()=>page.evaluate(()=>window.openGrid.installUpdate()),/not ready/);
    assert.deepEqual(errors,[]);
    console.log('Optics review/apply/restore, stale UI review invalidation, helmet assets and source update isolation passed.');
  } finally {
    // This test intentionally leaves an unsaved helmet draft; bypass its close prompt only in the fixture.
    await app.evaluate(({BrowserWindow})=>{for(const win of BrowserWindow.getAllWindows())win.destroy();}).catch(()=>{});
    await app.close();await fs.rm(dir,{recursive:true,force:true});
  }
})().catch(e=>{console.error(e);process.exitCode=1;});
