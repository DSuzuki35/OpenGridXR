'use strict';
// Runs the real sandboxed save-time canvas path without a headset or application window.
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const { pathToFileURL } = require('node:url');

// Packed 16-bit coordinates cross a byte carry here. Resizing their RG bytes
// separately jitters a narrow line by several pixels even though the actual
// coordinate changes by only 1/65535 across the entire image.
async function measureCoordinatePrecision(compose, aperture, helmet) {
  const canvas = (width, height) => Object.assign(document.createElement('canvas'), {width, height});
  const shell = canvas(2, 2), map = canvas(2, 2), ctx = map.getContext('2d');
  const data = ctx.createImageData(2, 2);
  const mapping = vertical => {
    for (let y = 0; y < 2; y++) for (let x = 0; x < 2; x++) {
      const at = (y * 2 + x) * 4, value = 32767 + (vertical ? y : x);
      data.data.set([value >> 8, value & 255, 0, 255], at);
    }
    ctx.putImageData(data, 0, 0); return map.toDataURL();
  };
  const uvUrl = mapping(false), uvVUrl = mapping(true);
  Object.assign(helmet, {opacity:0, visorTint:0, scale:100, offsetX:0, offsetY:0});
  delete helmet.openingAngle; delete helmet.openingWidth;
  Object.assign(helmet.visor, {enabled:false, graphicPlacement:'full', graphicMirror:false, mirror:false, textOpacity:100, softness:0});
  const results = [];
  for (const vertical of [false, true]) {
    const art = canvas(800, 450), paint = art.getContext('2d'); paint.fillStyle = 'white';
    if (vertical) paint.fillRect(399, 0, 2, 450); else paint.fillRect(0, 224, 800, 2);
    helmet.visor.graphic = art.toDataURL();
    const result = await compose({helmet, shellUrl:shell.toDataURL(), uvUrl, uvVUrl, width:256, height:144}, aperture);
    const image = new Image(); image.src = result.png; await image.decode();
    const output = canvas(256, 144), out = output.getContext('2d'); out.drawImage(image, 0, 0);
    const pixels = out.getImageData(0, 0, 256, 144).data;
    let minimum = 255;
    for (let i = 3; i < pixels.length; i += 4) minimum = Math.min(minimum, pixels[i]);
    results.push(minimum);
  }
  // Transparent map texels must not drag the surviving coordinate toward zero.
  data.data.set([128, 0, 0, 255], 0); data.data.set([0, 0, 0, 0], 4);
  data.data.set([128, 0, 0, 255], 8); data.data.set([0, 0, 0, 0], 12);
  ctx.putImageData(data, 0, 0);
  const masked = await compose({helmet, shellUrl:shell.toDataURL(), uvUrl:map.toDataURL(), uvVUrl, width:256, height:144}, aperture);
  const image = new Image(); image.src = masked.png; await image.decode();
  const output = canvas(256, 144), out = output.getContext('2d'); out.drawImage(image, 0, 0);
  const row = out.getImageData(0, 72, 256, 1).data;
  return {lineMinimum:results, coverage:[0, 127, 128, 255].map(x => row[x * 4 + 3])};
}

async function measureLetterEdges(compose, aperture, helmet) {
  const map = document.createElement('canvas'); map.width = 800; map.height = 450;
  const ctx = map.getContext('2d'), data = ctx.createImageData(800, 450);
  const shellUrl = map.toDataURL();
  const mapping = vertical => {
    for (let y = 0; y < 450; y++) for (let x = 0; x < 800; x++) {
      const at = (y * 800 + x) * 4, value = Math.round((vertical ? y / 449 : x / 799) * 65535);
      data.data.set([value >> 8, value & 255, 0, 255], at);
    }
    ctx.putImageData(data, 0, 0); return map.toDataURL();
  };
  const uvUrl = mapping(false), uvVUrl = mapping(true);
  Object.assign(helmet, {opacity:0, visorTint:0, scale:100, offsetX:0, offsetY:0});
  delete helmet.openingAngle; delete helmet.openingWidth;
  Object.assign(helmet.visor, {enabled:true, opacity:0, height:40, curve:0, text:'I'.repeat(40),
    textSize:44, font:'arial', textWidth:80, textCurve:0, textX:0, textY:0,
    softness:0, textOpacity:100, mirror:false, graphic:''});
  const result = await compose({helmet, shellUrl, uvUrl, uvVUrl, width:4096, height:2304}, aperture);
  const image = new Image(); image.src = result.png; await image.decode();
  map.width = 4096; map.height = 2304; ctx.drawImage(image, 0, 0);
  const row = ctx.getImageData(0, Math.round(156 / 450 * 2304), 4096, 1).data;
  let partial = 0, edges = 0, previous = false;
  for (let x = 0; x < 4096; x++) {
    const alpha = row[x * 4 + 3], inside = alpha >= 128;
    if (alpha > 10 && alpha < 245) partial++;
    if (inside !== previous) edges++;
    previous = inside;
  }
  return {edges, transitionWidth:partial / edges};
}

async function measureFittedGlyphs(compose, aperture, helmet) {
  const map = document.createElement('canvas'); map.width = 800; map.height = 450;
  const ctx = map.getContext('2d'), data = ctx.createImageData(800, 450), shellUrl = map.toDataURL();
  const mapping = vertical => {
    for (let y = 0; y < 450; y++) for (let x = 0; x < 800; x++) {
      const at = (y * 800 + x) * 4, value = Math.round((vertical ? y / 449 : x / 799) * 65535);
      data.data.set([value >> 8, value & 255, 0, 255], at);
    }
    ctx.putImageData(data, 0, 0); return map.toDataURL();
  };
  const uvUrl = mapping(false), uvVUrl = mapping(true);
  Object.assign(helmet, {opacity:0, visorTint:0, scale:100, offsetX:0, offsetY:0, openingWidth:100});
  delete helmet.openingAngle;
  Object.assign(helmet.visor, {enabled:true, opacity:0, height:26, curve:12, text:'Ug j y Q',
    textSize:44, textWidth:45, textCurve:10, textX:0, softness:0, textOpacity:100, mirror:true, graphic:''});
  const results = [];
  for (const [font, customFont] of [['arialNarrow',''], ['custom','Segoe UI']]) {
    const samples = [];
    for (const textY of [-5, 3]) {
      Object.assign(helmet.visor, {font, customFont, textY});
      const result = await compose({helmet, shellUrl, uvUrl, uvVUrl, width:800, height:450}, aperture);
      const image = new Image(); image.src = result.png; await image.decode();
      ctx.clearRect(0, 0, 800, 450); ctx.drawImage(image, 0, 0);
      const pixels = ctx.getImageData(0, 0, 800, 450).data;
      let top = 450, bottom = 0, alphaSum = 0;
      for (let y = 0; y < 450; y++) for (let x = 0; x < 800; x++) {
        const alpha = pixels[(y * 800 + x) * 4 + 3]; alphaSum += alpha;
        if (alpha > 10) { top = Math.min(top, y); bottom = Math.max(bottom, y); }
      }
      samples.push({height:bottom - top + 1, alphaSum});
    }
    results.push({font, samples});
  }
  return results;
}

// Identity maps isolate text bending from the baked visor perspective.
async function measureTextCurves(compose, aperture, helmet, renderWidth = 800) {
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
    const { png } = await compose({ helmet, shellUrl, uvUrl, uvVUrl, width: renderWidth, height: renderWidth * 9 / 16 }, aperture);
    const image = new Image(); image.src = png; await image.decode();
    ctx.clearRect(0, 0, 800, 450); ctx.drawImage(image, 0, 0, 800, 450);
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
        const precision = await curveWindow.webContents.executeJavaScript(`(${measureCoordinatePrecision.toString()})(${composeHelmetRaster.toString()}, ${deriveHelmetAperture.toString()}, ${JSON.stringify(p.helmet)})`);
        assert.deepEqual(precision.lineMinimum, [255, 255], 'Resized packed U and V coordinates preserve continuous narrow artwork across byte carries');
        assert.deepEqual(precision.coverage, [255, 128, 127, 0], 'Coordinate interpolation preserves smooth coverage without pulling transparent-map coordinates into visible ink');
        const sharpness = await curveWindow.webContents.executeJavaScript(`(${measureLetterEdges.toString()})(${composeHelmetRaster.toString()}, ${deriveHelmetAperture.toString()}, ${JSON.stringify(p.helmet)})`);
        assert.equal(sharpness.edges, 80, 'Detailed output retains each of the 40 separate letter strokes');
        assert.ok(sharpness.transitionWidth < 2, `4K letter edges must not magnify a fixed 1600px raster: ${JSON.stringify(sharpness)}`);
        console.log('UV continuity, alpha coverage and 4K letter-edge measurements:', JSON.stringify({precision, sharpness}));
        const glyphs = await curveWindow.webContents.executeJavaScript(`(${measureFittedGlyphs.toString()})(${composeHelmetRaster.toString()}, ${deriveHelmetAperture.toString()}, ${JSON.stringify(p.helmet)})`);
        for (const {font, samples:[reference, lowered]} of glyphs) {
          assert.ok(Math.abs(lowered.height - reference.height) <= 2, `${font}: fitting retains complete rounded glyphs and descenders near the strip edge: ${JSON.stringify({reference, lowered})}`);
          assert.ok(Math.abs(lowered.alphaSum / reference.alphaSum - 1) < .02, `${font}: fitting moves the complete glyph ink without clipping it`);
        }
        const curves = await curveWindow.webContents.executeJavaScript(`(${measureTextCurves.toString()})(${composeHelmetRaster.toString()}, ${deriveHelmetAperture.toString()}, ${JSON.stringify(p.helmet)})`);
        assert.ok(curves[0].png !== curves[1].png, 'Negative curvature must bend the rendered text');
        assert.ok(curves[2].png !== curves[1].png, 'Positive curvature must bend the rendered text');
        assert.ok(Math.abs(curves[1].left - curves[1].center) < 1, 'Zero curvature stays straight');
        assert.ok(curves[0].left - curves[0].center > 10, 'Negative curvature lowers both edges');
        assert.ok(curves[0].right - curves[0].center > 10, 'Negative curvature lowers mirrored right edge');
        assert.ok(curves[2].center - curves[2].left > 10, 'Positive curvature raises both edges');
        assert.ok(curves[2].center - curves[2].right > 10, 'Positive curvature raises mirrored right edge');
        const detailedCurves = await curveWindow.webContents.executeJavaScript(`(${measureTextCurves.toString()})(${composeHelmetRaster.toString()}, ${deriveHelmetAperture.toString()}, ${JSON.stringify(p.helmet)}, 4096)`);
        for (let i = 0; i < curves.length; i++) for (const part of ['left', 'center', 'right']) {
          assert.ok(Math.abs(detailedCurves[i][part] - curves[i][part]) < 1, `4K output preserves ${part} lettering placement for curve ${i}`);
        }
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
