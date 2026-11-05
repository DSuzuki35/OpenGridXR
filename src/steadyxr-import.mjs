import { createProfile, validateProfile } from './model.mjs';

const MAX_BYTES = 1024 * 1024;
const edges = ['left', 'right', 'top', 'bottom'];
const title = s => s[0].toUpperCase() + s.slice(1);

export function decodeSteadyXrPreset(bytes) {
  const data = bytes instanceof ArrayBuffer ? new Uint8Array(bytes)
    : ArrayBuffer.isView(bytes) ? new Uint8Array(bytes.buffer, bytes.byteOffset, bytes.byteLength) : null;
  if (!data) throw new Error('Preset must contain file bytes.');
  if (data.byteLength > MAX_BYTES) throw new Error('Preset exceeds the 1 MiB size limit.');
  let encoding = 'utf-8';
  if (data[0] === 255 && data[1] === 254) encoding = 'utf-16le';
  if (data[0] === 254 && data[1] === 255) encoding = 'utf-16be';
  try { return new TextDecoder(encoding, { fatal: true }).decode(data); }
  catch { throw new Error('Preset has invalid UTF-8 or BOM-marked UTF-16 encoding.'); }
}

// Windows INI lookup is case-insensitive and returns the first matching key/section.
// Maps keep even untrusted prototype-like names inert; no INI key becomes a profile key.
function parseIni(text, warnings) {
  if (typeof text !== 'string') throw new Error('Preset must be INI text.');
  if (text.length > MAX_BYTES || new TextEncoder().encode(text).length > MAX_BYTES)
    throw new Error('Preset exceeds the 1 MiB size limit.');
  text = text.replace(/^\uFEFF/, '');
  if (/[\u0000-\u0008\u000b\u000c\u000e-\u001f\u007f]/.test(text))
    throw new Error('Preset contains binary or control characters.');
  const sections = new Map();
  let section;
  for (const [index, raw] of text.split(/\r\n|\r|\n/).entries()) {
    const line = raw.trim();
    if (!line || line.startsWith(';') || line.startsWith('#')) continue;
    const heading = /^\[([^\[\]]+)\]\s*(?:;.*)?$/.exec(line);
    if (heading) {
      const name = heading[1].trim().toLowerCase();
      if (!name) throw new Error(`Invalid INI section at line ${index + 1}.`);
      if (sections.has(name)) {
        warnings.push(`Duplicate [${name}] section skipped; first section wins.`);
        section = new Map(); // Parse but never import a later duplicate section.
      } else { section = new Map(); sections.set(name, section); }
      continue;
    }
    const equal = line.indexOf('=');
    if (line.startsWith('[') || !section || equal < 1)
      throw new Error(`Malformed INI at line ${index + 1}.`);
    const key = line.slice(0, equal).trim().toLowerCase();
    let value = line.slice(equal + 1).trim();
    if (!key) throw new Error(`Missing INI key at line ${index + 1}.`);
    if ((value.startsWith('"') && value.endsWith('"')) || (value.startsWith("'") && value.endsWith("'")))
      value = value.slice(1, -1);
    if (section.has(key)) warnings.push(`Duplicate ${key} skipped; first value wins.`);
    else section.set(key, value);
  }
  if (!sections.has('preview')) throw new Error('Not a steadyXR preset: missing [Preview] section.');
  return sections;
}

/** Convert only supported steadyXR controls. Missing keys follow steadyXR's read() defaults. */
export function importSteadyXrPreset(text, { game = 'iracing', name = 'steadyXR import' } = {}) {
  const warnings = [], imported = [], sections = parseIni(text, warnings), used = new Set();
  const preview = sections.get('preview');
  // Require a recognisable setting as well as the section, so unrelated INI cannot silently import defaults.
  const signature = /^(smoothing(tenths)?|crop(tenths)?|eye|stabilize|horizontenths|pitchtenths|adaptivehorizon|mirror|outputwidth|outputheight|fps)$/;
  if (![...preview.keys()].some(k => signature.test(k))) throw new Error('No recognised steadyXR preview settings.');
  const has = (key, section = 'preview') => sections.get(section)?.has(key.toLowerCase()) ?? false;
  const read = (key, section = 'preview') => {
    used.add(`${section}/${key.toLowerCase()}`);
    return sections.get(section)?.get(key.toLowerCase());
  };
  const clamp = (n, lo, hi, label) => {
    const bounded = Math.min(hi, Math.max(lo, n));
    if (bounded !== n) warnings.push(`${label} clamped from ${n} to ${bounded}.`);
    return bounded;
  };
  const number = (key, fallback, lo, hi, section = 'preview', integer = true) => {
    const raw = read(key, section);
    if (raw === undefined) return fallback;
    const syntax = integer ? /^[+-]?\d+$/ : /^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?$/i;
    if (!syntax.test(raw))
      throw new Error(`Invalid numeric value for [${section}] ${key}.`);
    const n = Number(raw);
    if (!Number.isFinite(n) || (integer && !Number.isInteger(n)))
      throw new Error(`Invalid finite ${integer ? 'integer ' : ''}value for [${section}] ${key}.`);
    return clamp(n, lo, hi, `[${section}] ${key}`);
  };
  const bool = (key, fallback, section = 'preview') => number(key, fallback ? 1 : 0, 0, 1, section) !== 0;
  const tenth = (key, fallback, lo, hi, section = 'preview', integer = true) =>
    number(key, fallback * 10, lo * 10, hi * 10, section, integer) / 10;
  const profile = createProfile(game, name), s = profile.stream;
  profile.stream.preserveFullFrame = true;
  s.smoothing = tenth('SmoothingTenths', number('Smoothing', 40, 0, 100), 0, 100);
  const crop = tenth('CropTenths', number('Crop', 10, 0, 40), 0, 40);
  s.horizonLock = tenth('HorizonTenths', 0, 0, 100);
  s.pitchLock = tenth('PitchTenths', 0, 0, 100);
  s.adaptiveHorizon = bool('AdaptiveHorizon', true);
  if (!bool('Stabilize', true)) {
    s.smoothing = s.horizonLock = s.pitchLock = 0;
    warnings.push('Stabilize is disabled: smoothing, horizon lock and pitch lock imported as zero.');
  }
  s.eye = number('Eye', 0, 0, 1);
  s.mirror = bool('Mirror', true);
  s.mirrorLimit = tenth('MirrorLimitTenths', 10, 0, 50);
  s.mirrorSoftness = tenth('MirrorSoftnessTenths', 60, 0, 100);
  s.mirrorStretch = tenth('MirrorStretchTenths', 0, 0, 100);
  s.offsetX = tenth('HorizontalOffsetTenths', 0, -100, 100);
  s.offsetY = tenth('VerticalOffsetTenths', 0, -100, 100);
  imported.push('Smoothing, horizon lock, adaptive horizon, pitch lock and stabilization state.',
    'Selected eye, mirrored borders (limit, softness, stretch) and horizontal/vertical framing.');

  for (const edge of edges) profile.view.crop[edge] = clamp(tenth(`Crop${title(edge)}Tenths`, 0, 0, 45), 0, 40, `Crop ${edge}`);
  if (Object.values(profile.view.crop).some(value => value > 0))
    warnings.push('Crop values are imported as input trims. steadyXR trims after stabilization reserve, so framing, available stabilization area and reflected borders can differ. Review the output after importing.');
  for (const [a,b] of [['left','right'],['top','bottom']]) {
    const sum = profile.view.crop[a] + profile.view.crop[b];
    if (sum > 70) {
      profile.view.crop[a] *= 70 / sum;
      profile.view.crop[b] = 70 - profile.view.crop[a];
      warnings.push(`Crop ${a}/${b} reduced proportionally to retain at least 30% of the image.`);
    }
  }
  const eyeNames = ['LeftEye', 'RightEye'];
  const reserves = eyeNames.map(eye => {
    const section = `${eye}Reserve`.toLowerCase();
    const enabled = bool('Enabled', false, section);
    return Object.fromEntries(edges.map(edge => {
      // steadyXR rounds the fallback half-crop to tenths before reading per-eye controls.
      const stored = tenth(`${title(edge)}Tenths`, Math.round(crop * 5) / 10, 0, 35, section);
      return [edge, enabled ? stored : crop / 2];
    }));
  });
  s.reserve = Object.fromEntries(edges.map(edge => [edge, clamp(reserves[s.eye][edge], 0, 30, `Selected-eye reserve ${edge}`)]));
  const renderEnabled = bool('Enabled', false, 'render');
  const symmetric = bool('Symmetric', false, 'render');
  const expansions = eyeNames.map(eye => ['Left','Right'].map(side =>
    tenth(`${eye}${side}Tenths`, 0, 0, 30, 'render', false)));
  [s.extraLeft, s.extraRight] = renderEnabled ? expansions[s.eye] : [0, 0];
  s.extraSymmetric = symmetric && s.extraLeft === s.extraRight;
  if (symmetric && s.extraLeft !== s.extraRight)
    warnings.push('Render Symmetric is an editing preference; unequal stored extra areas preserved with symmetry disabled.');
  if (JSON.stringify(reserves[0]) !== JSON.stringify(reserves[1]) || (renderEnabled && JSON.stringify(expansions[0]) !== JSON.stringify(expansions[1])))
    warnings.push(`Only the ${s.eye ? 'right' : 'left'} eye reserve and extra area were imported; the other eye configuration is not retained.`);
  imported.push('Crop values and selected-eye stabilization reserve.', 'Selected-eye Render extra area (zero when Render is disabled).');

  s.width = clamp(number('OutputWidth', 3840, 320, 7680), 640, 7680, 'OutputWidth');
  s.height = clamp(number('OutputHeight', 2160, 180, 4320), 360, 4320, 'OutputHeight');
  s.fps = number('FPS', 60, 30, 90);
  if (![30,60,90].includes(s.fps)) { warnings.push(`FPS ${s.fps} converted to 60, matching steadyXR's fallback.`); s.fps = 60; }
  s.fullscreen = bool('AutoFullscreen', true);
  let aspect = number('AspectNumerator', 16, 1, 10000) / number('AspectDenominator', 9, 1, 10000);
  if (aspect < .25 || aspect > 4) { warnings.push('Invalid source aspect ratio reset to 16:9, matching steadyXR.'); aspect = 16 / 9; }
  if (Math.abs(aspect - s.width / s.height) > 1e-6)
    warnings.push(`Source aspect ratio ${aspect.toFixed(4)} differs from output ${s.width}×${s.height}; OpenGrid uses the output dimensions for framing.`);
  imported.push('Output resolution, frame rate and fullscreen preference. Missing controls use steadyXR defaults.');
  if (number('Mode', 0, 0, 1) === 0)
    warnings.push('Window capture mode is unsupported; the new profile uses the selected game’s OpenXR eye.');
  for (const key of ['Monitor','SourceExecutable','SourceTitle']) {
    if (has(key) && read(key)) warnings.push(`${key} binding skipped; choose the OpenGrid output display/game independently.`);
  }
  if (has('HotkeyProfile')) { read('HotkeyProfile'); warnings.push('Hotkey profile skipped; steadyXR shortcuts are not imported.'); }
  const unsupported = /^(spectator|director|visual|overlay|compare)/;
  const active = /(?:enabled|exposure|adaptation|bloom|color|mode|effects)$/;
  for (const [key,value] of preview) {
    if (unsupported.test(key) && active.test(key) && value !== '' && value !== '0')
      warnings.push(`Unsupported setting ${key}=${value.slice(0,80)} skipped (spectator/director/visual/overlay/comparison modules are not imported).`);
  }
  const skipped = [];
  for (const [section, fields] of sections) for (const key of fields.keys())
    if (!used.has(`${section}/${key}`)) skipped.push(`[${section}] ${key}`);
  if (skipped.length) warnings.push(`${skipped.length} unknown or unsupported setting(s) skipped: ${skipped.slice(0,12).join(', ')}${skipped.length > 12 ? ', …' : ''}.`);
  validateProfile(profile);
  return { profile, imported, warnings };
}
