import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { createDocument } from '../src/model.mjs';
import { prepareHelmetAssets } from '../desktop/helmet-assets.cjs';

test('disabled helmet writes fixed disabled sidecars without launching a renderer', async t => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'opengrid-helmet-assets-'));
  t.after(() => fs.rm(dir, {recursive: true, force: true}));
  const result = await prepareHelmetAssets(createDocument(), { runtimeDir: dir, assetDir: path.resolve('public/helmets'), BrowserWindow: class { constructor() { throw new Error('Must not render disabled helmets'); } } });
  assert.equal(result.games.length, 2);
  for (const game of ['iracing', 'lmu']) {
    const config = JSON.parse(await fs.readFile(path.join(dir, `OpenGridXR-helmet-${game}.json`)));
    assert.equal(config.enabled, false);
    assert.equal(config.headMotion, 0);
    assert.equal(config.leftImage, `OpenGridXR-helmet-${game}-left.png`);
  }
});

test('failed generation disables stale artwork and always destroys the isolated renderer', async t => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'opengrid-helmet-failure-'));
  t.after(() => fs.rm(dir, {recursive: true, force: true}));
  const doc = createDocument(); doc.profiles[0].helmet.enabled = true;
  await fs.writeFile(path.join(dir, 'OpenGridXR-helmet-iracing.json'), '{"version":1,"enabled":true}');
  let destroyed = false, options;
  class FailingWindow {
    constructor(opts) { options = opts; this.webContents = { on() {}, setWindowOpenHandler() {}, session: { setPermissionRequestHandler() {}, webRequest: { onBeforeRequest() {} } }, executeJavaScript: async () => { throw new Error('decode failed'); } }; }
    on() {}
    async loadURL(url) { assert.match(url, /^data:text\/html/); }
    isDestroyed() { return destroyed; }
    destroy() { destroyed = true; }
  }
  await assert.rejects(prepareHelmetAssets(doc, { runtimeDir: dir, assetDir: path.resolve('public/helmets'), BrowserWindow: FailingWindow }), /decode failed/);
  assert.equal(destroyed, true);
  assert.equal(options.show, false);
  assert.equal(options.webPreferences.sandbox, true);
  assert.equal(options.webPreferences.contextIsolation, true);
  assert.equal(options.webPreferences.nodeIntegration, false);
  assert.equal(JSON.parse(await fs.readFile(path.join(dir, 'OpenGridXR-helmet-iracing.json'))).enabled, false);
});

test('streaming preserves the headset helmet, and unchanged appearance reuses prepared images', async t => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'opengrid-helmet-stream-'));
  t.after(() => fs.rm(dir, {recursive: true, force: true}));
  const doc = createDocument(); doc.profiles[0].helmet.enabled = true; doc.profiles[0].mode = 'streaming';
  const png = `data:image/png;base64,${(await fs.readFile('public/helmets/formula-left.png')).toString('base64')}`;
  let executions = 0, destroyed = false;
  class RasterWindow {
    constructor() { this.webContents = { on() {}, setWindowOpenHandler() {}, session: { setPermissionRequestHandler() {}, webRequest: { onBeforeRequest() {} } }, executeJavaScript: async () => { executions++; return { png, contour: [{x:0,y:0},{x:1,y:0},{x:1,y:1},{x:0,y:1}] }; } }; }
    async loadURL() {}
    isDestroyed() { return destroyed; }
    destroy() { destroyed = true; }
  }
  const opts = { runtimeDir: dir, assetDir: path.resolve('public/helmets'), BrowserWindow: RasterWindow };
  await prepareHelmetAssets(doc, opts);
  assert.equal(JSON.parse(await fs.readFile(path.join(dir, 'OpenGridXR-helmet-iracing.json'))).enabled, true);
  assert.equal(executions, 2);
  assert.equal(destroyed, true);
  await prepareHelmetAssets(doc, opts);
  assert.equal(executions, 2);
});
