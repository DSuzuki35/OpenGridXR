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

/** Prepare flat visor images per game after a validated profile save.
 * Both eye filenames contain identical bytes; v2 optionally adds live variants.
 * runtimeDir: existing installed layer directory. assetDir: bundled helmets.
 * BrowserWindow: Electron constructor supplied by main, never loaded in Node tests.
 * Expensive rendering is cached by appearance and source assets, not run per frame.
 */
async function prepare(document, { runtimeDir, assetDir, BrowserWindow }, stream = false) {
  if (!path.isAbsolute(runtimeDir) || !path.isAbsolute(assetDir)) throw new Error('Helmet directories must be absolute.');
  const { validateDocument } = await import(pathToFileURL(path.join(__dirname, '../src/model.mjs')).href);
  const { composeHelmetRaster, deriveHelmetAperture, visorSettings, helmetPaddingPixels, helmetRasterSize, streamHelmetAppearance } = await import(pathToFileURL(path.join(__dirname, '../src/helmet.mjs')).href);
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
      const selected = selectedForGame(document, game);
      const profile = stream && selected ? { ...selected, helmet: streamHelmetAppearance(selected) } : selected;
      const enabled = Boolean(profile?.helmet.enabled);
      const visorHotkey = profile?.helmet.visorHotkey ?? false;
      const prefix = stream ? 'OpenGridXR-stream-helmet' : 'OpenGridXR-helmet';
      const names = { leftImage: `${prefix}-${game}-left.png`, rightImage: `${prefix}-${game}-right.png` };
      if (visorHotkey) Object.assign(names, { halfImage: `OpenGridXR-helmet-${game}-half.png`, openImage: `OpenGridXR-helmet-${game}-open.png` });
      const paths = Object.fromEntries(Object.entries(names).map(([field, name]) => [field.replace('Image', 'Path'), path.join(runtimeDir, name)]));
      paths.sidecarPath = path.join(runtimeDir, `${prefix}-${game}.json`);
      const config = { version: 2, generation: randomUUID(), enabled, ...names, visorHotkey, visorPosition: profile?.helmet.visorPosition ?? 'closed', stereoOffset: profile?.helmet.stereoOffset ?? 0, headMotion: profile?.helmet.headMotion ?? 0, vehicleMotion: profile?.helmet.vehicleMotion ?? 0 };
      if (stream) config.streamOpacity = selected?.stream.helmet?.opacity ?? 100;
      const key = `${runtimeDir}:${prefix}:${game}`;
      result.games.push({ game, enabled, ...paths });
      if (!enabled) {
        await writeSidecar(paths.sidecarPath, config); cache.delete(key); continue;
      }
      const helmet = { ...profile.helmet, visor: visorSettings(profile.helmet) };
      try {
        const { width, height } = helmetRasterSize(helmet);
        const paddingPixels = helmetPaddingPixels(helmet, stream ? 0 : profile.mask.padding, width);
        const contourPosition = visorHotkey ? 'open' : config.visorPosition;
        const sourceFiles = [path.join(assetDir, `${helmet.style}.png`), path.join(assetDir, `${helmet.style}-mono-visor-uv.png`), path.join(assetDir, `${helmet.style}-mono-visor-v.png`)];
        const stamps = await Promise.all(sourceFiles.map(async file => { const stat = await fs.stat(file); return [file, stat.size, stat.mtimeMs]; }));
        // Scene grading and tracking operate in native code, not on these PNGs.
        // Padding can change the contour without replacing every visor variant.
        const appearance = { ...helmet, visorPosition: visorHotkey ? 'closed' : config.visorPosition };
        for (const field of ['headMotion', 'vehicleMotion', 'stereoOffset', 'visorBrightness', 'visorContrast', 'visorSaturation']) delete appearance[field];
        const fingerprint = createHash('sha256').update(JSON.stringify({ representation: 'flat-2d-v3', helmet: appearance, width, height, stamps })).digest('hex');
        const cached = cache.get(key);
        const requiredPaths = Object.keys(names).map(field => paths[field.replace('Image', 'Path')]);
        const imagesCached = cached?.fingerprint === fingerprint && (await Promise.all(requiredPaths.map(async file => {
          try {
            const bytes = await limitedPng(file), dimensions = pngDimensions(bytes);
            return dimensions.width === width && dimensions.height === height && createHash('sha256').update(bytes).digest('hex') === cached.hashes[file];
          } catch { return false; }
        }))).every(Boolean);
        if (imagesCached && cached.paddingPixels === paddingPixels) {
          config.generation = cached.generation;
          await writeSidecar(paths.sidecarPath, config); result.contours[game] = cached.contours; continue;
        }
        const renderer = await createRenderer();
        const [source, uv, uvV] = await Promise.all(sourceFiles.map(limitedPng));
        const payload = { helmet, shellUrl: `data:image/png;base64,${source.toString('base64')}`, uvUrl: `data:image/png;base64,${uv.toString('base64')}`, uvVUrl: `data:image/png;base64,${uvV.toString('base64')}`, paddingPixels, width, height };
        const render = async (position, apertureOnly = false) => {
          const input = { ...payload, helmet: { ...helmet, visorPosition: position }, apertureOnly };
          // JSON is passed as an expression to trusted code, never inserted into HTML.
          const code = `(${composeHelmetRaster.toString()})(${JSON.stringify(input)}, ${deriveHelmetAperture.toString()})`;
          let timer;
          return Promise.race([
            renderer.webContents.executeJavaScript(code),
            new Promise((_resolve, reject) => { timer = setTimeout(() => reject(new Error('Helmet image preparation timed out.')), 30000); }),
          ]).finally(() => clearTimeout(timer));
        };
        if (imagesCached) {
          const rendered = await render(contourPosition, true);
          const contours = { left: rendered.contour, right: structuredClone(rendered.contour) };
          config.generation = cached.generation;
          await writeSidecar(paths.sidecarPath, config);
          cache.set(key, { ...cached, contours, paddingPixels }); result.contours[game] = contours; continue;
        }
        const variants = visorHotkey ? ['closed', 'half', 'open'] : [config.visorPosition];
        const outputs = new Map();
        let contours;
        for (const position of variants) {
          const rendered = await render(position);
          if (typeof rendered?.png !== 'string' || !rendered.png.startsWith('data:image/png;base64,') || rendered.png.length > MAX_PNG_BYTES * 1.4)
            throw new Error('Helmet renderer returned an invalid image.');
          const png = Buffer.from(rendered.png.slice(22), 'base64');
          const dimensions = pngDimensions(png);
          if (dimensions.width !== width || dimensions.height !== height) throw new Error('Helmet renderer returned unexpected dimensions.');
          outputs.set(position, png);
          if (position === contourPosition) contours = { left: rendered.contour, right: structuredClone(rendered.contour) };
        }
        // Never let the native reader observe an enabled, half-updated generation.
        await writeSidecar(paths.sidecarPath, { ...config, enabled: false });
        const images = [[paths.leftPath, outputs.get(variants[0])], [paths.rightPath, outputs.get(variants[0])]];
        if (visorHotkey) images.push([paths.halfPath, outputs.get('half')], [paths.openPath, outputs.get('open')]);
        const hashes = {};
        for (const [file, png] of images) {
          await atomicWrite(file, png);
          hashes[file] = createHash('sha256').update(png).digest('hex');
        }
        await writeSidecar(paths.sidecarPath, config);
        cache.set(key, { fingerprint, contours, paddingPixels, hashes, generation: config.generation }); result.contours[game] = contours;
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
  const pending = previous.catch(() => {}).then(async () => {
    // Separate namespaces and appearances keep stream transforms out of the
    // headset images and, especially, its mask aperture. Attempt both paths so
    // an unrelated headset failure cannot leave an old stream manifest enabled.
    const results = await Promise.allSettled([prepare(snapshot, options), prepare(snapshot, options, true)]);
    const failed = results.find(result => result.status === 'rejected');
    if (failed) throw failed.reason;
    return { ...results[0].value, streamGames: results[1].value.games };
  });
  preparations.set(key, pending);
  const clear = () => { if (preparations.get(key) === pending) preparations.delete(key); };
  pending.then(clear, clear);
  return pending;
}

module.exports = { prepareHelmetAssets, pngDimensions };
