import { test } from "node:test";
import assert from "node:assert/strict";
import { createProfile, validateProfile, parseProfile } from "../src/model.mjs";
import { visorDefaults, visorSettings, helmetPreviewGeometry, readHelmetGraphic, validateVisor, MAX_GRAPHIC_BYTES, deriveHelmetAperture, cropForHelmetApertures } from "../src/helmet.mjs";

const pixel = 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+ip1sAAAAASUVORK5CYII=';

test('old helmet profiles remain valid and new profiles have independent visor settings', () => {
  const old = createProfile();
  delete old.helmet.visor;
  assert.doesNotThrow(() => validateProfile(old));
  assert.equal(visorSettings(old.helmet).enabled, false);
  const first = createProfile(), second = createProfile();
  first.helmet.visor.text = 'RACING';
  assert.notEqual(second.helmet.visor.text, first.helmet.visor.text);
  assert.equal(first.helmet.enabled, false);
});

test('derived aperture contains all nonopaque pixels and respects motion margin', () => {
  const width = 100, height = 80, pixels = new Uint8ClampedArray(width * height * 4);
  for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) pixels[(y * width + x) * 4 + 3] = x > 14 && x < 85 && y > 12 && y < 68 ? 0 : 255;
  const points = deriveHelmetAperture(pixels, width, height, 3);
  assert.ok(points.length >= 4 && points.length <= 64);
  assert.ok(Math.min(...points.map(p => p.x)) <= .12);
  assert.ok(Math.max(...points.map(p => p.x)) >= .88);
  const crop = cropForHelmetApertures({ left: points, right: points });
  assert.ok(crop.left <= 12 && crop.right <= 12 && crop.top <= 12);
  pixels.fill(0);
  assert.deepEqual(cropForHelmetApertures({ left: deriveHelmetAperture(pixels, width, height, 3), right: points }), {left: 0, right: 0, top: 0, bottom: 0});
});

test('motion settings are optional for old profiles and constrained to normalized strengths', () => {
  const p = createProfile();
  assert.equal(p.helmet.headMotion, 0);
  assert.equal(p.helmet.vehicleMotion, 0);
  p.helmet.headMotion = .4;
  p.helmet.vehicleMotion = .25;
  assert.doesNotThrow(() => validateProfile(p));
  for (const value of [-1, 1.1, '0.5']) {
    p.helmet.headMotion = value;
    assert.throws(() => validateProfile(p), /motion/i);
  }
});

test('curved visor typography and real scene-grade controls are bounded and backward compatible', () => {
  const profile = createProfile();
  assert.equal(profile.helmet.visorContrast, 100);
  assert.equal(profile.helmet.visorBrightness, 100);
  assert.equal(profile.helmet.visorSaturation, 100);
  assert.equal(visorSettings(profile.helmet).mirror, true);
  for (const change of [{textWidth: 110}, {textX: -60}, {textY: 30}, {textOpacity: 101}, {softness: 9}]) {
    const p = createProfile(); Object.assign(p.helmet.visor, change);
    assert.throws(() => validateProfile(p));
  }
  for (const change of [{visorContrast: 151}, {visorBrightness: 49}, {visorSaturation: -1}, {visorTintColor: 'red'}]) {
    const p = createProfile(); Object.assign(p.helmet, change);
    assert.throws(() => validateProfile(p));
  }
});

test('upload rejects vector markup disguised with a raster MIME type before decoding', async () => {
  await assert.rejects(readHelmetGraphic(new File(['<svg xmlns="http://www.w3.org/2000/svg"/>'], 'logo.png', { type: 'image/png' })), /contents do not match/);
  await assert.rejects(readHelmetGraphic(new File(['anything'], 'logo.svg', { type: 'image/svg+xml' })), /Choose a PNG/);
});

test('visor artwork, text and crop survive profile exchange', () => {
  const p = createProfile();
  p.helmet.visor = { ...visorDefaults(), enabled: true, text: 'TEAM <ONE>', graphic: pixel, mirror: true, graphicX: 12 };
  p.view.crop.top = 12;
  assert.deepEqual(parseProfile(JSON.stringify({ version: 1, profile: p })), p);
});

test('invalid imported visor data and oversized assets are rejected', () => {
  for (const change of [
    { color: 'url(https://example.org)' }, { opacity: 101 }, { curve: NaN },
    { text: 'a'.repeat(81) }, { text: 'line\nbreak' }, { mirror: 'yes' },
    { graphic: 'data:image/svg+xml;base64,PHN2Zz4=' }, { graphic: 'https://example.org/logo.png' },
    { graphic: 'data:image/png;base64,aGVsbG8=' }, { graphic: pixel.slice(0, -8) },
    { graphic: pixel.replace('O+ip1s', 'O+jRZk') },
    { graphic: 'data:image/png;base64,' + 'A'.repeat(MAX_GRAPHIC_BYTES * 2) },
    { graphicWidth: 101 }, { textSize: 0 }, { unknown: 'a'.repeat(1000) },
  ]) {
    const p = createProfile();
    p.helmet.visor = { ...visorDefaults(), ...change };
    assert.throws(() => validateProfile(p), undefined, JSON.stringify(change).slice(0, 100));
  }
  assert.doesNotThrow(() => validateVisor({ ...visorDefaults(), graphic: pixel }));
});

test('preview uses the real per-eye contour and crop, and disables driving crop in streaming', () => {
  const p = createProfile();
  p.mask.enabled = true;
  p.view.crop = { left: 10, right: 20, top: 5, bottom: 15 };
  const geometry = helmetPreviewGeometry(p, 'right');
  assert.deepEqual(geometry.crop, { x: 80, y: 22.5, width: 560, height: 360 });
  assert.equal(geometry.contour, p.mask.right.map(({x,y}) => `${x*800},${y*450}`).join(' '));
  p.mask.enabled = false;
  assert.equal(helmetPreviewGeometry(p).contour, null);
  p.mode = 'streaming';
  assert.deepEqual(helmetPreviewGeometry(p).crop, { x: 0, y: 0, width: 800, height: 450 });
  assert.equal(helmetPreviewGeometry(p).contour, null);
});
