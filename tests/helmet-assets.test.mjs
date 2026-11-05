import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { createDocument } from '../src/model.mjs';
import { prepareHelmetAssets } from '../desktop/helmet-assets.cjs';
import { composeHelmetRaster, deriveHelmetAperture } from '../src/helmet.mjs';

// The Electron boundary is mocked; filesystem publication and caching stay real.
function pngFixture(width = 2048, height = 1152, marker = 0) {
  const bytes = Buffer.alloc(34);
  Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]).copy(bytes);
  bytes.write('IHDR', 12); bytes.writeUInt32BE(width, 16); bytes.writeUInt32BE(height, 20);
  bytes[24] = 8; bytes[25] = 6; bytes[33] = marker;
  return `data:image/png;base64,${bytes.toString('base64')}`;
}

function rasterWindow(render) {
  return class {
    constructor() {
      this.webContents = { on() {}, setWindowOpenHandler() {}, session: { setPermissionRequestHandler() {}, webRequest: { onBeforeRequest() {} } },
        executeJavaScript: async code => {
          const prefix = `(${composeHelmetRaster.toString()})(`, suffix = `, ${deriveHelmetAperture.toString()})`;
          assert.ok(code.startsWith(prefix) && code.endsWith(suffix));
          return render(JSON.parse(code.slice(prefix.length, -suffix.length)));
        } };
    }
    async loadURL() {}
    isDestroyed() { return false; }
    destroy() {}
  };
}

test('stream helmet prepares independent artwork while headset is disabled and never returns mask contours', async t => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'opengrid-stream-helmet-'));
  t.after(() => fs.rm(dir, { recursive: true, force: true }));
  const doc = createDocument(), profile = doc.profiles[0];
  profile.helmet.enabled = false;
  Object.assign(profile.helmet, { scale: 90, offsetX: 3, visorHotkey: true, visorPosition: 'half', headMotion: .5, stereoOffset: 2 });
  profile.stream.helmet = { enabled: true, scale: 200, offsetX: -10, offsetY: 12, opacity: 40 };
  const originalHelmet = structuredClone(profile.helmet), calls = [];
  const BrowserWindow = rasterWindow(payload => {
    calls.push(payload);
    return { png: pngFixture(payload.width, payload.height), contour: [{ x: 0, y: 0 }, { x: 1, y: 0 }, { x: .5, y: 1 }] };
  });
  const opts = { runtimeDir: dir, assetDir: path.resolve('public/helmets'), BrowserWindow };
  const result = await prepareHelmetAssets(doc, opts);
  assert.equal(calls.length, 1);
  assert.deepEqual(profile.helmet, originalHelmet);
  assert.deepEqual(result.contours, {});
  for (const field of ['scale', 'offsetX', 'offsetY']) assert.equal(calls[0].helmet[field], profile.stream.helmet[field]);
  assert.equal(calls[0].helmet.opacity, 100, 'Stream opacity is independent of the headset shell opacity');
  assert.equal(calls[0].helmet.headMotion, 0); assert.equal(calls[0].helmet.stereoOffset, 0);
  assert.equal(calls[0].helmet.visorPosition, 'half'); assert.equal(calls[0].helmet.visorHotkey, false);
  const manifest = path.join(dir, 'OpenGridXR-stream-helmet-iracing.json');
  const config = JSON.parse(await fs.readFile(manifest));
  assert.equal(config.enabled, true); assert.equal(config.leftImage, 'OpenGridXR-stream-helmet-iracing-left.png'); assert.equal(config.streamOpacity, 40);
  assert.equal(JSON.parse(await fs.readFile(path.join(dir, 'OpenGridXR-helmet-iracing.json'))).enabled, false);
  await prepareHelmetAssets(doc, opts);
  assert.equal(calls.length, 1, 'Unchanged stream appearance is cached');
  profile.stream.helmet.opacity = 15;
  await prepareHelmetAssets(doc, opts);
  assert.equal(calls.length, 1, 'Global opacity changes update the manifest without baking again');
  assert.equal(JSON.parse(await fs.readFile(manifest)).streamOpacity, 15);
  profile.stream.helmet.enabled = false;
  await prepareHelmetAssets(doc, opts);
  assert.equal(calls.length, 1, 'Disabling stream helmet never renders');
  assert.equal(JSON.parse(await fs.readFile(manifest)).enabled, false);
});

test('disabled helmet writes fixed disabled sidecars without launching a renderer', async t => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'opengrid-helmet-assets-'));
  t.after(() => fs.rm(dir, {recursive: true, force: true}));
  const result = await prepareHelmetAssets(createDocument(), { runtimeDir: dir, assetDir: path.resolve('public/helmets'), BrowserWindow: class { constructor() { throw new Error('Must not render disabled helmets'); } } });
  assert.equal(result.games.length, 2);
  for (const game of ['iracing', 'lmu']) {
    const config = JSON.parse(await fs.readFile(path.join(dir, `OpenGridXR-helmet-${game}.json`)));
    assert.equal(config.enabled, false);
    assert.equal(config.headMotion, 0);
    assert.equal(config.stereoOffset, 0);
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
  doc.profiles[0].helmet.stereoOffset = -1.5;
  const png = pngFixture();
  let executions = 0, destroyed = false;
  class RasterWindow {
    constructor() { this.webContents = { on() {}, setWindowOpenHandler() {}, session: { setPermissionRequestHandler() {}, webRequest: { onBeforeRequest() {} } }, executeJavaScript: async () => { executions++; return { png, contour: [{x:0,y:0},{x:1,y:0},{x:1,y:1},{x:0,y:1}] }; } }; }
    async loadURL() {}
    isDestroyed() { return destroyed; }
    destroy() { destroyed = true; }
  }
  const assetNames = ['formula.png', 'formula-mono-visor-uv.png', 'formula-mono-visor-v.png'];
  const originalAssets = path.join(dir, 'original-assets');
  await fs.mkdir(originalAssets);
  for (const name of assetNames) {
    const target = path.join(originalAssets, name);
    await fs.copyFile(path.resolve('public/helmets', name), target);
    await fs.utimes(target, 1700000000, 1700000000);
  }
  const opts = { runtimeDir: dir, assetDir: originalAssets, BrowserWindow: RasterWindow };
  const result = await prepareHelmetAssets(doc, opts);
  assert.equal(JSON.parse(await fs.readFile(path.join(dir, 'OpenGridXR-helmet-iracing.json'))).enabled, true);
  assert.equal(JSON.parse(await fs.readFile(path.join(dir, 'OpenGridXR-helmet-iracing.json'))).stereoOffset, -1.5);
  assert.equal(executions, 1, 'Compose a single 2D image for both eyes');
  assert.deepEqual(await fs.readFile(result.games[0].leftPath), await fs.readFile(result.games[0].rightPath));
  assert.deepEqual(result.contours.iracing.left, result.contours.iracing.right);
  assert.equal(destroyed, true);
  await prepareHelmetAssets(doc, opts);
  assert.equal(executions, 1);
  const replacementAssets = path.join(dir, 'replacement-assets');
  await fs.mkdir(replacementAssets);
  for (const name of assetNames) {
    const source = path.join(opts.assetDir, name), target = path.join(replacementAssets, name);
    const stat = await fs.stat(source);
    await fs.copyFile(source, target);
    await fs.utimes(target, stat.atime, stat.mtime);
    assert.equal((await fs.stat(target)).mtimeMs, stat.mtimeMs);
  }
  const replacementOpts = { ...opts, assetDir: replacementAssets };
  await prepareHelmetAssets(doc, replacementOpts);
  assert.equal(executions, 2, 'Asset identity changes invalidate cached artwork');
  await prepareHelmetAssets(doc, replacementOpts);
  assert.equal(executions, 2);
  await fs.unlink(result.games[0].rightPath);
  await prepareHelmetAssets(doc, replacementOpts);
  assert.equal(executions, 3, 'A missing eye copy is regenerated');
  assert.deepEqual(await fs.readFile(result.games[0].leftPath), await fs.readFile(result.games[0].rightPath));
});

test('static visor bakes only the selected position at the chosen detail size', async t => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'opengrid-helmet-static-'));
  t.after(() => fs.rm(dir, { recursive: true, force: true }));
  const doc = createDocument(), helmet = doc.profiles[0].helmet;
  Object.assign(helmet, { enabled: true, visorPosition: 'half', visorHotkey: false, detail: 'detail' });
  const calls = [];
  const BrowserWindow = rasterWindow(payload => {
    calls.push(payload);
    return { png: pngFixture(payload.width, payload.height), contour: [{ x: 0, y: 0 }, { x: 1, y: 0 }, { x: .5, y: 1 }] };
  });
  const result = await prepareHelmetAssets(doc, { runtimeDir: dir, assetDir: path.resolve('public/helmets'), BrowserWindow });
  assert.equal(calls.length, 1);
  assert.equal(calls[0].helmet.visorPosition, 'half');
  assert.equal(calls[0].width, 4096); assert.equal(calls[0].height, 2304);
  assert.equal(calls[0].paddingPixels, 4 + doc.profiles[0].mask.padding / 100 * 4096);
  const config = JSON.parse(await fs.readFile(result.games[0].sidecarPath));
  assert.equal(config.version, 2);
  assert.equal(config.visorHotkey, false);
  assert.equal(config.halfImage, undefined);
  assert.equal(config.openImage, undefined);
  assert.deepEqual(await fs.readFile(result.games[0].leftPath), await fs.readFile(result.games[0].rightPath));
  assert.equal((await fs.readdir(dir)).filter(name => name.endsWith('.png')).length, 2);
});

test('live visor publishes a complete generation, uses the open contour and validates every cached variant', async t => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'opengrid-helmet-live-'));
  t.after(() => fs.rm(dir, { recursive: true, force: true }));
  const doc = createDocument(), helmet = doc.profiles[0].helmet;
  Object.assign(helmet, { enabled: true, visorPosition: 'half', visorHotkey: true, detail: 'performance' });
  const calls = [], positions = ['closed', 'half', 'open'];
  const contours = positions.map((_, i) => [{ x: i / 10, y: 0 }, { x: 1, y: 0 }, { x: .5, y: 1 }]);
  const BrowserWindow = rasterWindow(payload => {
    calls.push(payload);
    const i = positions.indexOf(payload.helmet.visorPosition);
    return { png: pngFixture(payload.width, payload.height, i), contour: contours[i] };
  });
  const opts = { runtimeDir: dir, assetDir: path.resolve('public/helmets'), BrowserWindow };
  const manifest = path.join(dir, 'OpenGridXR-helmet-iracing.json');
  const originalRename = fs.rename.bind(fs);
  const publications = [];
  t.mock.method(fs, 'rename', async (from, to) => {
    if (to.endsWith('.png')) {
      const config = JSON.parse(await fs.readFile(manifest));
      assert.equal(config.enabled, false, 'Disable the manifest before replacing any fixed PNG filename');
      publications.push(path.basename(to));
    }
    if (to === manifest) {
      const next = JSON.parse(await fs.readFile(from));
      if (next.enabled) for (const field of ['leftImage', 'rightImage', 'halfImage', 'openImage'])
        assert.ok((await fs.stat(path.join(dir, next[field]))).size > 0, 'Enable only after every variant exists');
    }
    return originalRename(from, to);
  });
  const result = await prepareHelmetAssets(doc, opts);
  assert.deepEqual(calls.map(p => p.helmet.visorPosition), positions);
  assert.equal(publications.length, 4);
  assert.deepEqual(result.contours.iracing.left, contours[2]);
  const config = JSON.parse(await fs.readFile(manifest));
  assert.equal(config.version, 2); assert.equal(config.visorHotkey, true); assert.equal(config.visorPosition, 'half');
  assert.equal(config.halfImage, 'OpenGridXR-helmet-iracing-half.png');
  assert.equal(config.openImage, 'OpenGridXR-helmet-iracing-open.png');
  helmet.visorPosition = 'open'; helmet.visorBrightness = 110; helmet.visorContrast = 105;
  await prepareHelmetAssets(doc, opts);
  assert.equal(calls.length, 3, 'Switching initial state or scene grading reuses prepared variants');
  assert.equal(JSON.parse(await fs.readFile(manifest)).visorPosition, 'open');
  await fs.writeFile(path.join(dir, config.halfImage), 'corrupt');
  await prepareHelmetAssets(doc, opts);
  assert.equal(calls.length, 6, 'A damaged additional variant invalidates the generation');
  await fs.unlink(path.join(dir, config.openImage));
  await prepareHelmetAssets(doc, opts);
  assert.equal(calls.length, 9, 'A missing additional variant invalidates the generation');
  helmet.headMotion = .2;
  await prepareHelmetAssets(doc, opts);
  assert.equal(calls.length, 10, 'Motion margin changes recalculate only the conservative contour');
  assert.equal(calls[9].apertureOnly, true);
  assert.equal(calls[9].helmet.visorPosition, 'open');
  assert.equal(publications.length, 12, 'Contour-only changes do not replace image files');
});

test('a failed additional-variant replacement leaves the manifest disabled and invalidates the cache', async t => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'opengrid-helmet-write-failure-'));
  t.after(() => fs.rm(dir, { recursive: true, force: true }));
  const doc = createDocument();
  Object.assign(doc.profiles[0].helmet, { enabled: true, visorHotkey: true, visorPosition: 'closed' });
  let calls = 0;
  const BrowserWindow = rasterWindow(payload => {
    calls++;
    return { png: pngFixture(payload.width, payload.height, calls), contour: [{ x: 0, y: 0 }, { x: 1, y: 0 }, { x: .5, y: 1 }] };
  });
  const opts = { runtimeDir: dir, assetDir: path.resolve('public/helmets'), BrowserWindow };
  const result = await prepareHelmetAssets(doc, opts), manifest = result.games[0].sidecarPath;
  const first = JSON.parse(await fs.readFile(manifest));
  const originalRename = fs.rename.bind(fs);
  const rename = t.mock.method(fs, 'rename', async (from, to) => {
    if (to.endsWith('-half.png')) throw new Error('Simulated disk write failure');
    return originalRename(from, to);
  });
  doc.profiles[0].helmet.visor.color = '#112233';
  await assert.rejects(prepareHelmetAssets(doc, opts), /Simulated disk write failure/);
  const failed = JSON.parse(await fs.readFile(manifest));
  assert.equal(failed.enabled, false);
  assert.notEqual(failed.generation, first.generation);
  rename.mock.restore();
  await prepareHelmetAssets(doc, opts);
  assert.equal(calls, 9, 'Failed publication cannot reuse an incomplete cached generation');
  const final = JSON.parse(await fs.readFile(manifest));
  assert.equal(final.enabled, true);
  assert.notEqual(final.generation, failed.generation);
});
