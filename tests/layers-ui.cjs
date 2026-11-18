const { _electron: electron } = require('playwright-core');
const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const assert = require('node:assert/strict');
const http = require('node:http');

// Real React controls with the OS boundary replaced: never edits registrations.
// Catches inverted DWORD semantics, missing admin gating, optimistic success,
// duplicate in-flight actions, incorrect restore IDs and lost error messages.
(async () => {
  const { api } = await import('../src/api.mjs');
  assert.equal(typeof api.layersInventory, 'function', 'Browser preview exposes read-only layer inventory');
  const preview = await api.layersInventory();
  assert.deepEqual(preview.entries, []);
  assert.ok(preview.errors.some(message => /Windows app/i.test(message)));
  await assert.rejects(() => api.layersSetEnabled({ id: 'anything', enabled: true, admin: true }), /Windows app/i);
  // Compile before starting Electron: browser readiness must not depend on Vite's
  // first-request transforms, dependency optimization, HMR or a shared dev cache.
  // write:false leaves the product dist and every other test's build untouched.
  const { build } = await import('vite');
  const fixtureId = path.resolve('tests/__layers_fixture__.jsx').replaceAll('\\', '/');
  const bundle = await build({ logLevel: 'error', base: '/', plugins: [{
    name: 'layers-test-entry',
    resolveId(id) { if (id === 'virtual:layers-fixture') return fixtureId; },
    load(id) { if (id === fixtureId) return 'import React from "react"; import {createRoot} from "react-dom/client"; import LayersPage from "../src/components/LayersPage.jsx"; import "../src/styles.css"; createRoot(document.getElementById("root")).render(React.createElement(LayersPage));'; },
  }], build: { write: false, cssCodeSplit: false, rollupOptions: { input: 'virtual:layers-fixture', output: { entryFileNames: 'fixture.js', assetFileNames: '[name][extname]' } } } });
  const assets = new Map(bundle.output.map(item => ['/' + item.fileName, { body: item.type === 'chunk' ? item.code : item.source, type: item.fileName.endsWith('.css') ? 'text/css' : 'text/javascript' }]));
  for (const font of ['archivo', 'inter', 'dm-mono']) assets.set(`/fonts/${font}.woff2`, { body: await fs.readFile(path.resolve('public/fonts', `${font}.woff2`)), type: 'font/woff2' });
  assets.set('/__layers_test.html', { type: 'text/html', body: `<html><head>${bundle.output.filter(item => item.fileName.endsWith('.css')).map(item => `<link rel="stylesheet" href="/${item.fileName}">`).join('')}</head><body><div id="root"></div><img src="/__load_probe.gif" hidden alt=""><script type="module" src="/fixture.js"></script></body></html>` });
  // Keep an irrelevant image pending until the editor is ready. Reverting to a
  // whole-page load wait now fails deterministically, even with a warm cache.
  let loadProbe;
  const finishLoadProbe = () => {
    if (!loadProbe) return;
    loadProbe.writeHead(200, { 'Content-Type': 'image/gif' });
    loadProbe.end(Buffer.from('R0lGODlhAQABAIAAAAAAAP///ywAAAAAAQABAAACAUwAOw==', 'base64'));
    loadProbe = null;
  };
  const server = http.createServer((req, res) => {
    if (req.url === '/__load_probe.gif') { loadProbe = res; return; }
    const asset = assets.get(new URL(req.url, 'http://127.0.0.1').pathname);
    if (!asset) { res.writeHead(404); res.end(); return; }
    res.writeHead(200, { 'Content-Type': asset.type }); res.end(asset.body);
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'opengrid-layers-ui-'));
  const entry = path.join(dir, 'main.cjs');
  await fs.writeFile(entry, 'const {app,BrowserWindow}=require("electron");app.whenReady().then(()=>{new BrowserWindow({width:1180,height:1000,webPreferences:{contextIsolation:true}}).loadURL("about:blank")});');
  const env = { ...process.env }; delete env.ELECTRON_RUN_AS_NODE;
  let app;
  const errors = [], pendingRequests = new Set();
  try {
    app = await electron.launch({ executablePath: require('electron'), args: [entry], env });
    const page = await app.firstWindow(); page.setDefaultTimeout(10000);
    page.on('pageerror', error => errors.push(error.message));
    page.on('request', request => pendingRequests.add(request.url()));
    page.on('requestfinished', request => pendingRequests.delete(request.url()));
    page.on('requestfailed', request => { pendingRequests.delete(request.url()); errors.push(`${request.url()}: ${request.failure()?.errorText}`); });
    await page.addInitScript(() => {
      const entry = (id, scope, value, extra = {}) => ({ id, scope, view: '64', kind: 'implicit', manifest: `C:\\OpenXR providers\\${id}\\manifest.json`, type: 'DWORD', value, name: id, library: `C:\\OpenXR providers\\${id}\\provider.dll`, health: 'Ready', editable: true, ...extra });
      let state = { entries: [entry('User layer', 'HKCU', 0), entry('Machine layer', 'HKLM', 7, { kind: 'explicit', view: '32' }), entry('Unsupported entry', 'HKCU', 'text', { type: 'REG_SZ', editable: false, health: 'Unsupported registry value type' }), entry('Malformed DWORD', 'HKCU', null, { editable: true, health: 'Invalid DWORD data' })], errors: [], backups: [{ id: 'pending-backup', name: 'Unconfirmed layer', scope: 'HKCU', view: '64', kind: 'implicit', manifest: 'C:\\OpenXR providers\\unconfirmed\\manifest.json', before: 7, after: 0, createdAt: '2026-11-03T11:00:00Z', state: 'pending', canRestore: false }] };
      window.layerCalls = []; window.layerFailure = ''; window.inventoryFailure = ''; window.holdLayer = false;
      window.openGrid = {
        async layersInventory() { if (window.inventoryFailure) throw new Error(window.inventoryFailure); return structuredClone(state); },
        async layersSetEnabled(request) {
          window.layerCalls.push(request);
          if (window.holdLayer) await new Promise(resolve => { window.releaseLayer = resolve; });
          if (window.layerFailure) throw new Error(window.layerFailure);
          const layer = state.entries.find(item => item.id === request.id);
          state.backups.unshift({ id: 'backup-' + state.backups.length, name: layer.name, scope: layer.scope, view: layer.view, kind: layer.kind, manifest: layer.manifest, before: layer.value, after: request.enabled ? 0 : 1, createdAt: '2026-11-03T12:00:00Z', state: 'confirmed', canRestore: true });
          layer.value = request.enabled ? 0 : 1;
          return structuredClone(state);
        },
        async layersRestore(request) {
          window.layerCalls.push(request);
          const backup = state.backups.find(item => item.id === request.id);
          state.entries.find(item => item.manifest === backup.manifest).value = backup.before;
          backup.state = 'restored'; backup.canRestore = false;
          return structuredClone(state);
        },
      };
    });
    await page.goto(`http://127.0.0.1:${server.address().port}/__layers_test.html`, { waitUntil: 'commit' });
    const user = page.getByRole('article', { name: 'User layer', exact: true });
    const machine = page.getByRole('article', { name: 'Machine layer', exact: true });
    const unsupported = page.getByRole('article', { name: 'Unsupported entry', exact: true });
    await user.getByRole('button', { name: 'Disable', exact: true }).waitFor();
    await page.waitForFunction(() => document.querySelector('.layers-page')?.getAttribute('aria-busy') === 'false');
    assert.notEqual(await page.evaluate(() => document.readyState), 'complete', 'Inventory readiness does not wait for incidental resources');
    finishLoadProbe();
    await user.getByText('32/64-bit shared', { exact: true }).waitFor();
    assert.equal(await machine.getByRole('button', { name: 'Enable', exact: true }).isDisabled(), true);
    assert.equal(await unsupported.getByRole('button', { name: 'Read only', exact: true }).isDisabled(), true);
    const malformed = page.getByRole('article', { name: 'Malformed DWORD', exact: true });
    await malformed.getByText('Invalid DWORD', { exact: true }).waitFor();
    assert.equal(await malformed.getByRole('button', { name: 'Read only', exact: true }).isDisabled(), true);
    await page.getByText('Pending verification', { exact: false }).waitFor();
    assert.equal(await page.getByRole('button', { name: 'Restore Unconfirmed layer', exact: true }).isDisabled(), true);
    await page.getByRole('switch', { name: 'Allow administrator changes', exact: true }).click();
    await page.evaluate(() => { window.holdLayer = true; });
    await machine.getByRole('button', { name: 'Enable', exact: true }).click();
    assert.equal(await user.getByRole('button', { name: 'Disable', exact: true }).isDisabled(), true);
    assert.equal(await page.getByRole('button', { name: 'Refresh layers', exact: true }).isDisabled(), true);
    await page.evaluate(() => { window.holdLayer = false; window.releaseLayer(); });
    await machine.getByRole('button', { name: 'Disable', exact: true }).waitFor();
    assert.deepEqual(await page.evaluate(() => window.layerCalls[0]), { id: 'Machine layer', enabled: true, admin: true });
    await page.getByRole('button', { name: 'Restore Machine layer', exact: true }).click();
    await machine.getByRole('button', { name: 'Enable', exact: true }).waitFor();
    assert.deepEqual(await page.evaluate(() => window.layerCalls[1]), { id: 'backup-1', admin: true });
    await machine.getByText('DWORD 7', { exact: true }).waitFor();
    await page.evaluate(() => { window.layerFailure = 'Administrator approval was cancelled. No changes made.'; });
    await machine.getByRole('button', { name: 'Enable', exact: true }).click();
    await page.getByRole('alert').filter({ hasText: 'Administrator approval was cancelled' }).waitFor();
    assert.equal(await machine.getByRole('button', { name: 'Enable', exact: true }).isEnabled(), true);
    await page.evaluate(() => { window.layerFailure = ''; });
    await user.getByRole('button', { name: 'Disable', exact: true }).click();
    await user.getByRole('button', { name: 'Enable', exact: true }).waitFor();
    await page.locator('.layers-backup').filter({ hasText: 'User layer' }).getByText('HKCU · 32/64-bit shared', { exact: false }).waitFor();
    assert.deepEqual(await page.evaluate(() => window.layerCalls.at(-1)), { id: 'User layer', enabled: false, admin: false });
    await page.evaluate(() => { window.inventoryFailure = 'Inventory could not be refreshed.'; });
    await page.getByRole('button', { name: 'Refresh layers', exact: true }).click();
    await page.getByRole('alert').filter({ hasText: 'Inventory could not be refreshed' }).waitFor();
    assert.equal(await user.getByRole('button', { name: 'Enable', exact: true }).isDisabled(), true, 'Failed refresh invalidates stale mutation controls');
    await page.evaluate(() => { window.inventoryFailure = ''; });
    await page.getByRole('button', { name: 'Refresh layers', exact: true }).click();
    await page.waitForFunction(() => !document.querySelector('.layers-page')?.getAttribute('aria-busy') || document.querySelector('.layers-page').getAttribute('aria-busy') === 'false');
    await fs.mkdir(path.resolve('test-results'), { recursive: true });
    await page.screenshot({ path: path.resolve('test-results/layers-editor.png'), fullPage: true });
    await page.reload({ waitUntil: 'commit' });
    await user.getByRole('button', { name: 'Disable', exact: true }).waitFor();
    finishLoadProbe();
    assert.equal(await page.getByRole('switch', { name: 'Allow administrator changes', exact: true }).getAttribute('aria-checked'), 'false');
    assert.deepEqual(errors, []);
    console.log('Layer editor: DWORD semantics, admin opt-in, serialization, exact restore, cancellation and stale-inventory gating passed.');
  } catch (error) {
    console.error('Layer fixture failure details:', JSON.stringify({ pendingRequests: [...pendingRequests], pageErrors: errors }));
    throw error;
  } finally {
    finishLoadProbe();
    if (app) await app.close();
    await new Promise(resolve => server.close(resolve));
    await fs.rm(dir, { recursive: true, force: true });
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
