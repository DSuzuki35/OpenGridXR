'use strict';
// Runs the real sandboxed save-time canvas path without a headset or application window.
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const { pathToFileURL } = require('node:url');

if (!process.versions.electron) {
  const { spawn } = require('node:child_process');
  const env = { ...process.env }; delete env.ELECTRON_RUN_AS_NODE;
  const child = spawn(require('electron'), [__filename], { env, windowsHide: true, stdio: 'inherit' });
  child.on('error', error => { console.error(error); process.exitCode = 1; });
  child.on('exit', code => { process.exitCode = code ?? 1; });
} else {
  const { app, BrowserWindow, nativeImage } = require('electron');
  app.on('window-all-closed', () => {});
  app.whenReady().then(async () => {
    const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'opengrid-helmet-real-bake-'));
    try {
      const { createDocument, isSimplePolygon } = await import(pathToFileURL(path.resolve(__dirname, '../src/model.mjs')).href);
      const { prepareHelmetAssets } = require('../desktop/helmet-assets.cjs');
      const doc = createDocument(), p = doc.profiles[0];
      p.helmet.enabled = true; p.helmet.visor.text = 'STEREO'; p.helmet.visor.color = '#bd732e'; p.helmet.headMotion = .2;
      const opts = { runtimeDir: dir, assetDir: path.resolve(__dirname, '../public/helmets'), BrowserWindow };
      const result = await prepareHelmetAssets(doc, opts);
      const game = result.games.find(g => g.game === 'iracing');
      const [left, right] = await Promise.all([fs.readFile(game.leftPath), fs.readFile(game.rightPath)]);
      assert.notDeepEqual(left, right, 'Stereo pair must contain genuinely different images');
      const image = nativeImage.createFromBuffer(left);
      assert.deepEqual(image.getSize(), { width: 1920, height: 1080 });
      const pixels = image.toBitmap();
      assert.equal(pixels[(540 * 1920 + 960) * 4 + 3], 0, 'Center remains transparent');
      assert.equal(pixels[(50 * 1920 + 960) * 4 + 3], 255, 'Opaque brow remains opaque');
      const stripAlpha = pixels[(330 * 1920 + 750) * 4 + 3];
      assert.ok(stripAlpha > 100 && stripAlpha < 200, `Strip remains translucent: ${stripAlpha}`);
      assert.ok(isSimplePolygon(result.contours.iracing.left));
      assert.ok(result.contours.iracing.left.length <= 64);
      const config = JSON.parse(await fs.readFile(game.sidecarPath));
      assert.equal(config.enabled, true); assert.equal(config.headMotion, .2);
      assert.equal(BrowserWindow.getAllWindows().length, 0, 'Offscreen renderer destroyed');
      const before = (await fs.stat(game.leftPath)).mtimeMs;
      await prepareHelmetAssets(doc, opts);
      assert.equal((await fs.stat(game.leftPath)).mtimeMs, before, 'Unchanged save reuses images');
      p.mode = 'streaming';
      await prepareHelmetAssets(doc, opts);
      assert.equal(JSON.parse(await fs.readFile(game.sidecarPath)).enabled, true, 'Headset helmet remains on in streaming');
      p.helmet.enabled = false;
      await prepareHelmetAssets(doc, opts);
      assert.equal(JSON.parse(await fs.readFile(game.sidecarPath)).enabled, false);
      console.log('Real Electron stereo PNG preparation, transparency, conservative contours, cache, streaming and disabled sidecars passed.');
    } finally { await fs.rm(dir, {recursive: true, force: true}); }
  }).then(() => app.exit(0), error => { console.error(error); app.exit(1); });
}
