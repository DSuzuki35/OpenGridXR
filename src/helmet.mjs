// Raster graphics are normalized to PNG before they enter a profile. Never store
// external URLs or vector markup in an appearance profile.
export const MAX_GRAPHIC_BYTES = 192 * 1024;
export const MAX_UPLOAD_BYTES = 4 * 1024 * 1024;
export const visorDefaults = () => ({
  enabled: true, height: 30, curve: 12, color: '#64538d', opacity: 55,
  text: '', textColor: '#ffffff', textSize: 44, mirror: true,
  textWidth: 52, textX: 0, textY: 0, textOpacity: 100, softness: .6,
  graphic: '', graphicWidth: 40, graphicX: 0, graphicY: 0,
});
export const visorSettings = (helmet) => ({ ...visorDefaults(), ...(helmet.visor ?? { enabled: false }) });

const invalid = (message) => { throw new Error(`Visor ${message}`); };
const uint32 = (bytes, at) => ((bytes.charCodeAt(at) * 0x1000000) +
  (bytes.charCodeAt(at + 1) << 16) + (bytes.charCodeAt(at + 2) << 8) + bytes.charCodeAt(at + 3));
const crcTable = Uint32Array.from({ length: 256 }, (_, value) => {
  for (let bit = 0; bit < 8; bit++) value = value & 1 ? 0xedb88320 ^ (value >>> 1) : value >>> 1;
  return value >>> 0;
});
function crc32(bytes, start, end) {
  let value = 0xffffffff;
  for (let at = start; at < end; at++) value = crcTable[(value ^ bytes.charCodeAt(at)) & 255] ^ (value >>> 8);
  return (value ^ 0xffffffff) >>> 0;
}

export function validateGraphic(value) {
  if (value === '') return;
  if (typeof value !== 'string' || value.length > Math.ceil(MAX_GRAPHIC_BYTES / 3) * 4 + 22 ||
      !/^data:image\/png;base64,(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(value))
    invalid('graphic must be an embedded PNG of at most 192 KB. Upload a PNG, JPEG or WebP to convert it.');
  let bytes;
  try { bytes = atob(value.slice(22)); } catch { invalid('graphic is not valid base64.'); }
  if (bytes.length > MAX_GRAPHIC_BYTES || bytes.slice(0, 8) !== '\x89PNG\r\n\x1a\n') invalid('graphic is not a PNG image.');
  let offset = 8, hasHeader = false, hasImage = false, hasEnd = false;
  while (offset + 12 <= bytes.length) {
    const length = uint32(bytes, offset), type = bytes.slice(offset + 4, offset + 8);
    if (offset + 12 + length > bytes.length) invalid('graphic is truncated.');
    if (crc32(bytes, offset + 4, offset + 8 + length) !== uint32(bytes, offset + 8 + length)) invalid('graphic has a damaged PNG checksum.');
    if (!hasHeader && type !== 'IHDR') invalid('graphic has no PNG header.');
    if (type === 'IHDR') {
      const width = uint32(bytes, offset + 8), height = uint32(bytes, offset + 12);
      if (hasHeader || length !== 13 || !width || !height || width > 1024 || height > 512)
        invalid('graphic must fit within 1024 × 512 pixels.');
      hasHeader = true;
    }
    if (type === 'IDAT' && length > 0) hasImage = true;
    offset += length + 12;
    if (type === 'IEND') { hasEnd = length === 0; break; }
  }
  if (!hasHeader || !hasImage || !hasEnd || offset !== bytes.length) invalid('graphic is an incomplete PNG.');
}

export function validateVisor(visor) {
  if (visor === undefined) return; // Profiles saved before the visor editor.
  if (!visor || typeof visor !== 'object' || Array.isArray(visor)) invalid('settings must be an object.');
  const defaults = visorDefaults();
  for (const key of Object.keys(visor)) if (!Object.hasOwn(defaults, key)) invalid('settings contain an unsupported field.');
  for (const key of ['enabled', 'mirror']) if (typeof visor[key] !== 'boolean') invalid(`${key} must be true or false.`);
  for (const [key, min, max] of [
    ['height', 5, 55], ['curve', -25, 25], ['opacity', 0, 100], ['textSize', 10, 60],
    ['graphicWidth', 5, 100], ['graphicX', -50, 50], ['graphicY', -25, 25],
  ]) if (!Number.isFinite(visor[key]) || visor[key] < min || visor[key] > max) invalid(`${key} must be between ${min} and ${max}.`);
  for (const key of ['color', 'textColor']) if (typeof visor[key] !== 'string' || !/^#[a-fA-F0-9]{6}$/.test(visor[key])) invalid(`${key} must be a six-digit hex color.`);
  for (const [key, min, max] of [['textWidth', 10, 100], ['textX', -50, 50], ['textY', -25, 25], ['textOpacity', 0, 100], ['softness', 0, 8]])
    if (visor[key] !== undefined && (!Number.isFinite(visor[key]) || visor[key] < min || visor[key] > max)) invalid(`${key} must be between ${min} and ${max}.`);
  if (typeof visor.text !== 'string' || visor.text.length > 80 || /[\u0000-\u001f\u007f]/.test(visor.text)) invalid('text must be at most 80 characters on one line.');
  validateGraphic(visor.graphic);
}

export function helmetPreviewGeometry(profile, eye = 'left') {
  const crop = profile.mode === 'driving' ? profile.view.crop : { left: 0, right: 0, top: 0, bottom: 0 };
  return {
    crop: { x: crop.left * 8, y: crop.top * 4.5, width: 800 * (1 - (crop.left + crop.right) / 100), height: 450 * (1 - (crop.top + crop.bottom) / 100) },
    contour: profile.mode === 'driving' && profile.mask.enabled
      ? profile.mask[eye].map(({ x, y }) => `${x * 800},${y * 450}`).join(' ') : null,
  };
}

export function visorPath(visor) {
  const bottom = visor.height * 4.5;
  return `M0 0H800V${bottom}Q400 ${bottom + visor.curve * 9} 0 ${bottom}Z`;
}

export async function readHelmetGraphic(file) {
  if (!file || !['image/png', 'image/jpeg', 'image/webp'].includes(file.type))
    throw new Error('Choose a PNG, JPEG or WebP image.');
  if (file.size > MAX_UPLOAD_BYTES) throw new Error('Choose an image smaller than 4 MB.');
  const header = new Uint8Array(await file.slice(0, 16).arrayBuffer());
  const signature = String.fromCharCode(...header);
  const validType = (file.type === 'image/png' && signature.startsWith('\x89PNG\r\n\x1a\n')) ||
    (file.type === 'image/jpeg' && header[0] === 0xff && header[1] === 0xd8 && header[2] === 0xff) ||
    (file.type === 'image/webp' && signature.startsWith('RIFF') && signature.slice(8, 12) === 'WEBP');
  if (!validType) throw new Error('The file contents do not match a PNG, JPEG or WebP image.');
  const url = URL.createObjectURL(file);
  try {
    const img = new Image();
    img.src = url;
    try { await img.decode(); } catch { throw new Error('This image could not be decoded. Choose a valid PNG, JPEG or WebP.'); }
    if (!img.naturalWidth || !img.naturalHeight || img.naturalWidth * img.naturalHeight > 16 * 1024 * 1024)
      throw new Error('Choose an image with at most 16 megapixels.');
    const canvas = document.createElement('canvas');
    let scale = Math.min(1, 1024 / img.naturalWidth, 512 / img.naturalHeight);
    for (let attempt = 0; attempt < 8; attempt++) {
      canvas.width = Math.max(1, Math.round(img.naturalWidth * scale));
      canvas.height = Math.max(1, Math.round(img.naturalHeight * scale));
      canvas.getContext('2d').drawImage(img, 0, 0, canvas.width, canvas.height);
      const graphic = canvas.toDataURL('image/png');
      if (graphic.length <= Math.ceil(MAX_GRAPHIC_BYTES / 3) * 4 + 22) { validateGraphic(graphic); return graphic; }
      scale *= 0.75;
    }
    throw new Error('This image is too detailed to fit. Choose a simpler graphic.');
  } finally { URL.revokeObjectURL(url); }
}

// Circumscribed convex aperture: every nonopaque shell pixel remains visible.
// Keeping a convex envelope also avoids treating lettering or the breath
// deflector as disconnected holes. At most 52 points survive the half-planes.
export function deriveHelmetAperture(pixels, width, height, paddingPixels = 0) {
  const rows = [];
  for (let y = 0; y < height; y++) {
    let left = width, right = -1;
    for (let x = 0; x < width; x++) {
      if (pixels[(y * width + x) * 4 + 3] < 254) { left = Math.min(left, x); right = x; }
    }
    if (right >= 0) rows.push([left, y], [left, y + 1], [right + 1, y], [right + 1, y + 1]);
  }
  let polygon = [{ x: 0, y: 0 }, { x: width, y: 0 }, { x: width, y: height }, { x: 0, y: height }];
  if (rows.length) for (let n = 0; n < 48; n++) {
    const angle = n * Math.PI * 2 / 48, nx = Math.cos(angle), ny = Math.sin(angle);
    let support = -Infinity;
    for (const [x, y] of rows) support = Math.max(support, nx * x + ny * y);
    support += paddingPixels;
    const output = [];
    for (let i = 0; i < polygon.length; i++) {
      const a = polygon[i], b = polygon[(i + 1) % polygon.length];
      const da = nx * a.x + ny * a.y - support, db = nx * b.x + ny * b.y - support;
      if (da <= 1e-7) output.push(a);
      if ((da < 0 && db > 0) || (da > 0 && db < 0)) {
        const t = da / (da - db);
        output.push({ x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t });
      }
    }
    polygon = output;
  }
  return polygon.map(({x, y}) => ({ x: Math.max(0, Math.min(1, x / width)), y: Math.max(0, Math.min(1, y / height)) }))
    .filter((point, i, list) => Math.hypot(point.x - list[(i + 1) % list.length].x, point.y - list[(i + 1) % list.length].y) > 1e-7);
}

export function cropForHelmetApertures({ left, right }) {
  const points = [...left, ...right];
  const safePercent = value => Math.max(0, Math.min(40, Math.floor(value * 100)));
  return {
    left: safePercent(Math.min(...points.map(p => p.x))),
    right: safePercent(1 - Math.max(...points.map(p => p.x))),
    top: safePercent(Math.min(...points.map(p => p.y))),
    bottom: safePercent(1 - Math.max(...points.map(p => p.y))),
  };
}

// Self-contained so the trusted main process can execute exactly this function
// in an isolated, network-disabled offscreen renderer. Inputs are validated by
// the main process before serialization; no user content becomes markup/code.
export async function composeHelmetRaster({ helmet, shellUrl, uvUrl, uvVUrl, paddingPixels = 4, width = 1920, height = 1080, apertureOnly = false }, aperture = deriveHelmetAperture) {
  const loadImage = async source => {
    const image = new Image();
    image.src = source;
    await image.decode();
    return image;
  };
  const shell = await loadImage(shellUrl), visor = helmet.visor;
  const canvas = document.createElement('canvas');
  canvas.width = width; canvas.height = height;
  const context = canvas.getContext('2d', { willReadFrequently: true });
  const fit = () => {
    context.translate(helmet.offsetX * 8, helmet.offsetY * 4.5);
    context.translate(400, 225); context.scale(helmet.scale / 100, helmet.scale / 100); context.translate(-400, -225);
  };
  context.scale(width / 800, height / 450);
  context.save(); fit();
  context.globalAlpha = helmet.opacity / 100;
  context.drawImage(shell, 0, 0, 800, 450);
  context.restore();
  const contour = aperture(context.getImageData(0, 0, width, height).data, width, height, paddingPixels);
  if (apertureOnly) return { contour, width, height };
  context.clearRect(0, 0, 800, 450);
  if (visor.enabled || helmet.visorTint > 0) {
    if (!uvUrl || !uvVUrl) throw new Error('The curved visor projection maps are missing. Reinstall the application assets.');
    const texture = document.createElement('canvas'); texture.width = 1600; texture.height = 900;
    const paint = texture.getContext('2d', { willReadFrequently: true }); paint.scale(2, 2);
    paint.fillStyle = helmet.visorTintColor ?? '#142035'; paint.globalAlpha = helmet.visorTint / 100; paint.fillRect(0, 0, 800, 450); paint.globalAlpha = 1;
    if (visor.enabled) {
    const bottom = visor.height * 4.5, centerY = bottom + visor.curve * 4.5 - 24;
    paint.beginPath(); paint.moveTo(0, 0); paint.lineTo(800, 0); paint.lineTo(800, bottom);
    paint.quadraticCurveTo(400, bottom + visor.curve * 9, 0, bottom); paint.closePath();
    paint.fillStyle = visor.color; paint.globalAlpha = visor.opacity / 100; paint.fill();
    paint.globalAlpha = (visor.textOpacity ?? 100) / 100; paint.clip();
    paint.filter = `blur(${visor.softness ?? .6}px)`;
    if (visor.mirror) { paint.translate(800, 0); paint.scale(-1, 1); }
    if (visor.graphic) {
      const graphic = await loadImage(visor.graphic);
      const boxWidth = visor.graphicWidth * 8, boxHeight = visor.graphicWidth * 2;
      const scale = Math.min(boxWidth / graphic.naturalWidth, boxHeight / graphic.naturalHeight);
      const w = graphic.naturalWidth * scale, h = graphic.naturalHeight * scale;
      paint.drawImage(graphic, 400 + visor.graphicX * 8 - w / 2, centerY + visor.graphicY * 4.5 - h / 2, w, h);
    }
    if (visor.text) {
      paint.fillStyle = visor.textColor; paint.font = `800 ${visor.textSize}px Arial, sans-serif`;
      paint.textAlign = 'center'; paint.textBaseline = 'middle'; paint.letterSpacing = '2px';
      paint.translate(400 + (visor.textX ?? 0) * 8, centerY + (visor.textY ?? 0) * 4.5);
      paint.scale((visor.textWidth ?? 52) * 8 / Math.max(1, paint.measureText(visor.text).width), 1);
      paint.fillText(visor.text, 0, 0);
    }
    }
    const uvImage = await loadImage(uvUrl), vImage = await loadImage(uvVUrl);
    const mapping = document.createElement('canvas'); mapping.width = width; mapping.height = height;
    const mapped = mapping.getContext('2d', { willReadFrequently: true });
    mapped.drawImage(uvImage, 0, 0, width, height);
    const uPixels = mapped.getImageData(0, 0, width, height).data;
    mapped.clearRect(0, 0, width, height); mapped.drawImage(vImage, 0, 0, width, height);
    const vPixels = mapped.getImageData(0, 0, width, height).data;
    const ink = paint.getImageData(0, 0, 1600, 900).data;
    const projection = mapped.createImageData(width, height), output = projection.data;
    for (let at = 0; at < output.length; at += 4) {
      if (!uPixels[at + 3]) continue;
      const tx = Math.min(1599, (uPixels[at] * 256 + uPixels[at + 1]) / 65535 * 1599);
      const ty = Math.min(899, (vPixels[at] * 256 + vPixels[at + 1]) / 65535 * 899);
      const x = Math.floor(tx), y = Math.floor(ty), fx = tx - x, fy = ty - y;
      const a = (y * 1600 + x) * 4, b = (y * 1600 + Math.min(x + 1, 1599)) * 4;
      const c = (Math.min(y + 1, 899) * 1600 + x) * 4, d = (Math.min(y + 1, 899) * 1600 + Math.min(x + 1, 1599)) * 4;
      const wa = (1 - fx) * (1 - fy) * ink[a + 3], wb = fx * (1 - fy) * ink[b + 3];
      const wc = (1 - fx) * fy * ink[c + 3], wd = fx * fy * ink[d + 3], alpha = wa + wb + wc + wd;
      if (!alpha) continue;
      for (let channel = 0; channel < 3; channel++) output[at + channel] = (ink[a + channel] * wa + ink[b + channel] * wb + ink[c + channel] * wc + ink[d + channel] * wd) / alpha;
      output[at + 3] = alpha * uPixels[at + 3] / 255;
    }
    mapped.putImageData(projection, 0, 0);
    context.save(); fit(); context.drawImage(mapping, 0, 0, 800, 450); context.restore();
  }
  context.save(); fit(); context.globalAlpha = helmet.opacity / 100; context.filter = 'brightness(.64)';
  context.drawImage(shell, 0, 0, 800, 450); context.restore();
  return { png: canvas.toDataURL('image/png'), contour, width, height };
}

export async function fitHelmetOpening(helmet, paddingPercent = 3) {
  const normalized = { ...helmet, visor: visorSettings(helmet) };
  // Reserve a conservative motion envelope as well as the user's mask padding.
  const motionMargin = (helmet.headMotion ?? 0) > 0 || (helmet.vehicleMotion ?? 0) > 0 ? .03 : 0;
  const paddingPixels = 4 + (paddingPercent / 100 + motionMargin) * 1920;
  const eyes = await Promise.all(['left', 'right'].map(async eye => {
    const { contour } = await composeHelmetRaster({ helmet: normalized, shellUrl: `./helmets/${helmet.style}-${eye}.png`, paddingPixels, apertureOnly: true });
    return [eye, contour];
  }));
  const contours = Object.fromEntries(eyes);
  return { contours, crop: cropForHelmetApertures(contours) };
}
