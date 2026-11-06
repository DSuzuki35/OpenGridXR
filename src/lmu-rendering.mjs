export const lmuRenderingDefaults = () => ({
  mode:'off', provider:'native', gaze:'auto', peripheralScale:25, focusScale:100,
  focusWidth:50, focusHeight:50, focusOffsetX:0, focusOffsetY:0,
  peripheralShadingRate:2, nativeOuterResolution:true, debugFocus:false,
  cpuUploadReuse:false, cpuUploadBatch:false, cpuOuterReuse:false, outerSsr:false,
});
const constrainedQuad = value => value.mode==='quad'&&['qvfr','vectorxr'].includes(value.provider);
const fitsFocus = value => value.peripheralScale<=value.focusScale*Math.min(value.focusWidth,value.focusHeight)/100;
function minimumFactor(peripheral,other) {
  const candidate=Math.floor(peripheral*100/other);
  // Check the same forward inequality as validation: ceil(34.2*100/38)
  // rounds to 91 even though 38*90/100 is exactly the valid 34.2 boundary.
  return candidate*other/100>=peripheral?candidate:candidate+1;
}
export function lmuRenderingLimits(value) {
  if(!constrainedQuad(value))return null;
  const focusMinimum=minimumFactor(value.peripheralScale,value.focusScale);
  const preserveCurrent=fitsFocus(value);
  const minimum=(key,bound)=>preserveCurrent?Math.min(value[key],bound):bound;
  const peripheralMaximum=Math.min(400,Math.floor(value.focusScale*Math.min(value.focusWidth,value.focusHeight)/100));
  return {
    peripheralScale:{min:10,max:preserveCurrent?Math.max(value.peripheralScale,peripheralMaximum):peripheralMaximum},
    focusScale:{min:minimum('focusScale',Math.max(25,minimumFactor(value.peripheralScale,Math.min(value.focusWidth,value.focusHeight)))),max:400},
    focusWidth:{min:minimum('focusWidth',Math.max(10,focusMinimum)),max:90},
    focusHeight:{min:minimum('focusHeight',Math.max(10,focusMinimum)),max:90},
  };
}
export function updateLmuRendering(value,patch) {
  const next={...value,...patch};
  delete next.outerSmoothing;
  if(patch.cpuUploadBatch===true){next.cpuUploadReuse=false;next.cpuOuterReuse=false;}
  if(patch.cpuUploadReuse===true||patch.cpuOuterReuse===true)next.cpuUploadBatch=false;
  if(patch.cpuOuterReuse===true)next.cpuUploadReuse=true;
  if(patch.cpuUploadReuse===false)next.cpuOuterReuse=false;
  if(next.nativeOuterResolution===false)next.outerSsr=false;
  if(!constrainedQuad(next))return next;
  if(Object.hasOwn(patch,'mode')||Object.hasOwn(patch,'provider')){
    if(fitsFocus(next))return next;
    // Inactive preferences may be incompatible. Preserve focus geometry and
    // lower peripheral resolution first; raise focus only to fit the 10% floor.
    const section=Math.min(next.focusWidth,next.focusHeight);
    next.focusScale=Math.max(next.focusScale,minimumFactor(10,section));
    next.peripheralScale=Math.min(next.peripheralScale,Math.floor(next.focusScale*section/100));
  }else{
    const limits=lmuRenderingLimits(value);
    for(const key of Object.keys(patch))if(limits?.[key])
      next[key]=Math.max(limits[key].min,Math.min(limits[key].max,patch[key]));
  }
  return next;
}
export function validateLmuRendering(value, game='lmu') {
  if(value===undefined)return;
  if(!value||typeof value!=='object'||Array.isArray(value))throw new Error('Invalid LMU rendering settings.');
  const defaults=lmuRenderingDefaults();
  for(const key of Object.keys(value))if(key!=='outerSmoothing'&&!Object.hasOwn(defaults,key))throw new Error(`Unsupported LMU rendering setting: ${key}.`);
  for(const [key,choices] of Object.entries({mode:['off','quad','stereoVrs'],provider:['native','qvfr','vectorxr'],gaze:['auto','fixed'],peripheralShadingRate:[1,2,4]}))
    if(!choices.includes(value[key]))throw new Error(`Unsupported LMU ${key}.`);
  if(game!=='lmu'&&value.mode!=='off')throw new Error('Quad views and stereo VRS require an LMU profile.');
  for(const [key,min,max] of [['peripheralScale',10,400],['focusScale',25,400],['focusWidth',10,90],['focusHeight',10,90],['focusOffsetX',-50,50],['focusOffsetY',-50,50]])
    if(!Number.isFinite(value[key])||value[key]<min||value[key]>max)throw new Error(`LMU ${key} must be between ${min} and ${max}.`);
  for(const key of ['nativeOuterResolution','debugFocus'])if(typeof value[key]!=='boolean')throw new Error(`LMU ${key} must be true or false.`);
  for(const key of ['cpuUploadReuse','cpuUploadBatch','cpuOuterReuse','outerSsr'])if(value[key]!==undefined&&typeof value[key]!=='boolean')throw new Error(`LMU ${key} must be true or false.`);
  if(value.outerSmoothing!==undefined&&(!Number.isFinite(value.outerSmoothing)||value.outerSmoothing<0||value.outerSmoothing>3))throw new Error('Invalid legacy LMU outerSmoothing.');
  if(constrainedQuad(value)&&!fitsFocus(value))
    throw new Error(`${value.provider==='qvfr'?'QVFR':'VectorXR'} outer image must fit inside the focus buffer. Reduce peripheral scale or increase focus scale / width / height.`);
}
