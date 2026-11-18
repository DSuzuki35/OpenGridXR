// SteadyXR preview 0.14 built-in camera settings. Upstream crop is the
// total reserved percentage along each axis. OpenGrid stores each edge.
const presets = {
  natural: { smoothing: 25, horizonLock: 25, crop: 8, mirrorLimit: 10, mirrorSoftness: 60 },
  comfort: { smoothing: 60, horizonLock: 50, crop: 12, mirrorLimit: 12, mirrorSoftness: 70 },
  strong: { smoothing: 70, horizonLock: 85, crop: 15, mirrorLimit: 12, mirrorSoftness: 75 },
};

export function cameraPreset(id) {
  if (id === 'dan-no-motion') return {...cameraPreset('dan-suzuki'), smoothing: 40, mirrorLimit: 10};
  if (id === 'dan-suzuki') return {
    stabilize: true, smoothing: 82.5, horizonLock: 21.1, adaptiveHorizon: true, pitchLock: 3.3,
    eye: 1, offsetX: -77, offsetY: -28.3, reserve: {left: 18, right: 18, top: 18, bottom: 18},
    mirror: true, mirrorLimit: 29.3, mirrorSoftness: 100, mirrorStretch: 31.6,
    preserveFullFrame: true, extraSymmetric: true, extraLeft: 0, extraRight: 0,
  };
  const {crop, ...camera} = presets[id];
  const edge = crop / 2;
  return {...camera, stabilize: true, adaptiveHorizon: true, mirror: true,
    reserve: {left: edge, right: edge, top: edge, bottom: edge}};
}
