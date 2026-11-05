import {test} from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {createDocument,createProfile,validateProfile,parseProfile,effectiveMask,pixelBudget} from '../src/model.mjs';
import {helmetPreviewGeometry} from '../src/helmet.mjs';
const {toRuntimeIni,toEnhancementsConfig}=createRequire(import.meta.url)('../desktop/runtime-config.cjs');
test('Disable crop is a persisted optional boolean and preserves saved geometry',()=>{
 const p=createProfile('lmu');assert.equal(p.view.disableCrop,false);delete p.view.disableCrop;assert.doesNotThrow(()=>validateProfile(p));
 p.view.disableCrop='true';assert.throws(()=>validateProfile(p),/Disable crop/i);
 p.view.disableCrop=true;p.view.crop={left:4,right:7,top:8,bottom:21};p.mask.enabled=true;
 assert.deepEqual(parseProfile(JSON.stringify(p)),p);assert.equal(effectiveMask(p),false);assert.equal(pixelBudget(p).retainedPercent,100);
 const preview=helmetPreviewGeometry(p);assert.deepEqual(preview.crop,{x:0,y:0,width:800,height:450});assert.equal(preview.contour,null);
});
test('Disable crop bypasses rectangular crop and mask for stereo and all LMU providers without changing preferences',()=>{
 for(const [game,mode,provider] of [['iracing','off','native'],['lmu','off','native'],['lmu','stereoVrs','native'],['lmu','quad','native'],['lmu','quad','qvfr'],['lmu','quad','vectorxr']]){
  const doc=createDocument(),p=doc.profiles.find(p=>p.game===game);doc.selectedProfileId=p.id;p.view.disableCrop=true;p.view.crop={left:4,right:7,top:8,bottom:21};p.mask.enabled=true;p.helmet.enabled=true;p.colorCorrection.enabled=true;p.lmuRendering.mode=mode;p.lmuRendering.provider=provider;
  const original=structuredClone(doc),ini=toRuntimeIni(doc).split(`[${game}]`)[1].split('\n\n')[0];
  for(const key of ['CropLeft','CropRight','CropTop','CropBottom','MaskEnabled'])assert.match(ini,new RegExp(`^${key}=0$`,'m'),`${game}/${mode}/${provider}/${key}`);
  assert.match(ini,/^ColorCorrectionEnabled=1$/m);assert.match(toEnhancementsConfig(doc,game).core.osd.toolkitSummary,/Crop disabled/);
  assert.deepEqual(doc,original);
  p.view.disableCrop=false;const restored=toRuntimeIni(doc).split(`[${game}]`)[1].split('\n\n')[0];assert.match(restored,/^CropTop=8$/m);assert.match(restored,/^CropBottom=21$/m);assert.match(restored,/^MaskEnabled=1$/m);
 }
});
test('Disable crop keeps independent stream framing and stabilization preferences intact',()=>{
 const doc=createDocument(),p=doc.profiles[1];doc.selectedProfileId=p.id;p.stream.preserveFullFrame = true;p.view.disableCrop=true;p.view.crop.top=20;p.stream.offsetX=12;p.stream.extraLeft=18;p.stream.reserve.top=9;
 const ini=toRuntimeIni(doc).split('[lmu]')[1];for(const line of ['CropTop=0','OffsetX=12','ExtraLeft=18','ReserveTop=9'])assert.ok(ini.includes(line));
 assert.equal(p.view.crop.top,20);assert.equal(pixelBudget(p).renderedPercent,118);
});
