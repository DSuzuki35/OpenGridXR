import defaults from './color-correction-defaults.json' with {type:'json'};

export const COLOR_PRESET_IDS = Object.freeze(['neutral', 'sun', 'night', 'custom']);
export const COLOR_RANGES = Object.freeze({brightness:[50,150],contrast:[50,150],saturation:[0,150],gamma:[50,200]});
const identity = Object.freeze({brightness:100,contrast:100,saturation:100,gamma:100});
const object = value => value && typeof value === 'object' && !Array.isArray(value);
export const colorCorrectionDefaults = () => structuredClone(defaults);

export function validateColorCorrection(settings) {
  const fail=()=>{throw new Error('Invalid VR color correction settings.');};
  if(!object(settings) || typeof settings.enabled !== 'boolean' || !COLOR_PRESET_IDS.includes(settings.preset) ||
    typeof settings.cycleShortcut !== 'string' || (settings.cycleShortcut !== '' && !/^(?:Ctrl\+)?(?:Alt\+)?(?:Shift\+)?F(?:[1-9]|1[0-2])$/.test(settings.cycleShortcut)) || !object(settings.presets)) fail();
  if(Object.keys(settings.presets).length !== 3 || Object.keys(settings.presets).some(id=>!['sun','night','custom'].includes(id))) fail();
  for(const id of ['sun','night','custom']) {
    const preset=settings.presets[id];if(!object(preset)) fail();
    for(const [key,[min,max]] of Object.entries(COLOR_RANGES))
      if(typeof preset[key] !== 'number' || !Number.isFinite(preset[key]) || preset[key]<min || preset[key]>max) fail();
  }
  return settings;
}

// Neutral is fixed and implicit. Returned values are safe to edit without changing settings.
export function resolvedColorPreset(settings, id = settings?.preset ?? 'neutral') {
  if(!COLOR_PRESET_IDS.includes(id)) throw new Error('Unknown VR color preset.');
  return {...(id === 'neutral' ? identity : (settings?.presets?.[id] ?? defaults.presets[id]))};
}
export const colorPresetIsIdentity = preset => Object.keys(identity).every(key=>preset[key] === 100);

// Desktop reference only: normalized sRGB, matching the native grading order.
export function gradeColorRgb(rgb, preset) {
  if(colorPresetIsIdentity(preset)) return [...rgb];
  const clamp=value=>Math.min(1,Math.max(0,value));
  const adjusted=rgb.map(value=>Math.pow(clamp(((value*preset.brightness/100)-.5)*preset.contrast/100+.5),100/preset.gamma));
  const luma=adjusted[0]*.2126+adjusted[1]*.7152+adjusted[2]*.0722;
  return adjusted.map(value=>clamp(luma+(value-luma)*preset.saturation/100));
}
