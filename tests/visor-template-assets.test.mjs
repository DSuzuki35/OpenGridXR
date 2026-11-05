import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import test from 'node:test';

const require = createRequire(import.meta.url);
const { initializeCanvas, readPsd } = require('ag-psd');
initializeCanvas(() => { throw new Error('Tests require raw ImageData, not node-canvas'); }, (width, height) => ({ width, height, data: new Uint8ClampedArray(width * height * 4) }));
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const assets = path.join(root, 'public/templates/visor');
const styles = ['formula', 'gt', 'open', 'level3'];
const read = style => readPsd(fs.readFileSync(path.join(assets, `visor-template-${style}.psd`)), { useImageData: true, skipThumbnail: true });
const alphaCount = image => {
  let count = 0;
  for (let at = 3; at < image.data.length; at += 4) if (image.data[at]) count++;
  return count;
};

if (process.versions.electron) {
  const { app, nativeImage } = require('electron');
  app.whenReady().then(() => {
    const blank = nativeImage.createFromPath(path.join(assets, 'visor-template.png'));
    assert.deepEqual(blank.getSize(), { width: 1600, height: 900 });
    assert.equal(alphaCount({ data: blank.toBitmap() }), 0, 'The upload canvas must contain no printed guides/background');
    const example = nativeImage.createFromPath(path.join(assets, 'generic-example.png'));
    const examplePixels = example.toBitmap();
    assert.deepEqual(example.getSize(), { width: 1600, height: 900 });
    assert.ok(alphaCount({ data: examplePixels }) > 10000, 'Example must contain actual artwork');
    for (let y = 310; y < 650; y++) for (let x = 450; x < 1150; x++) assert.equal(examplePixels[(y * 1600 + x) * 4 + 3], 0, 'Example center stays clear');
    for (const style of styles) {
      const psd = read(style);
      const visibility = psd.children.find(layer => layer.name.startsWith('Visibility'));
      const uv = nativeImage.createFromPath(path.join(root, `public/helmets/${style}-mono-visor-uv.png`));
      const vv = nativeImage.createFromPath(path.join(root, `public/helmets/${style}-mono-visor-v.png`));
      const shell = nativeImage.createFromPath(path.join(root, `public/helmets/${style}.png`));
      assert.deepEqual(uv.getSize(), vv.getSize()); assert.deepEqual(uv.getSize(), shell.getSize());
      const u = uv.toBitmap(), v = vv.toBitmap(), base = shell.toBitmap();
      const sourceHits = new Uint8Array(1600 * 900);
      for (let at = 0; at < u.length; at += 4) {
        if (u[at + 3] !== 255 || v[at + 3] !== 255 || base[at + 3] === 255) continue;
        const x = 1599 - Math.round((u[at + 2] * 256 + u[at + 1]) / 65535 * 1599);
        const y = Math.round((v[at + 2] * 256 + v[at + 1]) / 65535 * 899);
        sourceHits[y * 1600 + x] = 1;
      }
      let checked = 0;
      // Independent reverse check: real opaque map samples unobscured by the shell
      // must land on the supplied UV visibility reference. RG encodes 16-bit U/V;
      // nativeImage bitmaps are BGRA, hence R at +2 and G at +1.
      for (let at = 0; at < u.length; at += 4 * 137) {
        if (u[at + 3] !== 255 || v[at + 3] !== 255 || base[at + 3] !== 0) continue;
        const x = 1599 - Math.round((u[at + 2] * 256 + u[at + 1]) / 65535 * 1599);
        const y = Math.round((v[at + 2] * 256 + v[at + 1]) / 65535 * 899);
        const target = ((y - visibility.top) * visibility.imageData.width + x - visibility.left) * 4 + 3;
        assert.ok(visibility.imageData.data[target] > 0, `${style}: guide must correspond to actual visible UV sample ${x},${y}`);
        checked++;
      }
      assert.ok(checked > 100, `${style}: exercise real map pixels`);
      for (let y = 0; y < 900; y += 7) for (let x = 0; x < 1600; x += 7) {
        const index = ((y - visibility.top) * visibility.imageData.width + x - visibility.left) * 4 + 3;
        if (!visibility.imageData.data[index]) continue;
        let hasSource = false;
        for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) {
          if (x + dx >= 0 && x + dx < 1600 && y + dy >= 0 && y + dy < 900 && sourceHits[(y + dy) * 1600 + x + dx]) hasSource = true;
        }
        assert.ok(hasSource, `${style}: visibility at ${x},${y} must come from a real UV/shell sample, allowing one-pixel raster gap fill`);
      }
      const guide = nativeImage.createFromPath(path.join(assets, `visor-guide-${style}.png`));
      assert.deepEqual(guide.getSize(), { width: 1600, height: 900 });
      assert.equal(alphaCount(psd.imageData), 0, 'PSD default composite is export-safe with every reference hidden');
      const merged = new Uint8ClampedArray(1600 * 900 * 4);
      for (const prefix of ['REFERENCE', 'Visibility', 'GUIDES']) {
        const image = psd.children.find(layer => layer.name.startsWith(prefix)).imageData;
        for (let at = 0; at < merged.length; at += 4) {
          const alpha = image.data[at + 3] / 255;
          for (let channel = 0; channel < 3; channel++) merged[at + channel] = image.data[at + channel] * alpha + merged[at + channel] * (1 - alpha);
          merged[at + 3] = image.data[at + 3] + merged[at + 3] * (1 - alpha);
        }
      }
      const actualGuide = guide.toBitmap(), bgra = Buffer.alloc(merged.length);
      for (let at = 0; at < merged.length; at += 4) {
        bgra[at] = merged[at + 2]; bgra[at + 1] = merged[at + 1]; bgra[at + 2] = merged[at]; bgra[at + 3] = merged[at + 3];
        for (let channel = 0; channel < 4; channel++) assert.ok(Math.abs(bgra[at + channel] - actualGuide[at + channel]) <= 2, `${style}: editable PSD references must recompose to the PNG guide`);
      }
      if (process.env.VISOR_TEMPLATE_PREVIEW && style === 'formula') fs.writeFileSync(path.join(root, '.local/visor-template-psd-preview.png'), nativeImage.createFromBitmap(bgra, { width: 1600, height: 900 }).toPNG());
    }
    console.log('Real PNG transparency, clear example center, PSD composite and all four UV/shell correspondence checks passed.');
    app.exit(0);
  }).catch(error => { console.error(error); app.exit(1); });
} else {
  test('authoring PSDs retain blank editable artwork and hidden export-safe references', () => {
    for (const style of styles) {
      const file = path.join(assets, `visor-template-${style}.psd`);
      assert.ok(fs.existsSync(file), `Missing authoring template: ${style}`);
      const psd = read(style);
      assert.equal(psd.width, 1600); assert.equal(psd.height, 900);
      const artwork = psd.children.find(layer => layer.name === 'Artwork — export this layer only');
      assert.ok(artwork?.imageData, `${style}: actual editable raster layer`);
      assert.equal(artwork.hidden, false); assert.equal(alphaCount(artwork.imageData), 0);
      for (const prefix of ['GUIDES', 'Visibility', 'REFERENCE']) {
        const layer = psd.children.find(item => item.name.startsWith(prefix));
        assert.ok(layer?.imageData, `${style}: separate ${prefix} pixels`);
        assert.match(layer.name, /DO NOT EXPORT/); assert.equal(layer.hidden, true);
        assert.ok(alphaCount(layer.imageData) > 100, `${style}: useful reference data`);
      }
      assert.ok(psd.imageResources.gridAndGuidesInformation.guides.some(guide => guide.direction === 'vertical' && guide.location === 800));
      assert.ok(psd.imageResources.gridAndGuidesInformation.guides.some(guide => guide.direction === 'horizontal' && guide.location === 450));
    }
  });
  test('PNG assets and visibility references agree with actual closed helmet projection maps', () => {
    assert.ok(fs.existsSync(path.join(assets, 'visor-template.png')), 'Missing blank PNG');
    const env = { ...process.env }; delete env.ELECTRON_RUN_AS_NODE;
    const child = spawnSync(require('electron'), [fileURLToPath(import.meta.url)], { cwd: root, env, windowsHide: true, encoding: 'utf8', timeout: 120000 });
    assert.equal(child.status, 0, child.stdout + child.stderr);
  });
}
