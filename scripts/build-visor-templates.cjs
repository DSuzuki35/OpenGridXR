'use strict';
// Rebuild all registered 1600x900 authoring files with:
//   node scripts/build-visor-templates.cjs
// Electron supplies PNG decoding and the isolated Canvas renderer; ag-psd uses
// raw ImageData, so this intentionally requires no native node-canvas module.
const fs = require('node:fs/promises');
const path = require('node:path');
const { spawn } = require('node:child_process');
const ROOT = path.resolve(__dirname, '..');
const OUT = path.join(ROOT, 'public/templates/visor');
const WIDTH = 1600, HEIGHT = 900;
const STYLES = ['formula', 'gt', 'open', 'level3'];

// Runs only in the sandboxed offscreen renderer. Every contour comes from the
// real UV samples supplied by main; there is no hand-drawn visor silhouette.
function drawAuthoringLayers(style, coverageBase64) {
  const w = 1600, h = 900;
  const canvas = () => { const c = document.createElement('canvas'); c.width = w; c.height = h; return c; };
  const bytes = Uint8Array.from(atob(coverageBase64), char => char.charCodeAt(0));
  const visibility = canvas(), visible = visibility.getContext('2d'), pixels = visible.createImageData(w, h);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const at = y * w + x; if (!bytes[at]) continue;
    const boundary = x === 0 || y === 0 || x === w - 1 || y === h - 1 || !bytes[at - 1] || !bytes[at + 1] || !bytes[at - w] || !bytes[at + w];
    pixels.data.set([69, 218, 192, boundary ? 230 : 64], at * 4);
  }
  visible.putImageData(pixels, 0, 0);

  const reference = canvas(), ref = reference.getContext('2d');
  ref.fillStyle = '#101a29'; ref.fillRect(0, 0, w, h);
  ref.fillStyle = '#edf6ff'; ref.font = '700 28px Arial'; ref.fillText(`OPEN GRID  /  ${style.toUpperCase()} VISOR`, 44, 53);
  ref.fillStyle = '#a9bed2'; ref.font = '18px Arial'; ref.fillText('1600 × 900 UV artwork canvas  •  Outside-print orientation  •  Mirror graphic ON in OpenGrid XR', 44, 85);
  ref.fillStyle = '#45dac0'; ref.font = '700 17px Arial'; ref.fillText('MINT = sampled UV visible through the default closed helmet', 44, 110);
  ref.fillStyle = '#dbe8f5'; ref.font = '17px Arial'; ref.fillText('Reference only: fitting, crop, opening settings and head position change what you see.', 44, 855);
  ref.fillStyle = '#ffcd79'; ref.fillText('DO NOT UPLOAD THIS GUIDE. Export only Artwork as transparent PNG. Keep the driving view clear.', 44, 880);
  const grid = canvas(), g = grid.getContext('2d');
  g.strokeStyle = '#66849a65'; g.lineWidth = 1;
  for (let x = 100; x < w; x += 100) { g.beginPath(); g.moveTo(x + .5, 110); g.lineTo(x + .5, 748); g.stroke(); }
  for (let y = 150; y < 750; y += 100) { g.beginPath(); g.moveTo(24, y + .5); g.lineTo(1576, y + .5); g.stroke(); }
  g.strokeStyle = '#dcecffc0'; g.setLineDash([8, 8]); g.lineWidth = 2;
  g.beginPath(); g.moveTo(800, 110); g.lineTo(800, 748); g.moveTo(24, 450); g.lineTo(1576, 450); g.stroke(); g.setLineDash([]);
  g.fillStyle = '#dcecff'; g.font = '16px Arial'; g.fillText('CENTER 800,450', 818, 440);
  g.fillText('ARTWORK LEFT', 44, 143); g.textAlign = 'right'; g.fillText('ARTWORK RIGHT', 1556, 143); g.textAlign = 'left';
  g.font = '14px Arial';
  for (let x = 200; x < w; x += 200) g.fillText(String(x), x + 6, 740);
  const guide = canvas(), merged = guide.getContext('2d');
  merged.drawImage(reference, 0, 0); merged.drawImage(visibility, 0, 0); merged.drawImage(grid, 0, 0);

  const example = canvas(), art = example.getContext('2d');
  // Generic peripheral example; no opaque plate and a large clear center.
  art.fillStyle = '#45dac0';
  art.beginPath(); art.moveTo(400, 265); art.lineTo(525, 265); art.lineTo(555, 280); art.lineTo(375, 280); art.fill();
  art.beginPath(); art.moveTo(1200, 265); art.lineTo(1075, 265); art.lineTo(1045, 280); art.lineTo(1225, 280); art.fill();
  art.fillStyle = '#f3f7fb'; art.font = '900 58px Arial'; art.textAlign = 'center'; art.fillText('OPEN GRID', 800, 286);
  art.font = '700 17px Arial'; art.fillText('SIM RACING  /  XR', 800, 306);
  for (const side of [-1, 1]) {
    art.save(); art.translate(side === -1 ? 0 : 1600, 0); art.scale(side === -1 ? 1 : -1, 1);
    art.fillStyle = '#45dac0'; art.fillRect(265, 360, 12, 190); art.fillRect(295, 385, 7, 140);
    art.fillStyle = '#f3f7fb'; art.beginPath(); art.moveTo(310, 575); art.lineTo(385, 575); art.lineTo(415, 555); art.lineTo(335, 555); art.fill();
    art.restore();
  }
  const result = Object.fromEntries(Object.entries({ reference, visibility, grid, guide, example, blank: canvas() }).map(([key, value]) => [key, value.toDataURL('image/png')]));
  // Return unpremultiplied RGBA for PSD layers directly from Canvas. Decoding a
  // nativeImage bitmap can premultiply translucent guide colors on Windows.
  for (const [key, value] of Object.entries({ reference, visibility, grid })) {
    const data = value.getContext('2d').getImageData(0, 0, w, h).data;
    let binary = '';
    for (let at = 0; at < data.length; at += 32768) binary += String.fromCharCode(...data.subarray(at, at + 32768));
    result[`${key}RGBA`] = btoa(binary);
  }
  return result;
}

function coverageFromNativeMaps(u, v, shell, width, height) {
  const samples = Buffer.alloc(WIDTH * HEIGHT);
  for (let at = 0; at < width * height * 4; at += 4) {
    // BGRA bitmap; only fully opaque map samples preserve both encoded bytes.
    // The closed shell is drawn last in the compositor and hides these pixels.
    if (u[at + 3] !== 255 || v[at + 3] !== 255 || shell[at + 3] >= 255) continue;
    const x = WIDTH - 1 - Math.round((u[at + 2] * 256 + u[at + 1]) / 65535 * (WIDTH - 1));
    const y = Math.round((v[at + 2] * 256 + v[at + 1]) / 65535 * (HEIGHT - 1));
    samples[y * WIDTH + x] = 255;
  }
  // One authoring pixel covers raster sampling gaps, not a guessed silhouette.
  const filled = Buffer.from(samples);
  for (let y = 1; y < HEIGHT - 1; y++) for (let x = 1; x < WIDTH - 1; x++) {
    const at = y * WIDTH + x; if (!samples[at]) continue;
    for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) filled[at + dy * WIDTH + dx] = 255;
  }
  return filled;
}

async function build() {
  const { app, BrowserWindow, nativeImage } = require('electron');
  const { writePsdBuffer, initializeCanvas } = require('ag-psd');
  initializeCanvas(() => { throw new Error('Only raw ImageData is supported by this generator'); }, (width, height) => ({ width, height, data: new Uint8ClampedArray(width * height * 4) }));
  await app.whenReady();
  await fs.mkdir(OUT, { recursive: true });
  const win = new BrowserWindow({ show: false, webPreferences: { sandbox: true, contextIsolation: true, nodeIntegration: false, offscreen: true } });
  try {
    await win.loadURL('data:text/html,<meta charset="utf-8"><title>Visor template asset generator</title>');
    const blank = { width: WIDTH, height: HEIGHT, data: new Uint8ClampedArray(WIDTH * HEIGHT * 4) };
    const rawImageData = base64 => ({ width: WIDTH, height: HEIGHT, data: new Uint8ClampedArray(Buffer.from(base64, 'base64')) });
    const writePng = (name, url) => fs.writeFile(path.join(OUT, name), Buffer.from(url.split(',')[1], 'base64'));
    for (const style of STYLES) {
      const images = [`${style}-mono-visor-uv.png`, `${style}-mono-visor-v.png`, `${style}.png`].map(file => nativeImage.createFromPath(path.join(ROOT, 'public/helmets', file)));
      const { width, height } = images[0].getSize();
      if (!width || !height || images.some(image => image.getSize().width !== width || image.getSize().height !== height)) throw new Error(`Mismatched or missing real UV assets: ${style}`);
      const coverage = coverageFromNativeMaps(...images.map(image => image.toBitmap()), width, height);
      const layers = await win.webContents.executeJavaScript(`(${drawAuthoringLayers.toString()})(${JSON.stringify(style)}, ${JSON.stringify(coverage.toString('base64'))})`);
      await writePng(`visor-guide-${style}.png`, layers.guide);
      if (style === STYLES[0]) { await writePng('visor-template.png', layers.blank); await writePng('generic-example.png', layers.example); }
      const psd = {
        width: WIDTH, height: HEIGHT, imageData: blank,
        imageResources: {
          resolutionInfo: { horizontalResolution: 72, horizontalResolutionUnit: 'PPI', widthUnit: 'Inches', verticalResolution: 72, verticalResolutionUnit: 'PPI', heightUnit: 'Inches' },
          gridAndGuidesInformation: { grid: { horizontal: 3200, vertical: 3200 }, guides: [{ location: 800, direction: 'vertical' }, { location: 450, direction: 'horizontal' }] },
        },
        children: [
          { name: 'REFERENCE — legend / outside print / mirror ON — DO NOT EXPORT', hidden: true, imageData: rawImageData(layers.referenceRGBA) },
          { name: 'Visibility — default CLOSED fit / mirror ON — DO NOT EXPORT', hidden: true, imageData: rawImageData(layers.visibilityRGBA) },
          { name: 'GUIDES — UV grid + center — DO NOT EXPORT', hidden: true, imageData: rawImageData(layers.gridRGBA) },
          { name: 'Artwork — export this layer only', hidden: false, imageData: blank },
        ],
      };
      await fs.writeFile(path.join(OUT, `visor-template-${style}.psd`), writePsdBuffer(psd, { generateThumbnail: false, trimImageData: false }));
      console.log(`${style}: registered ${WIDTH}x${HEIGHT} PSD + PNG guide from ${width}x${height} closed-shell maps`);
    }
  } finally { win.destroy(); }
}

if (!process.versions.electron) {
  const env = { ...process.env }; delete env.ELECTRON_RUN_AS_NODE;
  const child = spawn(require('electron'), [__filename], { cwd: ROOT, env, windowsHide: true, stdio: 'inherit' });
  child.on('error', error => { console.error(error); process.exitCode = 1; });
  child.on('exit', code => { process.exitCode = code ?? 1; });
} else {
  const { app } = require('electron');
  app.on('window-all-closed', () => {});
  build().then(() => app.exit(0), error => { console.error(error); app.exit(1); });
}
