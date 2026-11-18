const { _electron: electron } = require('playwright-core');
const fs = require('node:fs/promises'), path = require('node:path'), os = require('node:os');
const assert = require('node:assert/strict');
(async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'opengrid-close-'));
  const env = {...process.env, OPENGRIDXR_DATA_DIR: directory, OPENGRIDXR_TEST_MODE: '1'};
  delete env.ELECTRON_RUN_AS_NODE;
  const app = await electron.launch({
    executablePath: process.env.OPENGRIDXR_TEST_EXE || require('electron'),
    args: process.env.OPENGRIDXR_TEST_EXE ? [] : [path.resolve('.')], env, timeout: 60000,
  });
  try {
    const page = await app.firstWindow();
    const crop = page.getByRole('spinbutton', {name: 'Left', exact: true});
    await crop.waitFor({timeout: 20000});
    const original = await crop.inputValue();
    const csp = await page.locator('meta[http-equiv="Content-Security-Policy"]').getAttribute('content');
    assert.ok(!csp.includes('127.0.0.1') && !csp.includes('ws://'), 'packaged HTML excludes development local connections');
    await app.evaluate(({app, BrowserWindow, dialog}) => {
      BrowserWindow.getAllWindows()[0].hide();
      globalThis.closeQuestions = [];
      globalThis.closeChoice = 0;
      globalThis.keepTestRunning = event => event.preventDefault();
      app.on('before-quit', globalThis.keepTestRunning);
      dialog.showMessageBoxSync = (_window, options) => {
        globalThis.closeQuestions.push(options);
        return globalThis.closeChoice;
      };
    });
    await crop.fill(String(Number(original) + 1));
    await page.getByRole('button', {name: 'Save profile', exact: true}).waitFor();
    const question = await app.evaluate(({BrowserWindow}) => new Promise(resolve => {
      const win = BrowserWindow.getAllWindows()[0];
      win.webContents.once('will-prevent-unload', () => setImmediate(() => resolve({
        alive: !win.isDestroyed(), questions: globalThis.closeQuestions,
      })));
      win.close();
    }));
    assert.equal(question.alive, true, 'Keep editing leaves the window open');
    assert.equal(question.questions.length, 1);
    assert.deepEqual(question.questions[0].buttons, ['Keep editing', 'Discard and close']);
    assert.equal(question.questions[0].cancelId, 0, 'Escape keeps unsaved work');
    assert.equal(await crop.inputValue(), String(Number(original) + 1), 'unsaved edit is retained');
    const state = await page.evaluate(() => window.openGrid.load());
    assert.equal(state.document.profiles[0].view.crop.left, Number(original), 'closing does not silently save');
    const discarded = await app.evaluate(({BrowserWindow}) => new Promise(resolve => {
      globalThis.closeChoice = 1;
      const win = BrowserWindow.getAllWindows()[0];
      win.once('closed', () => resolve(globalThis.closeQuestions.length));
      win.close();
    }));
    assert.equal(discarded, 2, 'Discard and close actually closes the editor');
    console.log('PASS production CSP, Keep editing, retained unsaved edits, and Discard and close');
  } finally {
    await app.evaluate(({app, BrowserWindow}) => {
      app.removeListener('before-quit', globalThis.keepTestRunning);
      for (const win of BrowserWindow.getAllWindows()) win.destroy();
    }).catch(() => {});
    await app.close();
    await fs.rm(directory, {recursive: true, force: true});
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
