'use strict';
const fs = require('node:fs/promises');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const { createHash, randomUUID } = require('node:crypto');
const { selectedForGame } = require('./runtime-config.cjs');
const { atomicWrite } = require('./store.cjs');
const cache = new Map();
const preparations = new Map();
const MAX_PNG_BYTES = 32 * 1024 * 1024;
const HTML = '<!doctype html><meta charset="utf-8"><meta http-equiv="Content-Security-Policy" content="default-src \'none\'; img-src data:; script-src \'none\'; style-src \'none\'; connect-src \'none\'"><title>OpenGrid XR helmet preparation</title>';

function pngDimensions(bytes) {
  if (!Buffer.isBuffer(bytes) || bytes.length < 33 || bytes.length > MAX_PNG_BYTES || !bytes.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10])) || bytes.toString('ascii', 12, 16) !== 'IHDR')
    throw new Error('Helmet image is not a supported PNG.');
  const width = bytes.readUInt32BE(16), height = bytes.readUInt32BE(20);
  if (!width || !height || width > 4096 || height > 4096 || bytes[24] !== 8 || bytes[25] !== 6)
    throw new Error('Helmet image must be 8-bit RGBA and at most 4096 × 4096.');
  return { width, height };
}

async function limitedPng(file) {
  if ((await fs.stat(file)).size > MAX_PNG_BYTES) throw new Error('Helmet image is too large.');
  const bytes = await fs.readFile(file); pngDimensions(bytes); return bytes;
}

async function writeSidecar(file, config) {
  const text = JSON.stringify(config, null, 2) + '\n';
  if (await fs.readFile(file, 'utf8').catch(() => '') !== text) await atomicWrite(file, text);
}

/** Prepare fixed per-game, per-eye assets after a validated profile save.
 * runtimeDir: existing installed layer directory. assetDir: bundled helmets.
 * BrowserWindow: Electron constructor supplied by main, never loaded in Node tests.
 * Expensive rendering is cached by appearance and source assets, not run per frame.
 */
async function prepare(document, { runtimeDir, assetDir, BrowserWindow }) {
  if (!path.isAbsolute(runtimeDir) || !path.isAbsolute(assetDir)) throw new Error('Helmet directories must be absolute.');
  const { validateDocument } = await import(pathToFileURL(path.join(__dirname, '../src/model.mjs')).href);
  const { composeHelmetRaster, deriveHelmetAperture, visorSettings } = await import(pathToFileURL(path.join(__dirname, '../src/helmet.mjs')).href);
  validateDocument(document);
  await fs.mkdir(runtimeDir, { recursive: true });
  const result = { games: [], contours: {} };
  let window;
  const createRenderer = async () => {
    if (window) return window;
    window = new BrowserWindow({
      show: false, width: 1, height: 1, skipTaskbar: true,
      webPreferences: { sandbox: true, contextIsolation: true, nodeIntegration: false, webSecurity: true, offscreen: true, backgroundThrottling: false, partition: `helmet-bake-${randomUUID()}` },
    });
    window.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
    window.webContents.on('will-navigate', event => event.preventDefault());
    window.webContents.session.setPermissionRequestHandler((_contents, _permission, callback) => callback(false));
    window.webContents.session.setPermissionCheckHandler?.(() => false);
    window.webContents.session.webRequest.onBeforeRequest({ urls: ['http://*/*', 'https://*/*', 'file://*/*', 'ftp://*/*'] }, (_details, callback) => callback({ cancel: true }));
    await window.loadURL(`data:text/html;charset=utf-8,${encodeURIComponent(HTML)}`);
    return window;
  };
  try {
    for (const game of ['iracing', 'lmu']) {
      const profile = selectedForGame(document, game);
      const enabled = Boolean(profile?.helmet.enabled);
      const names = { leftImage: `OpenGridXR-helmet-${game}-left.png`, rightImage: `OpenGridXR-helmet-${game}-right.png` };
      const paths = { leftPath: path.join(runtimeDir, names.leftImage), rightPath: path.join(runtimeDir, names.rightImage), sidecarPath: path.join(runtimeDir, `OpenGridXR-helmet-${game}.json`) };
      const config = { version: 1, generation: randomUUID(), enabled, ...names, headMotion: profile?.helmet.headMotion ?? 0, vehicleMotion: profile?.helmet.vehicleMotion ?? 0 };
      const key = `${runtimeDir}:${game}`;
      result.games.push({ game, enabled, ...paths });
      if (!enabled) {
        await writeSidecar(paths.sidecarPath, config); cache.delete(key); continue;
      }
      const helmet = { ...profile.helmet, visor: visorSettings(profile.helmet) };
      try {
        const sourceFiles = ['left', 'right'].map(eye => [path.join(assetDir, `${helmet.style}-${eye}.png`), path.join(assetDir, `${helmet.style}-${eye}-visor-uv.png`), path.join(assetDir, `${helmet.style}-${eye}-visor-v.png`)]);
        const stamps = await Promise.all(sourceFiles.flat().map(async file => { const stat = await fs.stat(file); return [stat.size, stat.mtimeMs]; }));
        const fingerprint = createHash('sha256').update(JSON.stringify({ helmet, padding: profile.mask.padding, stamps })).digest('hex');
        const cached = cache.get(key);
        if (cached?.fingerprint === fingerprint && (await Promise.all([paths.leftPath, paths.rightPath].map(file => fs.stat(file).then(s => s.size > 0).catch(() => false)))).every(Boolean)) {
          config.generation = cached.generation;
          await writeSidecar(paths.sidecarPath, config); result.contours[game] = cached.contours; continue;
        }
        const renderer = await createRenderer();
        const contours = {}, outputs = [];
        for (const [index, eye] of ['left', 'right'].entries()) {
          const [source, uv, uvV] = await Promise.all(sourceFiles[index].map(limitedPng));
          const motionMargin = config.headMotion > 0 || config.vehicleMotion > 0 ? .03 : 0;
          const payload = { helmet, shellUrl: `data:image/png;base64,${source.toString('base64')}`, uvUrl: `data:image/png;base64,${uv.toString('base64')}`, uvVUrl: `data:image/png;base64,${uvV.toString('base64')}`, paddingPixels: 4 + (profile.mask.padding / 100 + motionMargin) * 1920, width: 1920, height: 1080 };
          // JSON is passed as an expression to trusted code, never inserted into HTML.
          const code = `(${composeHelmetRaster.toString()})(${JSON.stringify(payload)}, ${deriveHelmetAperture.toString()})`;
          let timer;
          const rendered = await Promise.race([
            renderer.webContents.executeJavaScript(code),
            new Promise((_resolve, reject) => { timer = setTimeout(() => reject(new Error('Helmet image preparation timed out.')), 30000); }),
          ]).finally(() => clearTimeout(timer));
          if (typeof rendered?.png !== 'string' || !rendered.png.startsWith('data:image/png;base64,') || rendered.png.length > MAX_PNG_BYTES * 1.4)
            throw new Error('Helmet renderer returned an invalid image.');
          const png = Buffer.from(rendered.png.slice(22), 'base64');
          const dimensions = pngDimensions(png);
          if (dimensions.width !== 1920 || dimensions.height !== 1080) throw new Error('Helmet renderer returned unexpected dimensions.');
          outputs.push(png); contours[eye] = rendered.contour;
        }
        // Never let the native reader observe an enabled, half-updated eye pair.
        await writeSidecar(paths.sidecarPath, { ...config, enabled: false });
        await atomicWrite(paths.leftPath, outputs[0]);
        await atomicWrite(paths.rightPath, outputs[1]);
        await writeSidecar(paths.sidecarPath, config);
        cache.set(key, { fingerprint, contours, generation: config.generation }); result.contours[game] = contours;
      } catch (error) {
        cache.delete(key);
        await writeSidecar(paths.sidecarPath, { ...config, enabled: false });
        throw error;
      }
    }
    return result;
  } finally {
    if (window && !window.isDestroyed()) window.destroy();
  }
}

function prepareHelmetAssets(document, options) {
  const key = options.runtimeDir;
  const previous = preparations.get(key) ?? Promise.resolve();
  const snapshot = structuredClone(document);
  const pending = previous.catch(() => {}).then(() => prepare(snapshot, options));
  preparations.set(key, pending);
  const clear = () => { if (preparations.get(key) === pending) preparations.delete(key); };
  pending.then(clear, clear);
  return pending;
}

module.exports = { prepareHelmetAssets, pngDimensions };
