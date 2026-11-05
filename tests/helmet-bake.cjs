'use strict';
// Runs the real sandboxed save-time canvas path without a headset or application window.
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const { pathToFileURL } = require('node:url');

// Identity maps isolate text bending from the baked visor perspective.
async function measureTextCurves(compose, aperture, helmet) {
  const shell = document.createElement('canvas'); shell.width = 800; shell.height = 450;
  const map = document.createElement('canvas'); map.width = 800; map.height = 450;
  const ctx = map.getContext('2d'), pixels = ctx.createImageData(800, 450);
  const makeMap = vertical => {
    for (let y = 0; y < 450; y++) for (let x = 0; x < 800; x++) {
      const at = (y * 800 + x) * 4, value = Math.round((vertical ? y / 449 : x / 799) * 65535);
      pixels.data[at] = value >> 8; pixels.data[at + 1] = value & 255; pixels.data[at + 3] = 255;
    }
    ctx.putImageData(pixels, 0, 0); return map.toDataURL();
  };
  const shellUrl = shell.toDataURL(), uvUrl = makeMap(false), uvVUrl = makeMap(true);
  Object.assign(helmet, { opacity: 0, visorTint: 0, scale: 100, offsetX: 0, offsetY: 0 });
  Object.assign(helmet.visor, { enabled: true, text: 'IIIIIIIIIII', textColor: '#ffffff', height: 55, curve: 0, opacity: 0, textSize: 30, textWidth: 52, textY: -10, textX: 0, textOpacity: 100, softness: 0, mirror: true, graphic: '' });
  const results = [];
  for (const textCurve of [-20, 0, 20]) {
    helmet.visor.textCurve = textCurve;
    const { png } = await compose({ helmet, shellUrl, uvUrl, uvVUrl, width: 800, height: 450 }, aperture);
    const image = new Image(); image.src = png; await image.decode();
    ctx.clearRect(0, 0, 800, 450); ctx.drawImage(image, 0, 0);
    const data = ctx.getImageData(0, 0, 800, 450).data;
    const centroid = (start, end) => {
      let weight = 0, sum = 0;
      for (let y = 0; y < 450; y++) for (let x = start; x < end; x++) {
        const alpha = data[(y * 800 + x) * 4 + 3]; weight += alpha; sum += alpha * y;
      }
      if (!weight) throw new Error('Expected visible lettering');
      return sum / weight;
    };
    results.push({ png, left: centroid(190, 260), center: centroid(365, 435), right: centroid(540, 610) });
  }
  return results;
}

async function measureClearVisor(compose, aperture, helmet) {
  const shell=document.createElement('canvas'); shell.width=800;shell.height=450;
  const ctx=shell.getContext('2d');ctx.fillRect(0,0,800,70);ctx.fillRect(0,380,800,70);
  Object.assign(helmet,{visorTint:0,visorPosition:'half',scale:100,offsetX:0,offsetY:0,opacity:100});
  helmet.visor.enabled=false;
  const args={helmet,shellUrl:shell.toDataURL(),width:800,height:450};
  const half=await compose(args,aperture);
  helmet.visorPosition='open'; const open=await compose(args,aperture);
  return {half:half.png,open:open.png};
}
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
      const { composeHelmetRaster, deriveHelmetAperture } = await import(pathToFileURL(path.resolve(__dirname, '../src/helmet.mjs')).href);
      const { prepareHelmetAssets } = require('../desktop/helmet-assets.cjs');
      const doc = createDocument(), p = doc.profiles[0];
      const curveWindow = new BrowserWindow({ show: false, webPreferences: { sandbox: true, contextIsolation: true, nodeIntegration: false, offscreen: true } });
      try {
        await curveWindow.loadURL('data:text/html,<title>Text curvature regression</title>');
        const curves = await curveWindow.webContents.executeJavaScript(`(${measureTextCurves.toString()})(${composeHelmetRaster.toString()}, ${deriveHelmetAperture.toString()}, ${JSON.stringify(p.helmet)})`);
        assert.ok(curves[0].png !== curves[1].png, 'Negative curvature must bend the rendered text');
        assert.ok(curves[2].png !== curves[1].png, 'Positive curvature must bend the rendered text');
        assert.ok(Math.abs(curves[1].left - curves[1].center) < 1, 'Zero curvature stays straight');
        assert.ok(curves[0].left - curves[0].center > 10, 'Negative curvature lowers both edges');
        assert.ok(curves[0].right - curves[0].center > 10, 'Negative curvature lowers mirrored right edge');
        assert.ok(curves[2].center - curves[2].left > 10, 'Positive curvature raises both edges');
        assert.ok(curves[2].center - curves[2].right > 10, 'Positive curvature raises mirrored right edge');
        const clear=await curveWindow.webContents.executeJavaScript(`(${measureClearVisor.toString()})(${composeHelmetRaster.toString()}, ${deriveHelmetAperture.toString()}, ${JSON.stringify(p.helmet)})`);
        assert.ok(clear.half!==clear.open,'Half-open clear glass needs a visible lower rim even without tint or sunstrip');
      } finally { curveWindow.destroy(); }
      p.helmet.enabled = true; p.helmet.visor.text = 'FLAT'; p.helmet.visor.color = '#bd732e'; p.helmet.headMotion = .2;
      const opts = { runtimeDir: dir, assetDir: path.resolve(__dirname, '../public/helmets'), BrowserWindow };
      const result = await prepareHelmetAssets(doc, opts);
      const game = result.games.find(g => g.game === 'iracing');
      const [left, right] = await Promise.all([fs.readFile(game.leftPath), fs.readFile(game.rightPath)]);
      assert.deepEqual(left, right, 'Both eyes must receive the exact same 2D image');
      assert.deepEqual(result.contours.iracing.left, result.contours.iracing.right);
      const image = nativeImage.createFromBuffer(left);
      assert.deepEqual(image.getSize(), { width: 2048, height: 1152 });
      const pixels = image.toBitmap();
      const sampleAlpha = (bitmap, x, y, width = 2048, height = 1152) => bitmap[(Math.floor(y * height) * width + Math.floor(x * width)) * 4 + 3];
      assert.equal(sampleAlpha(pixels, .5, .5), 0, 'Center remains transparent');
      assert.equal(sampleAlpha(pixels, .5, 50 / 1080), 255, 'Opaque brow remains opaque');
      const stripAlpha = sampleAlpha(pixels, 750 / 1920, 330 / 1080);
      assert.ok(stripAlpha > 100 && stripAlpha < 200, `Strip remains translucent: ${stripAlpha}`);
      assert.ok(isSimplePolygon(result.contours.iracing.left));
      assert.ok(result.contours.iracing.left.length <= 64);
      p.helmet.visor.textCurve = 12;
      await prepareHelmetAssets(doc, opts);
      assert.deepEqual(await fs.readFile(game.leftPath), await fs.readFile(game.rightPath), 'Curved mirrored lettering remains identical in both eyes');
      const config = JSON.parse(await fs.readFile(game.sidecarPath));
      assert.equal(config.enabled, true); assert.equal(config.headMotion, .2);
      assert.equal(BrowserWindow.getAllWindows().length, 0, 'Offscreen renderer destroyed');
      const before = (await fs.stat(game.leftPath)).mtimeMs;
      await prepareHelmetAssets(doc, opts);
      assert.equal((await fs.stat(game.leftPath)).mtimeMs, before, 'Unchanged save reuses images');
      p.stream.preserveFullFrame = true;
      await prepareHelmetAssets(doc, opts);
      assert.equal(JSON.parse(await fs.readFile(game.sidecarPath)).enabled, true, 'Headset helmet remains on in streaming');
      p.helmet.visorHotkey = true; p.helmet.visorPosition = 'half'; p.helmet.visorTint = 20;
      const live = await prepareHelmetAssets(doc, opts);
      const liveConfig = JSON.parse(await fs.readFile(game.sidecarPath));
      assert.equal(liveConfig.version, 2); assert.equal(liveConfig.visorHotkey, true); assert.equal(liveConfig.visorPosition, 'half');
      const variants = await Promise.all([liveConfig.leftImage, liveConfig.halfImage, liveConfig.openImage].map(name => fs.readFile(path.join(dir, name))));
      assert.notDeepEqual(variants[0], variants[1], 'Half-raised artwork differs from closed');
      assert.notDeepEqual(variants[1], variants[2], 'Open artwork differs from half');
      for (const bytes of variants) {
        const variant = nativeImage.createFromBuffer(bytes);
        assert.deepEqual(variant.getSize(), { width: 2048, height: 1152 });
        assert.equal(sampleAlpha(variant.toBitmap(), .5, 50 / 1080), 255, 'Shell brow occludes every visor position');
        assert.ok(sampleAlpha(variant.toBitmap(), .5, .5) < 255, 'Every position leaves the scene visible through the center');
      }
      p.helmet.visorPosition = 'open';
      const liveBefore = (await fs.stat(game.leftPath)).mtimeMs;
      await prepareHelmetAssets(doc, opts);
      assert.equal((await fs.stat(game.leftPath)).mtimeMs, liveBefore, 'Initial live position changes reuse artwork');
      p.helmet.visorHotkey = false;
      const open = await prepareHelmetAssets(doc, opts);
      assert.deepEqual(await fs.readFile(game.leftPath), variants[2], 'Static open matches the live open variant');
      assert.deepEqual(open.contours.iracing, live.contours.iracing, 'Live crop uses the shell-only open aperture');
      p.helmet.visorPosition = 'half'; p.helmet.detail = 'detail';
      await prepareHelmetAssets(doc, opts);
      const detailImage = nativeImage.createFromBuffer(await fs.readFile(game.leftPath));
      assert.deepEqual(detailImage.getSize(), { width: 4096, height: 2304 });
      assert.ok(sampleAlpha(detailImage.toBitmap(), .5, .5, 4096, 2304) < 255, 'Detailed bake preserves transparency');
      assert.deepEqual(await fs.readFile(game.leftPath), await fs.readFile(game.rightPath));
      p.helmet.visorPosition = 'closed'; p.helmet.detail = 'performance'; p.helmet.visorTint = 0;
      for (const style of ['gt', 'open']) {
        p.helmet.style = style;
        const styled = await prepareHelmetAssets(doc, opts);
        const [leftStyle, rightStyle] = await Promise.all([fs.readFile(game.leftPath), fs.readFile(game.rightPath)]);
        assert.deepEqual(leftStyle, rightStyle, `${style}: same 2D artwork in both eyes`);
        assert.deepEqual(styled.contours.iracing.left, styled.contours.iracing.right);
        assert.ok(isSimplePolygon(styled.contours.iracing.left));
        const styledImage = nativeImage.createFromBuffer(leftStyle);
        assert.deepEqual(styledImage.getSize(), { width: 2048, height: 1152 });
        assert.equal(sampleAlpha(styledImage.toBitmap(), .5, .5), 0, `${style}: center remains transparent`);
      }
      p.helmet.enabled = false;
      await prepareHelmetAssets(doc, opts);
      assert.equal(JSON.parse(await fs.readFile(game.sidecarPath)).enabled, false);
      console.log('Real Electron 2048/4096 shared 2D PNG preparation, visor variants, transparency, conservative live contours, cache, streaming and disabled sidecars passed.');
    } finally { await fs.rm(dir, {recursive: true, force: true}); }
  }).then(() => app.exit(0), error => { console.error(error); app.exit(1); });
}
