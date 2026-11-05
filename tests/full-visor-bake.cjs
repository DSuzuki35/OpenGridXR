'use strict';
// Real sandboxed Canvas, including the compositor's toString injection contract.
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const { createHash } = require('node:crypto');

async function measure(compose, aperture, helmet, assets) {
  const width = 800, height = 450;
  const makeCanvas = (w = width, h = height) => Object.assign(document.createElement('canvas'), {width: w, height: h});
  const shell = makeCanvas(), map = makeCanvas(), ctx = map.getContext('2d');
  const pixels = ctx.createImageData(width, height);
  const makeMap = (vertical, masked = false) => {
    for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
      const at = (y * width + x) * 4, value = Math.round((vertical ? y / 449 : x / 799) * 65535);
      pixels.data[at] = value >> 8; pixels.data[at + 1] = value & 255;
      pixels.data[at + 3] = masked && x > 680 && y > 280 ? 0 : 255;
    }
    ctx.putImageData(pixels, 0, 0); return map.toDataURL();
  };
  const uvUrl = makeMap(false), uvVUrl = makeMap(true), maskedUv = makeMap(false, true);
  const art = makeCanvas(), ink = art.getContext('2d');
  for (const [color, x, y] of [['red',0,0],['lime',400,0],['blue',0,225],['rgba(255,255,0,.5)',400,225]]) {
    ink.fillStyle = color; ink.fillRect(x, y, 400, 225);
  }
  ink.clearRect(360, 190, 80, 70);
  const graphic = art.toDataURL();
  Object.assign(helmet, {opacity: 0, visorTint: 0, scale: 100, offsetX: 0, offsetY: 0});
  delete helmet.openingAngle; delete helmet.openingWidth;
  Object.assign(helmet.visor, {enabled: true, opacity: 35, height: 40, curve: 12, softness: 0,
    mirror: true, text: '', textOpacity: 100, graphic, graphicWidth: 73, graphicX: -8, graphicY: 3});
  delete helmet.visor.graphicPlacement;
  delete helmet.visor.graphicMirror; // Legacy profiles inherited the shared mirror.
  const render = async (change = {}, visorChange = {}, sources = {}, separateInk = false) => {
    const h = {...helmet, ...change, visor: {...helmet.visor, ...visorChange}};
    const output = await compose({helmet: h, shellUrl: shell.toDataURL(), uvUrl, uvVUrl,
      width, height, ...sources}, aperture);
    const img = new Image(); img.src = output.png; await img.decode();
    const canvas = makeCanvas(), paint = canvas.getContext('2d'); paint.drawImage(img, 0, 0);
    const data = paint.getImageData(0, 0, width, height).data;
    const sample = (x, y) => Array.from(data.slice((y * width + x) * 4, (y * width + x) * 4 + 4));
    let alphaSum = 0, weightedY = 0, bottomAlpha = 0;
    for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
      const a = data[(y * width + x) * 4 + 3]; alphaSum += a; weightedY += a * y;
      if (y > 290) bottomAlpha += a;
    }
    const inkParts = {};
    if (separateInk) for (const type of ['graphic','lettering']) {
      const separated = paint.createImageData(width,height);
      for (let at = 0; at < data.length; at += 4) {
        const colorful = Math.max(data[at],data[at+1],data[at+2]) - Math.min(data[at],data[at+1],data[at+2]) > 20;
        if ((type === 'graphic') === colorful) separated.data.set(data.subarray(at,at+4),at);
      }
      paint.putImageData(separated,0,0); inkParts[type] = canvas.toDataURL();
    }
    return {png: output.png, corners: [[100,100],[700,100],[100,350],[700,350]].map(([x,y]) => sample(x,y)),
      center: sample(400,225), left: sample(100,225), edge: sample(174,225),
      alphaSum, centroidY: weightedY / alphaSum, bottomAlpha, ...inkParts};
  };
  const legacy = await render();
  const legacyFitted = await render({openingAngle: 65, openingWidth: 76});
  const explicitStrip = await render({}, {graphicPlacement:'sunstrip'});
  const explicitLegacyMirror = await render({}, {graphicMirror:true});
  const legacyUnmirrored = await render({}, {mirror:false});
  const explicitUnmirrored = await render({}, {mirror:false,graphicMirror:false});
  const fullVisor = {enabled:false, graphicPlacement:'full', mirror:false, graphicMirror:false};
  const full = await render({}, fullVisor);
  const enabled = await render({}, {...fullVisor, enabled:true, opacity:0});
  const mirrored = await render({}, {...fullVisor, graphicMirror:true});
  const letteringMirrored = await render({}, {...fullVisor, mirror:true});
  const opacity = await render({}, {...fullVisor, textOpacity:40});
  const soft = await render({}, {...fullVisor, softness:4});
  const movedSliders = await render({}, {...fullVisor, graphicWidth:5, graphicX:50, graphicY:-25});
  const masked = await render({}, fullVisor, {uvUrl:maskedUv});
  const square = makeCanvas(400,400); const squareInk = square.getContext('2d');
  squareInk.fillStyle = '#ff00ff'; squareInk.fillRect(0,0,400,400);
  const contained = await render({}, {...fullVisor, graphic:square.toDataURL()});
  const softened = await render({}, {...fullVisor, graphic:square.toDataURL(), softness:4});
  const open = await render({visorPosition:'open'}, fullVisor);
  const half = await render({visorPosition:'half'}, fullVisor);
  const shaped = await render({openingAngle:65, openingWidth:76}, fullVisor);
  const textVisor = {enabled:true, graphic:'', text:'Wide GRID 012 MW', font:'arial',
    mirror:false, textCurve:0, textSize:44, textWidth:72, opacity:0, softness:0};
  const builtinText = await render({}, textVisor);
  const builtinCurvedText = await render({}, {...textVisor, textCurve:14});
  const customText = await render({}, {...textVisor, font:'custom', customFont:'Comic Sans MS'});
  const customCurvedText = await render({}, {...textVisor, font:'custom', customFont:'Comic Sans MS', textCurve:14});
  const customArial = await render({}, {...textVisor, font:'custom', customFont:'Arial'});
  const missingFont = await render({}, {...textVisor, font:'custom', customFont:'OpenGrid XR Missing Font Family 507ab1'});
  const inactiveCustom = await render({}, {...textVisor, customFont:'Impact'});
  const hostileFonts = [];
  for (const customFont of ['missing", Impact, "missing', 'missing\\", Impact, "missing']) {
    hostileFonts.push(await render({}, {...textVisor, font:'custom', customFont}));
  }
  const mirrorArt = makeCanvas(400,100), mirrorInk = mirrorArt.getContext('2d');
  mirrorInk.fillStyle = 'red'; mirrorInk.fillRect(0,0,200,100);
  mirrorInk.fillStyle = 'rgba(0,255,0,.5)'; mirrorInk.fillRect(200,0,200,100);
  mirrorInk.fillStyle = 'blue'; mirrorInk.fillRect(0,70,80,30);
  const independentMirrors = [];
  for (const graphicPlacement of ['sunstrip','full']) {
    const v = {...textVisor, graphic:mirrorArt.toDataURL(), graphicPlacement, graphicWidth:60,
      graphicX:0, graphicY:0, textSize:22, textY:-25, graphicMirror:false};
    independentMirrors.push({graphicPlacement,
      neither:await render({},v,{},true),
      graphic:await render({},{...v,graphicMirror:true},{},true),
      lettering:await render({},{...v,mirror:true},{},true)});
  }
  const actual = [];
  for (const asset of assets) {
    const common = {shellUrl:asset.shellUrl, uvUrl:asset.uvUrl, uvVUrl:asset.uvVUrl};
    const on = await render({opacity:100}, {...fullVisor, graphic:square.toDataURL()}, common);
    const off = await render({opacity:100}, {...fullVisor, graphic:''}, common);
    actual.push({style:asset.style, on, off});
  }
  return {legacy, legacyFitted, explicitStrip, explicitLegacyMirror, legacyUnmirrored, explicitUnmirrored,
    full, enabled, mirrored, letteringMirrored, independentMirrors, opacity, soft,
    movedSliders, masked, contained, softened, open, half, shaped, actual,
    builtinText,builtinCurvedText,customText,customCurvedText,customArial,missingFont,inactiveCustom,hostileFonts};
}

if (!process.versions.electron) {
  const {spawn} = require('node:child_process');
  const env = {...process.env}; delete env.ELECTRON_RUN_AS_NODE;
  const child = spawn(require('electron'), [__filename], {env, windowsHide:true, stdio:'inherit'});
  child.on('error', error => {console.error(error); process.exitCode = 1;});
  child.on('exit', code => {process.exitCode = code ?? 1;});
} else {
  const {app, BrowserWindow} = require('electron');
  app.on('window-all-closed', () => {});
  app.whenReady().then(async () => {
    const {createProfile} = await import(pathToFileURL(path.resolve(__dirname,'../src/model.mjs')).href);
    const {composeHelmetRaster, deriveHelmetAperture} = await import(pathToFileURL(path.resolve(__dirname,'../src/helmet.mjs')).href);
    const assets = await Promise.all(['formula','gt','open','level3'].map(async style => {
      const urls = await Promise.all([`${style}.png`,`${style}-mono-visor-uv.png`,`${style}-mono-visor-v.png`]
        .map(async name => `data:image/png;base64,${(await fs.readFile(path.resolve(__dirname,'../public/helmets',name))).toString('base64')}`));
      return {style,shellUrl:urls[0],uvUrl:urls[1],uvVUrl:urls[2]};
    }));
    const window = new BrowserWindow({show:false,webPreferences:{sandbox:true,contextIsolation:true,nodeIntegration:false,offscreen:true}});
    try {
      await window.loadURL('data:text/html,<title>Full visor Canvas regression</title>');
      const r = await window.webContents.executeJavaScript(`(${measure.toString()})(${composeHelmetRaster.toString()},${deriveHelmetAperture.toString()},${JSON.stringify(createProfile().helmet)},${JSON.stringify(assets)})`);
      const hash = png => createHash('sha256').update(Buffer.from(png.slice(22),'base64')).digest('hex');
      console.log('Legacy Canvas PNG hashes:', hash(r.legacy.png), hash(r.legacyFitted.png));
      // Captured before full placement was added, using the same real Canvas fixture.
      assert.equal(hash(r.legacy.png), '54bf9c413dcf1a99fd1ff6da9611596b94bd9be1e7c28c1aeed7b0052a4b6dba', 'Legacy closed strip output is byte-identical');
      assert.equal(hash(r.legacyFitted.png), 'b32d36270ca070440c9cffffe422afee504c66ced88cbb50fd083c64076e34df', 'Legacy fitted strip output is byte-identical');
      console.log('Builtin straight/curved text PNG hashes:',hash(r.builtinText.png),hash(r.builtinCurvedText.png));
      assert.equal(hash(r.builtinText.png),'b072767bf9870d28f2d1e62a26046a86bf5dab6133ab164982f7c9591da43364','Builtin text output remains byte-identical');
      assert.equal(hash(r.builtinCurvedText.png),'d21acfcba252684e7053c8e5cef2b077f6d06bd8ab2bfb5abb8855c1a9b11aa4','Builtin curved text output remains byte-identical');
      assert.notEqual(hash(r.customText.png),hash(r.builtinText.png),'Installed Comic Sans MS produces its own glyph shapes');
      assert.notEqual(hash(r.customCurvedText.png),hash(r.builtinCurvedText.png),'Curved lettering uses the custom font');
      assert.equal(hash(r.customArial.png),hash(r.builtinText.png),'A quoted installed Arial family matches builtin Arial');
      assert.equal(hash(r.missingFont.png),hash(r.builtinText.png),'A missing custom family falls back to Arial');
      assert.equal(hash(r.inactiveCustom.png),hash(r.builtinText.png),'Builtin fonts ignore the stored custom family');
      for (const rendered of r.hostileFonts) assert.equal(hash(rendered.png),hash(r.builtinText.png),'Quotes and backslashes cannot inject another CSS family');
      assert.equal(r.explicitStrip.png,r.legacy.png,'Missing placement field uses the old strip path');
      assert.equal(hash(r.explicitLegacyMirror.png),hash(r.legacy.png),'Legacy mirror true resolves to explicit graphic mirror true');
      assert.equal(hash(r.explicitUnmirrored.png),hash(r.legacyUnmirrored.png),'Legacy mirror false resolves to explicit graphic mirror false');
      assert.deepEqual(r.full.corners, [[255,0,0,255],[0,255,0,255],[0,0,255,255],[255,255,0,128]], 'Full artwork covers UV corners with strip disabled');
      assert.deepEqual(r.full.center,[0,0,0,0],'Transparent artwork stays transparent');
      assert.equal(r.enabled.png,r.full.png,'Enabling an invisible strip cannot refit or clip full graphics');
      assert.deepEqual(r.mirrored.corners, [[0,255,0,255],[255,0,0,255],[255,255,0,128],[0,0,255,255]], 'Graphic mirror flips full artwork');
      assert.equal(hash(r.letteringMirrored.png),hash(r.full.png),'Lettering mirror leaves full graphic unchanged');
      for (const {graphicPlacement,neither,graphic,lettering} of r.independentMirrors) {
        assert.notEqual(hash(graphic.graphic),hash(neither.graphic),`${graphicPlacement}: graphic mirror flips asymmetric RGBA logo`);
        assert.equal(hash(graphic.lettering),hash(neither.lettering),`${graphicPlacement}: graphic mirror leaves lettering unchanged`);
        assert.equal(hash(lettering.graphic),hash(neither.graphic),`${graphicPlacement}: lettering mirror leaves RGBA logo unchanged`);
        assert.notEqual(hash(lettering.lettering),hash(neither.lettering),`${graphicPlacement}: lettering mirror changes straight glyphs`);
      }
      assert.deepEqual(r.opacity.corners.map(p=>p[3]),[102,102,102,51],'Shared print opacity multiplies source alpha');
      assert.notEqual(r.soft.png,r.full.png,'Full artwork honors shared softness');
      assert.equal(r.movedSliders.png,r.full.png,'Full mode ignores legacy placement sliders');
      assert.equal(r.masked.corners[3][3],0,'UV alpha clips full artwork');
      assert.deepEqual(r.contained.left,[0,0,0,0],'Square artwork is centered with transparent side margins');
      assert.deepEqual(r.contained.center,[255,0,255,255],'Square artwork retains its aspect and reaches canvas center');
      assert.equal(r.contained.edge[3],0,'Contain edge remains sharp with zero softness');
      assert.ok(r.softened.edge[3]>0 && r.softened.edge[3]<255,'Softness blurs the full graphic edge');
      assert.equal(r.open.alphaSum,0,'Fully open visor hides all artwork');
      assert.ok(r.half.bottomAlpha<r.full.bottomAlpha / 10,'Half position lifts artwork away from the lower visor');
      assert.ok(r.half.centroidY<r.full.centroidY-50,'Half artwork moves upward');
      assert.ok(r.shaped.corners[2][3]>0,'Opening geometry keeps full artwork below the old strip');
      assert.notEqual(r.shaped.png,r.full.png,'Full artwork follows opening geometry');
      for (const {style,on,off} of r.actual) {
        assert.notEqual(on.png,off.png,`${style}: actual UVs expose full artwork with strip off`);
        assert.ok(on.alphaSum>off.alphaSum,`${style}: full artwork adds visible ink to the aperture`);
      }
      console.log('Full visor identity and four actual UV maps, alpha, independent graphic/lettering mirrors, contain, softness, strip independence, geometry, positions and safe installed custom font rendering passed.');
    } finally {window.destroy();}
  }).then(()=>app.exit(0),error=>{console.error(error);app.exit(1);});
}
