import { test } from "node:test";
import assert from "node:assert/strict";
import { createProfile, validateProfile, parseProfile } from "../src/model.mjs";
import { visorDefaults, visorSettings, helmetPreviewGeometry, readHelmetGraphic, validateVisor, MAX_GRAPHIC_BYTES, deriveHelmetAperture, cropForHelmetApertures, helmetPaddingPixels } from "../src/helmet.mjs";

const pixel = 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+ip1sAAAAASUVORK5CYII=';

test('graphic placement defaults old profiles to sunstrip and exchanges only supported placements', () => {
  const p = createProfile();
  assert.equal(visorSettings(p.helmet).graphicPlacement, 'sunstrip');
  delete p.helmet.visor.graphicPlacement;
  assert.doesNotThrow(() => validateProfile(p));
  assert.equal(visorSettings(p.helmet).graphicPlacement, 'sunstrip');
  for (const graphicPlacement of ['sunstrip', 'full']) {
    p.helmet.visor.graphicPlacement = graphicPlacement;
    assert.deepEqual(parseProfile(JSON.stringify({version: 1, profile: p})), p);
  }
  for (const graphicPlacement of ['', 'visor', null, 0, {}, true]) {
    p.helmet.visor.graphicPlacement = graphicPlacement;
    assert.throws(() => validateProfile(p), /placement/i);
  }
});

test('graphic mirroring is independent in new profiles and inherits legacy lettering mirroring', () => {
  const p = createProfile();
  assert.equal(p.helmet.visor.graphicMirror, true);
  p.helmet.visor.mirror = false;
  assert.equal(visorSettings(p.helmet).graphicMirror, true, 'New graphic choice stays independent');
  delete p.helmet.visor.graphicMirror;
  for (const mirror of [false, true]) {
    p.helmet.visor.mirror = mirror;
    assert.doesNotThrow(() => validateProfile(p));
    assert.equal(visorSettings(p.helmet).graphicMirror, mirror);
  }
  for (const graphicMirror of [true, false]) {
    p.helmet.visor.graphicMirror = graphicMirror;
    assert.deepEqual(parseProfile(JSON.stringify({version:1,profile:p})), p);
  }
  for (const graphicMirror of [null, 0, 1, '', 'true', [], {}]) {
    p.helmet.visor.graphicMirror = graphicMirror;
    assert.throws(() => validateProfile(p), /mirror/i);
  }
});

test('Level 3 helmet survives profile export and import without changing driving settings', () => {
  const p = createProfile();
  p.helmet.style = 'level3';
  p.helmet.openingAngle = 25;
  const restored = parseProfile(JSON.stringify({ version: 1, profile: p }));
  assert.deepEqual(restored, p);
  assert.equal(restored.helmet.openingWidth, 100);
  p.helmet.style = '../level3';
  assert.throws(() => validateProfile(p));
});

test('visor fonts allow known Windows families and preserve old profiles', () => {
  const p = createProfile();
  assert.equal(visorSettings(p.helmet).font, 'arial');
  for (const font of ['arial', 'arialNarrow', 'impact', 'georgia', 'consolas', 'trebuchet']) {
    p.helmet.visor.font = font;
    assert.deepEqual(parseProfile(JSON.stringify({version:1,profile:p})), p);
  }
  for (const font of ['unknown', 'url(https://example.com/font)', null, 12]) {
    p.helmet.visor.font=font; assert.throws(()=>validateProfile(p));
  }
  delete p.helmet.visor.font;assert.doesNotThrow(()=>validateProfile(p));
});

test('custom visor font families round trip safely while old builtins need no custom family', () => {
  const p = createProfile();
  assert.equal(visorSettings(p.helmet).customFont, '');
  delete p.helmet.visor.customFont;
  assert.doesNotThrow(() => validateProfile(p));
  assert.equal(visorSettings(p.helmet).customFont, '');
  p.helmet.visor.font = 'custom';
  assert.throws(() => validateProfile(p), /font/i, 'A custom font requires a family');
  for (const customFont of ['Comic Sans MS', 'Segoe UI', 'a', 'a'.repeat(160), 'Quoted "Font" \\ Family', 'missing", Impact, "missing']) {
    p.helmet.visor.customFont = customFont;
    assert.deepEqual(parseProfile(JSON.stringify({version:1,profile:p})), p);
  }
  for (const customFont of ['', '   ', null, 1, true, {}, 'a'.repeat(161), 'a\nb', 'a\rb', 'a\tb', 'a\0b', 'a\u007fb', 'a\u0085b', 'a\u2028b', 'a\u2029b']) {
    p.helmet.visor.customFont = customFont;
    assert.throws(() => validateProfile(p), /font/i);
  }
  p.helmet.visor.font = 'arial'; p.helmet.visor.customFont = '';
  assert.doesNotThrow(() => validateProfile(p));
  p.helmet.visor.customFont = 'bad\nfamily';
  assert.throws(() => validateProfile(p), /font/i, 'Inactive custom fields also reject controls');
});

test('vertical visor opening accepts continuous angles, preserves legacy profiles and rejects invalid values', () => {
  const p = createProfile();
  assert.doesNotThrow(() => validateProfile(p));
  for (const openingAngle of [25, 25.5, 60, 99.5, 100]) {
    p.helmet.openingAngle = openingAngle;
    assert.deepEqual(parseProfile(JSON.stringify({ version: 1, profile: p })), p);
  }
  for (const openingAngle of [24.9, 100.1, NaN, '25', null]) {
    p.helmet.openingAngle = openingAngle;
    assert.throws(() => validateProfile(p));
  }
});

test('narrow aperture crop stays within the shared 70 percent limit without clipping the aperture', () => {
  const slit = [{x: 0, y: .45}, {x: 1, y: .45}, {x: 1, y: .55}, {x: 0, y: .55}];
  const crop = cropForHelmetApertures({left: slit, right: slit});
  assert.ok(crop.top + crop.bottom <= 70);
  assert.ok(crop.top <= 45 && crop.bottom <= 45);
  assert.equal(crop.left + crop.right, 0);
});

test('interior grading and opening width validate independently of visor and scene settings', () => {
  const p = createProfile();
  assert.equal(p.helmet.openingWidth, 100);
  for (const [key, low, high] of [['shellBrightness', 0, 150], ['shellContrast', 0, 200], ['shellSaturation', 0, 200], ['openingWidth', 50, 150]]) {
    for (const value of [low, (low + high) / 2, high]) { p.helmet[key] = value; assert.doesNotThrow(() => validateProfile(p)); }
    for (const value of [low - 1, high + 1, NaN, '100']) { p.helmet[key] = value; assert.throws(() => validateProfile(p)); }
    delete p.helmet[key];
  }
});

test('helmet blur defaults to sharp and validates fractional blur independently of print softness', () => {
  const p = createProfile();
  assert.equal(p.helmet.shellBlur, 0);
  for (const shellBlur of [0, .5, 8]) {p.helmet.shellBlur = shellBlur; assert.doesNotThrow(() => validateProfile(p));}
  for (const shellBlur of [-.1, 8.1, NaN, '2']) {p.helmet.shellBlur = shellBlur; assert.throws(() => validateProfile(p));}
  delete p.helmet.shellBlur;
  assert.doesNotThrow(() => validateProfile(p));
});

test('helmet stereo alignment defaults to neutral and survives old profiles and export', () => {
  const profile = createProfile();
  assert.equal(profile.helmet.stereoOffset, 0);
  delete profile.helmet.stereoOffset;
  assert.doesNotThrow(() => validateProfile(profile));
  for (const value of [-5, -0.1, 0, 0.1, 5]) {
    profile.helmet.stereoOffset = value;
    assert.deepEqual(parseProfile(JSON.stringify({ version: 1, profile })), profile);
  }
  for (const value of [-5.1, 5.1, NaN, '1']) {
    profile.helmet.stereoOffset = value;
    assert.throws(() => validateProfile(profile), /stereo|Non-finite/i);
  }
});

test('crop fitting reserves either direction of stereo offset in addition to motion', () => {
  const base = helmetPaddingPixels({}, 3, 1000);
  for (const stereoOffset of [-5, 5]) {
    assert.equal(helmetPaddingPixels({ stereoOffset }, 3, 1000), base + 50);
    assert.equal(helmetPaddingPixels({ stereoOffset, headMotion: 1 }, 3, 1000), base + 80);
  }
});

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
  pixels[(4 * width + 50) * 4 + 3] = 254;
  assert.ok(Math.min(...deriveHelmetAperture(pixels, width, height, 0).map(p => p.y)) <= 4 / height, 'Even one alpha step of transparency must retain the scene');
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
  for(const key of ['visorContrast','visorBrightness','visorSaturation']) assert.equal(Object.hasOwn(profile.helmet,key),false);
  assert.equal(visorSettings(profile.helmet).mirror, true);
  for (const change of [{textWidth: 110}, {textCurve: -26}, {textCurve: 26}, {textCurve: NaN}, {textX: -60}, {textY: 30}, {textOpacity: 101}, {softness: 9}]) {
    const p = createProfile(); Object.assign(p.helmet.visor, change);
    assert.throws(() => validateProfile(p));
  }
  for (const change of [{visorContrast: 151}, {visorBrightness: 49}, {visorSaturation: -1}, {visorTintColor: 'red'}]) {
    const p = createProfile(); Object.assign(p.helmet, change);
    assert.throws(() => validateProfile(p));
  }
});

test('text curvature is optional in old profiles and survives profile exchange in either direction', () => {
  const profile = createProfile();
  delete profile.helmet.visor.textCurve;
  assert.doesNotThrow(() => validateProfile(profile));
  assert.equal(visorSettings(profile.helmet).textCurve, 0);
  for (const textCurve of [-25, 0, 25]) {
    profile.helmet.visor.textCurve = textCurve;
    assert.deepEqual(parseProfile(JSON.stringify({ version: 1, profile })), profile);
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
  p.stream.preserveFullFrame = true;
  assert.deepEqual(helmetPreviewGeometry(p).crop, { x: 0, y: 0, width: 800, height: 450 });
  assert.equal(helmetPreviewGeometry(p).contour, null);
});
