const { _electron } = require('playwright-core');
const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const assert = require('node:assert/strict');

(async () => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'opengrid-bridge-ui-'));
  const env = {...process.env, OPENGRIDXR_DATA_DIR:dir, OPENGRIDXR_TEST_MODE:'1'};
  delete env.ELECTRON_RUN_AS_NODE;
  let app;
  try {
    app = await _electron.launch({executablePath:process.env.OPENGRIDXR_TEST_EXE || require('electron'), args:process.env.OPENGRIDXR_TEST_EXE ? [] : [path.resolve('.')], env});
    // Replace the lazy backend in this isolated main process: no SimHub files,
    // process state or real installer actions are touched by the UI test.
    await app.evaluate(({app}) => {
      const load = process.getBuiltinModule('module').createRequire(app.getAppPath() + '/desktop/main.cjs');
      const fixture = globalThis.simHubBridgeFixture = {
        state:{directory:'C:\\SimHub',available:true,installed:false,current:false,running:false,error:null,message:'Bridge is not installed.'},
        installs:0, fail:false, chooseCancel:true,
      };
      let firstStatus = true;
      load('./simhub-bridge.cjs').createSimHubBridge = () => ({
        status: async () => {
          if (firstStatus) { firstStatus = false; await new Promise(resolve => { fixture.releaseInitial = resolve; }); }
          return {...fixture.state};
        },
        choose: async () => ({...fixture.state}),
        install: async () => {
          fixture.installs++;
          if (fixture.fail) throw Error('Close SimHub before installing the bridge.');
          fixture.state = {...fixture.state,installed:true,current:true,message:'Bridge installed. Start SimHub and enable oVRlay lights.'};
          return {...fixture.state};
        },
      });
    });
    const page = await app.firstWindow();
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.getByRole('navigation', {name:'Main navigation'}).getByRole('button', {name:'Setup',exact:true}).click();
    const choose = page.getByRole('button', {name:'Choose SimHub folder',exact:true});
    await choose.waitFor();
    assert.equal(await choose.isDisabled(), true, 'Initial discovery cannot race with folder selection');
    assert.equal(await page.getByRole('button', {name:'Refresh bridge status',exact:true}).isDisabled(), true);
    await app.evaluate(async () => {
      while (!globalThis.simHubBridgeFixture.releaseInitial) await new Promise(resolve => setTimeout(resolve, 10));
      globalThis.simHubBridgeFixture.releaseInitial();
    });
    const install = page.getByRole('button', {name:'Install SimHub bridge',exact:true});
    await install.waitFor();
    await install.click();
    const current = page.getByRole('button', {name:'Bridge up to date',exact:true});
    await current.waitFor();
    assert.equal(await current.isDisabled(), true);
    assert.equal(await app.evaluate(() => globalThis.simHubBridgeFixture.installs), 1);
    await page.getByRole('button', {name:'Choose SimHub folder',exact:true}).click();
    assert.equal(await current.isDisabled(), true, 'Cancelling folder selection keeps the installed state');
    await app.evaluate(() => { Object.assign(globalThis.simHubBridgeFixture.state, {current:false,running:true,message:'Close SimHub before installing the bridge.'}); });
    await page.getByRole('button', {name:'Refresh bridge status',exact:true}).click();
    const update = page.getByRole('button', {name:'Update SimHub bridge',exact:true});
    await update.waitFor();
    assert.equal(await update.isDisabled(), true, 'A running SimHub blocks installation');
    await app.evaluate(() => { Object.assign(globalThis.simHubBridgeFixture.state, {running:false}); globalThis.simHubBridgeFixture.fail = true; });
    await page.getByRole('button', {name:'Refresh bridge status',exact:true}).click();
    await update.click();
    await page.getByRole('alert').filter({hasText:'Close SimHub before installing'}).waitFor();
    assert.doesNotMatch(await page.getByRole('alert').innerText(), /Error invoking remote method/);
    assert.equal(await app.evaluate(() => globalThis.simHubBridgeFixture.installs), 2);
    await page.getByText('Supported wheels and manual installation', {exact:true}).click();
    assert.equal(await page.getByRole('button', {name:'Open bridge files',exact:true}).isVisible(), true);
    assert.match(await page.locator('body').innerText(), /Hyper P1, Hyper SL, GXL V2, GT-MAX 32, FPE2 and Ascher Artura Ultimate/);
    assert.equal(await choose.evaluate(button => {
      const container = button.parentElement.getBoundingClientRect();
      return [...button.parentElement.querySelectorAll('button')].every(item => {
        const bounds = item.getBoundingClientRect();
        return bounds.left >= container.left - 1 && bounds.right <= container.right + 1;
      });
    }), true, 'Bridge buttons fit their narrow Setup column');
    await fs.mkdir('test-results', {recursive:true});
    await page.screenshot({path:'test-results/simhub-bridge-setup.png',fullPage:true});
    assert.deepEqual(errors, []);
    console.log('SimHub Setup install/update, cancellation, running guard, failure feedback and supported models passed.');
  } finally {
    if (app) await app.close();
    await fs.rm(dir, {recursive:true,force:true,maxRetries:5,retryDelay:100});
  }
})().catch(error => {console.error(error);process.exitCode=1;});
