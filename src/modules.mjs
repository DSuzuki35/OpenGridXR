// These preferences belong to the application, never to exported game profiles.
export const MODULES = Object.freeze([
  { id: 'lmuRendering', label: 'LMU quad views & VRS', description: 'LMU offline quad views and stereo variable-rate shading. Profiles remain off until configured.' },
  { id: 'helmet', label: 'Helmet & visor', description: 'Helmet artwork, visor effects and helmet motion.' },
  { id: 'colorCorrection', label: 'VR color correction', description: 'Independent headset color presets. Clean Output stays unchanged.' },
  { id: 'framegen', label: 'Frame generation', description: 'Generate intermediate frames in supported OpenXR sessions.' },
  { id: 'overlay', label: 'Performance HUD', description: 'Show performance and session information inside the headset.' },
  { id: 'stream', label: 'Clean Output', description: 'Monitor output, streaming view and extra field of view.' },
  { id: 'telemetry', label: 'Telemetry', description: 'Read live vehicle data while a telemetry view or recording needs it.' },
  { id: 'benchmark', label: 'Benchmark', description: 'Record frame timings and lap telemetry. Requires Telemetry and an active native engine feature, such as the Performance HUD.' },
  { id: 'spatial', label: 'Spatial overlays', description: 'Capture windows and web pages as headset overlays.' },
  { id: 'bindings', label: 'Input bindings', description: 'Use joystick buttons and POV directions for supported actions.' },
  { id: 'sharpening', label: 'Sharpening', description: 'Apply image sharpening to the headset view.' },
]);

export const moduleDefaults = () => Object.fromEntries(MODULES.map(({ id }) => [id, true]));

export function validateModuleSettings(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value) || value.version !== 1 ||
      !value.enabled || typeof value.enabled !== 'object' || Array.isArray(value.enabled))
    throw new Error('Invalid module settings.');
  // Version-1 preferences written before 0.4.0 have no LMU module key.
  value = {...value, enabled:{lmuRendering:true,colorCorrection:true,...value.enabled}};
  delete value.enabled.foveation; // Retired iRacing INI management preference.
  const allowed = new Set(MODULES.map(({ id }) => id));
  if (Object.keys(value.enabled).some(id => !allowed.has(id)) ||
      MODULES.some(({ id }) => typeof value.enabled[id] !== 'boolean'))
    throw new Error('Module settings must include a boolean for every supported module.');
  if (value.enabled.benchmark && !value.enabled.telemetry)
    throw new Error('Benchmark requires the Telemetry module.');
  return { version: 1, enabled: Object.fromEntries(MODULES.map(({ id }) => [id, value.enabled[id]])) };
}

export function moduleForAction(actionId) {
  const section = typeof actionId === 'string' ? actionId.split('.')[0] : '';
  return ({ helmet: 'helmet', framegen: 'framegen', overlay: 'overlay', stream: 'stream',
    telemetry: 'telemetry', spatialOverlays: 'spatial', inputBindings: 'bindings',
    sharpening: 'sharpening', centering: 'bindings', colorCorrection: 'colorCorrection' })[section];
}

export function effectiveDocument(document, enabled) {
  const result = structuredClone(document);
  for (const profile of result.profiles) {
    if ((enabled.lmuRendering === false || profile.game !== 'lmu') && profile.lmuRendering) profile.lmuRendering.mode = 'off';
    for (const [id, section] of [['helmet','helmet'], ['framegen','framegen'], ['overlay','overlay'], ['telemetry','telemetry'], ['spatial','spatialOverlays'], ['bindings','inputBindings']]) {
      if (enabled[id] === false && profile[section]) profile[section].enabled = false;
    }
    if (enabled.framegen === false && profile.framegen) profile.framegen.toggleShortcut = '';
    if (enabled.colorCorrection === false && profile.colorCorrection) {
      profile.colorCorrection.enabled = false;
      profile.colorCorrection.cycleShortcut = '';
    }
    if (enabled.overlay === false && profile.overlay) {
      profile.overlay.toggleShortcut = '';
      profile.overlay.cycleShortcut = '';
    }
    if (enabled.stream === false && profile.stream) profile.stream.preserveFullFrame = false;
    if ((enabled.stream === false || enabled.helmet === false) && profile.stream?.helmet)
      profile.stream.helmet.enabled = false;
    if (enabled.sharpening === false && profile.sharpening) profile.sharpening.mode = 'off';
  }
  return result;
}
