import { test } from 'node:test';
import assert from 'node:assert/strict';
import { importSteadyXrPreset, decodeSteadyXrPreset } from '../src/steadyxr-import.mjs';
import { validateProfile, createProfile } from '../src/model.mjs';

const full = `[Preview]
Smoothing=40
SmoothingTenths=635
Crop=10
CropTenths=125
CropLeftTenths=25
CropRightTenths=35
CropTopTenths=45
CropBottomTenths=55
HorizonTenths=800
PitchTenths=275
AdaptiveHorizon=1
Stabilize=1
Mirror=1
MirrorLimitTenths=120
MirrorSoftnessTenths=650
MirrorStretchTenths=225
Eye=1
HorizontalOffsetTenths=-350
VerticalOffsetTenths=225
OutputWidth=2560
OutputHeight=1440
AspectNumerator=16
AspectDenominator=9
FPS=90
AutoFullscreen=0
Mode=1
[LeftEyeReserve]
Enabled=1
LeftTenths=300
[RightEyeReserve]
Enabled=1
LeftTenths=25
RightTenths=100
TopTenths=50
BottomTenths=75
[Render]
Enabled=1
Symmetric=0
LeftEyeLeftTenths=200
LeftEyeRightTenths=0
RightEyeLeftTenths=75
RightEyeRightTenths=125
`;

test('imports representative saved preset into a fresh validated streaming profile', () => {
  const existing = createProfile('lmu');
  const before = structuredClone(existing);
  const {profile:p, imported, warnings} = importSteadyXrPreset(full, {game:'lmu', name:'My import'});
  validateProfile(p);
  assert.equal(p.game, 'lmu'); assert.equal(p.stream.preserveFullFrame, true); assert.equal(Object.hasOwn(p,'mode'),false); assert.equal(p.name, 'My import');
  assert.notEqual(p.id, existing.id); assert.deepEqual(existing, before);
  assert.deepEqual(p.view.crop, {left:2.5,right:3.5,top:4.5,bottom:5.5});
  assert.deepEqual(p.stream.reserve, {left:2.5,right:10,top:5,bottom:7.5});
  for (const [key,value] of Object.entries({smoothing:63.5,horizonLock:80,pitchLock:27.5,adaptiveHorizon:true,
    mirror:true,mirrorLimit:12,mirrorSoftness:65,mirrorStretch:22.5,eye:1,offsetX:-35,offsetY:22.5,
    width:2560,height:1440,fps:90,fullscreen:false,extraLeft:7.5,extraRight:12.5,extraSymmetric:false}))
    assert.equal(p.stream[key], value, key);
  assert.ok(imported.some(x => /reserve/i.test(x)));
  assert.ok(warnings.some(x => /other eye/i.test(x)));
  assert.ok(warnings.some(x => /input trims.*framing.*stabilization area/i.test(x)), 'Different crop/reserve semantics are reported');
  assert.equal(p.helmet.enabled, false);
});

test('legacy integer fallback and absent fields use steadyXR loading defaults', () => {
  const {profile:p} = importSteadyXrPreset('[Preview]\nSmoothing=73\nCrop=18');
  assert.equal(p.stream.smoothing,73);
  assert.deepEqual(p.stream.reserve,{left:9,right:9,top:9,bottom:9});
  assert.equal(p.stream.eye,0); assert.equal(p.stream.width,3840); assert.equal(p.stream.height,2160);
  assert.equal(p.stream.adaptiveHorizon,true); assert.equal(p.stream.mirrorSoftness,60);
  assert.equal(p.stream.extraLeft,0); assert.equal(p.stream.extraRight,0);
});

test('disabled stabilization clears effective smoothing, horizon, and pitch while retaining framing', () => {
  const {profile:p,warnings} = importSteadyXrPreset(full.replace('Stabilize=1','Stabilize=0'));
  assert.equal(p.stream.smoothing,0); assert.equal(p.stream.horizonLock,0); assert.equal(p.stream.pitchLock,0);
  assert.equal(p.stream.offsetX,-35); assert.equal(p.stream.mirror,true);
  assert.ok(warnings.some(x => /stabiliz/i.test(x)));
});

test('clamps with explicit reports, preserves minimum cropped area and matches source FPS fallback', () => {
  const {profile:p,warnings} = importSteadyXrPreset(`[Preview]\nEye=0\nSmoothingTenths=1200\nCropLeftTenths=450\nCropRightTenths=450\nOutputWidth=320\nOutputHeight=180\nFPS=48\nAspectNumerator=4\nAspectDenominator=3\n[LeftEyeReserve]\nEnabled=1\nLeftTenths=350`);
  validateProfile(p); assert.equal(p.stream.smoothing,100); assert.equal(p.stream.reserve.left,30);
  assert.equal(p.view.crop.left+p.view.crop.right,70); assert.equal(p.stream.width,640); assert.equal(p.stream.height,360);
  assert.equal(p.stream.fps,60);
  for(const term of ['SmoothingTenths','reserve','crop','OutputWidth','OutputHeight','FPS','aspect'])
    assert.ok(warnings.some(x=>x.toLowerCase().includes(term.toLowerCase())),term);
});

test('disabled per-eye reserve and render ignore stored values; asymmetric amounts override editing preference', () => {
  const {profile:p} = importSteadyXrPreset(full.replace('Eye=1','Eye=0').replace('[LeftEyeReserve]\nEnabled=1','[LeftEyeReserve]\nEnabled=0').replace('[Render]\nEnabled=1','[Render]\nEnabled=0'));
  assert.equal(p.stream.reserve.left,6.25); assert.equal(p.stream.extraLeft,0);
  const q=importSteadyXrPreset(full.replace('Symmetric=0','Symmetric=1'));
  assert.equal(q.profile.stream.extraSymmetric,false);
  assert.ok(q.warnings.some(x=>/symmetric/i.test(x)));
});

test('bindings, active unsupported modules and unknown fields are reported without copying paths or keys', () => {
  const data='[Preview]\nSmoothing=40\nMonitor=\\\\.\\DISPLAY2\nSourceExecutable=C:\\untrusted.exe\nSourceTitle=Window\nMode=0\nSpectatorGazeEnabled=1\nDirectorAutoCropEnabled=1\nVisualBloom=1\nOverlayMode=2\nHotkeyProfile=2\nFutureOption=on\n__proto__=polluted\nconstructor=unsafe\n[__proto__]\npolluted=yes';
  const {profile:p,warnings}=importSteadyXrPreset(data);
  assert.equal({}.polluted,undefined); assert.equal(p.stream.displayId,'primary');
  assert.equal(JSON.stringify(p).includes('untrusted.exe'),false);
  for(const term of ['Monitor','Source','window','Spectator','Director','Visual','Overlay','Hotkey','skipped'])
    assert.ok(warnings.some(x=>x.toLowerCase().includes(term.toLowerCase())),term);
});

test('case insensitive Windows-style first occurrence and quoted values, with duplicate reports', () => {
  const {profile:p,warnings}=importSteadyXrPreset('; comment\r\n[pReViEw] ; section\r\n smoothing = "21"\r\nSMOOTHING=90\r\n[PREVIEW]\r\nSmoothing=80');
  assert.equal(p.stream.smoothing,21); assert.ok(warnings.some(x=>/duplicate/i.test(x)));
});

test('rejects malformed, non-INI, oversized, binary and non-finite mapped input', () => {
  for(const s of ['', '{}','[Other]\nSmoothing=40','[Preview]\nUnknown=1','[Preview\nSmoothing=40','[Preview]\nbroken','x=1\n[Preview]\nSmoothing=40','[Preview]\nSmoothing=NaN','[Preview]\nSmoothing=Infinity','[Preview]\nSmoothing=12junk','[Preview]\nSmoothing=1e999','[Preview]\nSmoothing=2e1','[Preview]\nEye=0.5','[Preview]\nSmoothing=40\0'])
    assert.throws(()=>importSteadyXrPreset(s),undefined,s);
  assert.throws(()=>importSteadyXrPreset('[Preview]\nSmoothing=40\n;'+ 'é'.repeat(600000)),/large|MiB/i);
});

test('strictly decodes UTF-8 and BOM UTF-16 in both byte orders, rejects invalid bytes and overlimit', () => {
  const text='[Preview]\nSmoothing=53\nSourceTitle=Rennstrecke 🏎';
  const le=Buffer.concat([Buffer.from([255,254]),Buffer.from(text,'utf16le')]);
  const be=Buffer.from(le); for(let i=0;i<be.length;i+=2) [be[i],be[i+1]]=[be[i+1],be[i]];
  for(const b of [Buffer.from(text),Buffer.concat([Buffer.from([239,187,191]),Buffer.from(text)]),le,be]) {
    assert.equal(decodeSteadyXrPreset(b),text); assert.equal(importSteadyXrPreset(decodeSteadyXrPreset(b)).profile.stream.smoothing,53);
  }
  for(const b of [Buffer.from([0xff]),Buffer.from([255,254,0]),Buffer.from([255,254,0,216]),Buffer.alloc(1048577)])
    assert.throws(()=>decodeSteadyXrPreset(b));
});
