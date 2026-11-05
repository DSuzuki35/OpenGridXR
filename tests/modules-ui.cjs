const { _electron: electron } = require('playwright-core');
const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const assert = require('node:assert/strict');
const { pathToFileURL } = require('node:url');

(async () => {
  const { createDocument } = await import(pathToFileURL(path.resolve('src/model.mjs')).href);
  const { MODULES } = await import(pathToFileURL(path.resolve('src/modules.mjs')).href);
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'opengrid-modules-ui-'));
  const profiles = createDocument();
  const profile = profiles.profiles[0];
  profile.stream.preserveFullFrame = true;
  profile.framegen.enabled = true;
  profile.telemetry.enabled = true;
  profile.helmet.shellBrightness = 73;
  profile.helmet.visor.text = 'MODULE TEST';
  const hudBinding = {
    actionId: 'overlay.enabled', deviceId: 'dinput:{11111111-1111-4111-8111-111111111111}',
    deviceName: 'Preserved module fixture', kind: 'button', index: 12,
  };
  profile.inputBindings.bindings = [hudBinding];
  const originalProfiles = JSON.stringify(profiles, null, 2) + '\n';
  await fs.writeFile(path.join(dir, 'profiles.json'), originalProfiles);
  await fs.writeFile(path.join(dir, 'module-settings.json'), JSON.stringify({
    version: 1, enabled: Object.fromEntries(MODULES.map(({ id }) => [id, false])),
  }));
  const env = { ...process.env, OPENGRIDXR_DATA_DIR: dir, OPENGRIDXR_TEST_MODE: '1' };
  delete env.ELECTRON_RUN_AS_NODE;
  let app;
  try {
    app = await electron.launch({
      executablePath: process.env.OPENGRIDXR_TEST_EXE || require('electron'),
      args: process.env.OPENGRIDXR_TEST_EXE ? [] : [path.resolve('.')],
      env, timeout: 60000,
    });
    const page = await app.firstWindow(), errors = [];
    page.on('pageerror', error => errors.push(error.message));
    const nav = page.getByRole('navigation', { name: 'Main navigation' });
    await nav.getByRole('button', { name: 'View & crop', exact: true }).waitFor();
    await page.getByRole('button', { name: 'Profile saved', exact: true }).waitFor();
    assert.deepEqual(await nav.getByRole('button').allTextContents().then(items => items.map(text => text.trim())),
      ['Session', 'View & crop', 'Profiles', 'Setup', 'OpenXR layers']);
    const requestedScripts = new Set();
    page.on('request', request => {
      const name = decodeURIComponent(request.url());
      if (/\.js(?:\?|$)/.test(name)) requestedScripts.add(name);
    });
    // Chromium file: resources may not appear in PerformanceResourceTiming.
    // Enabling the debugger also reports scripts parsed before Playwright attached.
    const debuggerSession = await page.context().newCDPSession(page);
    debuggerSession.on('Debugger.scriptParsed', ({ url }) => {
      const name = decodeURIComponent(url);
      if (/\.js(?:\?|$)/.test(name)) requestedScripts.add(name);
    });
    await debuggerSession.send('Debugger.enable');
    const loadedScripts = async () => [...new Set([
      ...requestedScripts,
      ...await page.evaluate(() => performance.getEntriesByType('resource').map(entry => decodeURIComponent(entry.name)).filter(name => /\.js(?:\?|$)/.test(name))),
    ])];
    const optionalChunk = /\/(?:HelmetEditor|QuadViewPage|TelemetryPanel|BenchmarkPanel|SpatialOverlaysPanel|InputBindings|EnhancementPanels|StreamPage)-[^/]+\.js(?:\?|$)/;
    const initialScripts = await loadedScripts();
    assert.ok(initialScripts.some(name => /\/index-[^/]+\.js$/.test(name)), 'The initial bundle is observed');
    assert.deepEqual(initialScripts.filter(name => optionalChunk.test(name)), [], 'Core-only startup does not fetch optional editor chunks');
    console.log('Core-only parsed JavaScript:', initialScripts.map(name => path.basename(name)).join(', '));
    assert.deepEqual((await page.evaluate(() => window.openGrid.load())).document, profiles);
    await assert.rejects(() => page.evaluate(() => window.openGrid.launchOutput()), /disabled/i);

    const moduleSwitch = id => page.getByRole('switch', { name: MODULES.find(module => module.id === id).label, exact: true });
    const apply = async expected => {
      await page.getByRole('button', { name: 'Apply modules', exact: true }).click();
      await page.waitForFunction(async expected => {
        const settings = await window.openGrid.getModuleSettings();
        return Object.entries(expected).every(([id, value]) => settings.enabled[id] === value);
      }, expected);
      await page.locator('.app-shell:not([inert])').waitFor();
      const savedButton = page.getByRole('button', { name: 'Profile saved', exact: true });
      await savedButton.waitFor();
      assert.equal(await savedButton.isDisabled(), true, 'Applying modules leaves the editor draft clean');
      assert.equal(await fs.readFile(path.join(dir, 'profiles.json'), 'utf8'), originalProfiles,
        'Applying modules never rewrites saved profile preferences');
    };
    await nav.getByRole('button', { name: 'Setup', exact: true }).click();
    await moduleSwitch('helmet').click();
    await apply({ helmet: true });
    assert.equal((await loadedScripts()).some(name => /\/HelmetEditor-[^/]+\.js(?:\?|$)/.test(name)), false,
      'Enabling Helmet keeps its editor unloaded until opened');
    await nav.getByRole('button', { name: 'Helmet', exact: true }).click();
    await page.getByRole('switch', { name: 'Show helmet', exact: true }).waitFor();
    assert.equal((await loadedScripts()).some(name => /\/HelmetEditor-[^/]+\.js(?:\?|$)/.test(name)), true,
      `Opening Helmet loads its editor chunk: ${JSON.stringify(await loadedScripts())}`);
    assert.equal(await page.getByRole('textbox', { name: 'Sunstrip text', exact: true }).inputValue(), 'MODULE TEST');

    await nav.getByRole('button', { name: 'Setup', exact: true }).click();
    await moduleSwitch('benchmark').click();
    assert.equal(await moduleSwitch('telemetry').getAttribute('aria-checked'), 'true', 'Benchmark automatically enables its Telemetry dependency');
    await apply({ benchmark: true, telemetry: true });
    await moduleSwitch('telemetry').click();
    assert.equal(await moduleSwitch('benchmark').getAttribute('aria-checked'), 'false', 'Disabling Telemetry also disables Benchmark');
    await apply({ benchmark: false, telemetry: false });

    await page.getByRole('button', { name: 'Core only', exact: true }).click();
    await apply(Object.fromEntries(MODULES.map(({ id }) => [id, false])));
    assert.equal(await nav.getByRole('button', { name: 'Helmet', exact: true }).count(), 0);
    await moduleSwitch('helmet').click();
    await apply({ helmet: true });
    await nav.getByRole('button', { name: 'Helmet', exact: true }).click();
    await page.getByRole('switch', { name: 'Show helmet', exact: true }).waitFor();
    assert.equal(await page.getByRole('textbox', { name: 'Sunstrip text', exact: true }).inputValue(), 'MODULE TEST');
    assert.deepEqual((await page.evaluate(() => window.openGrid.load())).document, profiles);
    await nav.getByRole('button', { name: 'Setup', exact: true }).click();
    await moduleSwitch('bindings').click();
    await apply({ bindings: true, overlay: false });
    await nav.getByRole('button', { name: 'Controller bindings', exact: true }).click();
    await page.getByRole('button', { name: 'Pause profile bindings', exact: true }).click();
    await page.getByRole('button', { name: 'Save profile', exact: true }).click();
    await page.getByRole('button', { name: 'Profile saved', exact: true }).waitFor();
    const pausedProfile = (await page.evaluate(() => window.openGrid.load())).document.profiles[0];
    assert.equal(pausedProfile.inputBindings.enabled, false);
    assert.deepEqual(pausedProfile.inputBindings.bindings, [hudBinding],
      'Changing input preferences preserves the saved binding for a disabled HUD module');
    await nav.getByRole('button', { name: 'Setup', exact: true }).click();
    await fs.mkdir('test-results', { recursive: true });
    await page.screenshot({ path: 'test-results/modules-settings.png' });
    await nav.getByRole('button', { name: 'Helmet', exact: true }).click();
    await page.getByRole('textbox', { name: 'Sunstrip text', exact: true }).fill('UNSAVED MODULE TEST');
    await nav.getByRole('button', { name: 'Setup', exact: true }).click();
    await moduleSwitch('helmet').click();
    assert.equal(await page.getByRole('button', { name: 'Apply modules', exact: true }).isDisabled(), true,
      'Unsaved profile edits block module changes');
    await assert.rejects(() => page.evaluate(async () => {
      const settings = await window.openGrid.getModuleSettings();
      settings.enabled.helmet = false;
      return window.openGrid.setModuleSettings(settings);
    }), /profile edits|Save or discard/i);
    assert.deepEqual(errors, []);
    console.log('Module visibility, lazy chunks, dependencies, persistence and profile preservation passed.');
  } finally {
    if (app) {
      await app.evaluate(({ BrowserWindow }) => { for (const window of BrowserWindow.getAllWindows()) window.destroy(); }).catch(() => {});
      await app.close().catch(() => {});
    }
    await fs.rm(dir, { recursive: true, force: true });
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
