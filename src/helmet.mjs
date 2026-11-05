// Raster graphics are normalized to PNG before they enter a profile. Never store
// external URLs or vector markup in an appearance profile.
export const MAX_GRAPHIC_BYTES = 192 * 1024;
export const MAX_UPLOAD_BYTES = 4 * 1024 * 1024;
export const helmetRasterSize = helmet => helmet.detail === 'detail'
  ? {width:4096,height:2304} : {width:2048,height:1152};
export const visorFonts = [
  { value: 'arial', label: 'Arial' }, { value: 'arialNarrow', label: 'Arial Narrow' },
  { value: 'impact', label: 'Impact' }, { value: 'georgia', label: 'Georgia' },
  { value: 'consolas', label: 'Consolas' }, { value: 'trebuchet', label: 'Trebuchet MS' },
];
export const visorDefaults = () => ({
  enabled: true, height: 30, curve: 12, color: '#64538d', opacity: 55,
  text: '', textColor: '#ffffff', textSize: 44, font: 'arial', mirror: true,
  textWidth: 52, textCurve: 0, textX: 0, textY: 0, textOpacity: 100, softness: .6,
  graphic: '', graphicWidth: 40, graphicX: 0, graphicY: 0,
});
export const visorSettings = (helmet) => ({ ...visorDefaults(), ...(helmet.visor ?? { enabled: false }) });

// Shared by the composition preview and saved output artwork. Stream opacity is
// applied to the complete raster by the consumer, independent of shell opacity.
export function streamHelmetAppearance(profile) {
  const stream = profile.stream.helmet;
  return { ...profile.helmet, enabled: stream?.enabled === true,
    opacity: 100,
    scale: stream?.scale ?? 100, offsetX: stream?.offsetX ?? 0, offsetY: stream?.offsetY ?? 0,
    visorHotkey: false, headMotion: 0, vehicleMotion: 0, stereoOffset: 0,
    visorBrightness: 100, visorContrast: 100, visorSaturation: 100 };
}

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
  if (visor.font !== undefined && !visorFonts.some(({value}) => value === visor.font)) invalid('font must be a supported family.');
  for (const [key, min, max] of [
    ['height', 5, 55], ['curve', -25, 25], ['opacity', 0, 100], ['textSize', 10, 60],
    ['graphicWidth', 5, 100], ['graphicX', -50, 50], ['graphicY', -25, 25],
  ]) if (!Number.isFinite(visor[key]) || visor[key] < min || visor[key] > max) invalid(`${key} must be between ${min} and ${max}.`);
  for (const key of ['color', 'textColor']) if (typeof visor[key] !== 'string' || !/^#[a-fA-F0-9]{6}$/.test(visor[key])) invalid(`${key} must be a six-digit hex color.`);
  for (const [key, min, max] of [['textWidth', 10, 100], ['textCurve', -25, 25], ['textX', -50, 50], ['textY', -25, 25], ['textOpacity', 0, 100], ['softness', 0, 8]])
    if (visor[key] !== undefined && (!Number.isFinite(visor[key]) || visor[key] < min || visor[key] > max)) invalid(`${key} must be between ${min} and ${max}.`);
  if (typeof visor.text !== 'string' || visor.text.length > 80 || /[\u0000-\u001f\u007f]/.test(visor.text)) invalid('text must be at most 80 characters on one line.');
  validateGraphic(visor.graphic);
}

export function helmetPreviewGeometry(profile, eye = 'left') {
  const fullFrame = profile.stream?.preserveFullFrame ?? profile.mode === 'streaming';
  const crop = !fullFrame && !profile.view.disableCrop ? profile.view.crop : { left: 0, right: 0, top: 0, bottom: 0 };
  return {
    crop: { x: crop.left * 8, y: crop.top * 4.5, width: 800 * (1 - (crop.left + crop.right) / 100), height: 450 * (1 - (crop.top + crop.bottom) / 100) },
    contour: !fullFrame && profile.mask.enabled && !profile.view.disableCrop
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

// Circumscribed convex aperture: every nonopaque composite pixel remains visible.
// Keeping a convex envelope also avoids treating lettering or the breath
// deflector as disconnected holes. At most 52 points survive the half-planes.
export function deriveHelmetAperture(pixels, width, height, paddingPixels = 0) {
  const rows = [];
  for (let y = 0; y < height; y++) {
    let left = width, right = -1;
    for (let x = 0; x < width; x++) {
      if (pixels[(y * width + x) * 4 + 3] < 255) { left = Math.min(left, x); right = x; }
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
  const crop = {
    left: safePercent(Math.min(...points.map(p => p.x))),
    right: safePercent(1 - Math.max(...points.map(p => p.x))),
    top: safePercent(Math.min(...points.map(p => p.y))),
    bottom: safePercent(1 - Math.max(...points.map(p => p.y))),
  };
  for (const [a, b] of [['left', 'right'], ['top', 'bottom']]) {
    const excess = Math.max(0, crop[a] + crop[b] - 70);
    crop[a] -= Math.ceil(excess / 2); crop[b] -= Math.floor(excess / 2);
  }
  return crop;
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
  let shell = await loadImage(shellUrl);
  const visor = helmet.visor;
  // Bake the opening once. Move the padded exterior without stretching it;
  // interpolate the interior (and its projected print) between the new edges.
  // Angles use a declared 100-degree reference, not a measured headset FOV.
  const original = document.createElement('canvas'); original.width = width; original.height = height;
  const originalContext = original.getContext('2d', { willReadFrequently: true });
  originalContext.drawImage(shell, 0, 0, width, height);
  const alpha = originalContext.getImageData(0, 0, width, height).data;
  const centerX = Math.floor(width / 2);
  let top = 0, bottom = height;
  while (top < height && alpha[(top * width + centerX) * 4 + 3] >= 254) top++;
  while (bottom > top && alpha[((bottom - 1) * width + centerX) * 4 + 3] >= 254) bottom--;
  const centerY = Math.min(height - 1, Math.floor((top + bottom) / 2));
  let left = 0, right = width;
  while (left < width && alpha[(centerY * width + left) * 4 + 3] >= 254) left++;
  while (right > left && alpha[(centerY * width + right - 1) * 4 + 3] >= 254) right--;
  const openingY = helmet.openingAngle === undefined || bottom <= top ? 1 : height * Math.tan(helmet.openingAngle * Math.PI / 360) / Math.tan(50 * Math.PI / 180) / (bottom - top);
  const openingX = helmet.openingWidth === undefined || right <= left ? 1 : width * helmet.openingWidth / 100 / (right - left);
  const reshape = source => {
    let result = source;
    const axes = [
      [false, top, bottom, helmet.openingAngle === undefined ? undefined : Math.tan(helmet.openingAngle * Math.PI / 360) / Math.tan(50 * Math.PI / 180)],
      [true, left, right, helmet.openingWidth === undefined ? undefined : helmet.openingWidth / 100],
    ];
    for (const [horizontal, start, end, fraction] of axes) {
      if (fraction === undefined || end <= start) continue;
      const size = horizontal ? width : height, span = size * fraction, low = (size - span) / 2, high = low + span;
      const output = document.createElement('canvas'); output.width = width; output.height = height;
      const paint = output.getContext('2d');
      for (let n = 0; n < size; n++) {
        const at = n + .5;
        const from = Math.max(0, Math.min(size - 1, at < low ? start + at - low : at > high ? end + at - high : start + (at - low) * (end - start) / span));
        if (horizontal) paint.drawImage(result, from, 0, 1, height, n, 0, 1, height);
        else paint.drawImage(result, 0, from, width, 1, 0, n, width, 1);
      }
      result = output;
    }
    return result;
  };
  shell = reshape(original);
  const soften = source => {
    if (!(helmet.shellBlur > 0)) return source;
    const radius = helmet.shellBlur * width / 800, pad = Math.ceil(radius * 3) + 2;
    const padded = document.createElement('canvas'); padded.width = width + 2 * pad; padded.height = height + 2 * pad;
    const fill = padded.getContext('2d');
    const xs = [[0, 1, 0, pad], [0, width, pad, width], [width - 1, 1, pad + width, pad]];
    const ys = [[0, 1, 0, pad], [0, height, pad, height], [height - 1, 1, pad + height, pad]];
    for (const [sx, sw, dx, dw] of xs) for (const [sy, sh, dy, dh] of ys)
      fill.drawImage(source, sx, sy, sw, sh, dx, dy, dw, dh);
    const softened = document.createElement('canvas'); softened.width = width; softened.height = height;
    const blur = softened.getContext('2d'); blur.filter = `blur(${radius}px)`;
    blur.drawImage(padded, -pad, -pad); return softened;
  };
  const canvas = document.createElement('canvas');
  canvas.width = width; canvas.height = height;
  const context = canvas.getContext('2d', { willReadFrequently: true });
  const fit = () => {
    context.translate(helmet.offsetX * 8, helmet.offsetY * 4.5);
    context.translate(400, 225); context.scale(helmet.scale / 100, helmet.scale / 100); context.translate(-400, -225);
  };
  const drawShell = () => {
    // Extend all four borders and corners before moving/zooming the image.
    // Otherwise translating the finite PNG creates transparent scene strips.
    const bleed = 2400;
    // Overlap one logical pixel: fractional translations otherwise leave an
    // antialiased seam that the aperture detector mistakes for visible scene.
    context.drawImage(shell, 0, 0, width, 1, 0, -bleed, 800, bleed + 1);
    context.drawImage(shell, 0, height - 1, width, 1, 0, 449, 800, bleed + 1);
    context.drawImage(shell, 0, 0, 1, height, -bleed, 0, bleed + 1, 450);
    context.drawImage(shell, width - 1, 0, 1, height, 799, 0, bleed + 1, 450);
    for (const [sx, dx] of [[0, -bleed], [width - 1, 799]])
      for (const [sy, dy] of [[0, -bleed], [height - 1, 449]])
        context.drawImage(shell, sx, sy, 1, 1, dx, dy, bleed + 1, bleed + 1);
    context.drawImage(shell, 0, 0, 800, 450);
  };
  context.scale(width / 800, height / 450);
  const visorPosition = helmet.visorPosition ?? 'closed';
  const visorLift = visorPosition === 'half' ? (bottom - top) / height * 225 * openingY : 0;
  if (visorPosition !== 'open' && (visor.enabled || helmet.visorTint > 0)) {
    // Move all projected glass and print together; the shell drawn last hides
    // the raised visor behind the brow. Closed remains byte-compatible in layout.
    context.save();
    context.translate(0, -visorLift * helmet.scale / 100);
    if (!uvUrl || !uvVUrl) throw new Error('The curved visor projection maps are missing. Reinstall the application assets.');
    const texture = document.createElement('canvas'); texture.width = 1600; texture.height = 900;
    const paint = texture.getContext('2d', { willReadFrequently: true }); paint.scale(2, 2);
    let backgroundTexture = texture, bandTexture;
    paint.fillStyle = helmet.visorTintColor ?? '#142035'; paint.globalAlpha = helmet.visorTint / 100; paint.fillRect(0, 0, 800, 450); paint.globalAlpha = 1;
    if (visor.enabled) {
    const bottom = visor.height * 4.5, centerY = bottom + visor.curve * 4.5 - 24;
    paint.beginPath(); paint.moveTo(0, 0); paint.lineTo(800, 0); paint.lineTo(800, bottom);
    paint.quadraticCurveTo(400, bottom + visor.curve * 9, 0, bottom); paint.closePath();
    paint.fillStyle = visor.color; paint.globalAlpha = visor.opacity / 100; paint.fill();
    // Separate visor material, printable artwork and the allowed print region.
    backgroundTexture = document.createElement('canvas'); backgroundTexture.width = 1600; backgroundTexture.height = 900;
    backgroundTexture.getContext('2d').drawImage(texture, 0, 0);
    paint.clearRect(0, 0, 800, 450); paint.fillStyle = '#ffffff'; paint.globalAlpha = 1; paint.fill();
    bandTexture = document.createElement('canvas'); bandTexture.width = 1600; bandTexture.height = 900;
    bandTexture.getContext('2d').drawImage(texture, 0, 0);
    paint.clearRect(0, 0, 800, 450);
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
      const families = { arial: 'Arial, sans-serif', arialNarrow: '"Arial Narrow", sans-serif', impact: 'Impact, sans-serif', georgia: 'Georgia, serif', consolas: 'Consolas, monospace', trebuchet: '"Trebuchet MS", sans-serif' };
      paint.fillStyle = visor.textColor; paint.font = `800 ${visor.textSize}px ${families[visor.font ?? 'arial'] ?? families.arial}`;
      paint.textAlign = 'center'; paint.textBaseline = 'middle'; paint.letterSpacing = '2px';
      paint.translate(400 + (visor.textX ?? 0) * 8, centerY + (visor.textY ?? 0) * 4.5);
      const textWidth = (visor.textWidth ?? 52) * 8, textCurve = visor.textCurve ?? 0;
      if (!textCurve) {
        paint.scale(textWidth / Math.max(1, paint.measureText(visor.text).width), 1);
        paint.fillText(visor.text, 0, 0);
      } else {
        // Warp the whole text raster, including the glyphs, at the 2x texture
        // resolution. The parabola matches the strip's quadratic curve: positive
        // values raise the edges while keeping the text's center in place.
        const lettering = document.createElement('canvas');
        lettering.width = Math.ceil(textWidth * 2) + 8; lettering.height = Math.ceil(visor.textSize * 4) + 8;
        const letters = lettering.getContext('2d'); letters.scale(2, 2);
        letters.fillStyle = visor.textColor; letters.font = paint.font;
        letters.textAlign = 'center'; letters.textBaseline = 'middle'; letters.letterSpacing = '2px';
        letters.translate(lettering.width / 4, lettering.height / 4);
        letters.scale(textWidth / Math.max(1, letters.measureText(visor.text).width), 1);
        letters.fillText(visor.text, 0, 0);
        const edgeShift = Math.ceil(Math.abs(textCurve) * 9 * (lettering.width / 1600) ** 2) + 2;
        const bent = document.createElement('canvas');
        bent.width = lettering.width; bent.height = lettering.height + edgeShift * 2;
        const bend = bent.getContext('2d');
        for (let x = 0; x < lettering.width; x++) {
          const shift = -textCurve * 9 * ((x + .5 - lettering.width / 2) / 800) ** 2;
          bend.drawImage(lettering, x, 0, 1, lettering.height, x, edgeShift + shift, 1, lettering.height);
        }
        // Apply the existing mirror transform, opacity and blur once, after
        // warping; filtering individual columns would leave visible seams.
        paint.drawImage(bent, -bent.width / 4, -lettering.height / 4 - edgeShift / 2, bent.width / 2, bent.height / 2);
      }
    }
    }
    const uvImage = await loadImage(uvUrl), vImage = await loadImage(uvVUrl);
    const mapping = document.createElement('canvas'); mapping.width = width; mapping.height = height;
    const mapped = mapping.getContext('2d', { willReadFrequently: true });
    mapped.drawImage(uvImage, 0, 0, width, height);
    const uPixels = mapped.getImageData(0, 0, width, height).data;
    mapped.clearRect(0, 0, width, height); mapped.drawImage(vImage, 0, 0, width, height);
    const vPixels = mapped.getImageData(0, 0, width, height).data;
    const project = inkCanvas => {
    const ink = inkCanvas.getContext('2d').getImageData(0, 0, 1600, 900).data;
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
    const raster = document.createElement('canvas'); raster.width = width; raster.height = height;
    raster.getContext('2d').putImageData(projection, 0, 0);
    return raster;
    };
    context.save(); fit(); context.drawImage(reshape(project(backgroundTexture)), 0, 0, 800, 450); context.restore();
    if (visor.enabled) {
      let print = project(texture);
      if (helmet.openingAngle !== undefined || helmet.openingWidth !== undefined) {
        // The opening deforms the visor material, not the lettering. Fit the
        // original projected artwork uniformly inside the new visible strip.
        const printContext = print.getContext('2d', { willReadFrequently: true });
        printContext.globalCompositeOperation = 'destination-out'; printContext.drawImage(original, 0, 0);
        printContext.globalCompositeOperation = 'source-over';
        const inkPixels = printContext.getImageData(0, 0, width, height).data;
        let x0 = width, y0 = height, x1 = -1, y1 = -1;
        for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
          if (!inkPixels[(y * width + x) * 4 + 3]) continue;
          x0 = Math.min(x0, x); x1 = Math.max(x1, x); y0 = Math.min(y0, y); y1 = Math.max(y1, y);
        }
        if (x1 >= x0 && y1 >= y0) {
          const allowed = reshape(project(bandTexture));
          const allowedContext = allowed.getContext('2d', { willReadFrequently: true });
          allowedContext.globalCompositeOperation = 'destination-out'; allowedContext.drawImage(shell, 0, 0);
          const mask = allowedContext.getImageData(0, 0, width, height).data;
          const columns = Array.from({length: width}, () => [height, -1]);
          let bandLeft = width, bandRight = -1;
          for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) if (mask[(y * width + x) * 4 + 3] > 250) {
            columns[x][0] = Math.min(columns[x][0], y); columns[x][1] = Math.max(columns[x][1], y);
            bandLeft = Math.min(bandLeft, x); bandRight = Math.max(bandRight, x);
          }
          const forward = (v, start, end, size, factor) => {
            const low = (size - (end - start) * factor) / 2;
            return v < start ? low + v - start : v > end ? low + (end - start) * factor + v - end : low + (v - start) * factor;
          };
          const bw = x1 - x0 + 1, bh = y1 - y0 + 1;
          let scale = Math.min(1, Math.max(0, bandRight - bandLeft - 4) / bw);
          let cx = helmet.openingWidth === undefined ? (x0 + x1) / 2 : forward((x0 + x1) / 2, left, right, width, openingX);
          let cy = helmet.openingAngle === undefined ? (y0 + y1) / 2 : forward((y0 + y1) / 2, top, bottom, height, openingY);
          let lo = 0, hi = height;
          // Find a conservative region across the complete artwork width.
          const requestedX = cx;
          const region = size => {
            cx = Math.max(bandLeft + bw * size / 2 + 2, Math.min(bandRight - bw * size / 2 - 2, requestedX));
            lo = 0; hi = height;
            for (let x = Math.max(0, Math.floor(cx - bw * size / 2)); x <= Math.min(width - 1, Math.ceil(cx + bw * size / 2)); x++) {
              lo = Math.max(lo, columns[x][0] + 2); hi = Math.min(hi, columns[x][1] - 2);
            }
            return hi - lo >= bh * size;
          };
          if (!region(scale)) {
            let low = 0, high = scale;
            for (let attempt = 0; attempt < 14; attempt++) {
              const middle = (low + high) / 2;
              if (region(middle)) low = middle; else high = middle;
            }
            scale = low; region(scale);
          }
          const fitted = document.createElement('canvas'); fitted.width = width; fitted.height = height;
          if (hi > lo && scale > 0) {
            scale = Math.min(scale, (hi - lo) / bh);
            cy = Math.max(lo + bh * scale / 2, Math.min(hi - bh * scale / 2, cy));
            const dst = fitted.getContext('2d');
            dst.drawImage(print, x0, y0, bw, bh, cx - bw * scale / 2, cy - bh * scale / 2, bw * scale, bh * scale);
            dst.globalCompositeOperation = 'destination-in'; dst.drawImage(allowed, 0, 0);
          }
          print = fitted;
        }
      }
      context.save(); fit(); context.drawImage(print, 0, 0, 800, 450); context.restore();
    }
    context.restore();
  }
  if (visorPosition === 'half') {
    // Clear glass still has a lower edge. Its lifted rim distinguishes Half
    // from Open even when all printed material is hidden behind the brow.
    context.save(); fit();
    const span = (right - left) / width * 800 * openingX;
    const edge = 225;
    context.beginPath(); context.moveTo(400 - span / 2, edge - 7);
    context.quadraticCurveTo(400, edge + 7, 400 + span / 2, edge - 7);
    context.strokeStyle = 'rgba(215,225,240,.32)'; context.lineWidth = .8; context.stroke();
    context.restore();
  }
  context.save(); fit(); context.globalAlpha = helmet.opacity / 100;
  context.filter = `brightness(${.64 * (helmet.shellBrightness ?? 100) / 100}) contrast(${(helmet.shellContrast ?? 100) / 100}) saturate(${(helmet.shellSaturation ?? 100) / 100})`;
  drawShell(); context.restore();
  // Discover the visible scene from the exact final overlay, after strip,
  // graphics, shell opacity, transforms and blur. Any remaining transparency
  // needs the game behind it. Print layout still uses the sharp shell above.
  const finished = soften(canvas);
  const contour = aperture(finished.getContext('2d', { willReadFrequently: true }).getImageData(0, 0, width, height).data, width, height, paddingPixels);
  return { ...(apertureOnly ? {} : { png: finished.toDataURL('image/png') }), contour, width, height };
}

export function helmetPaddingPixels(helmet, paddingPercent = 3, width = 1920) {
  // A shared crop must contain either eye's opening, including stereo correction.
  const motionMargin = (helmet.headMotion ?? 0) > 0 || (helmet.vehicleMotion ?? 0) > 0 ? .03 : 0;
  return 4 + (paddingPercent / 100 + motionMargin + Math.abs(helmet.stereoOffset ?? 0) / 100) * width;
}

export async function fitHelmetOpening(helmet, paddingPercent = 3) {
  // A live-openable visor must never reserve crop beneath an opaque closed strip.
  const normalized = { ...helmet, visor: visorSettings(helmet), ...(helmet.visorHotkey ? {visorPosition:'open'} : {}) };
  const paddingPixels = helmetPaddingPixels(helmet, paddingPercent);
  const root = `./helmets/${helmet.style}`;
  const { contour } = await composeHelmetRaster({ helmet: normalized, shellUrl: `${root}.png`, uvUrl: `${root}-mono-visor-uv.png`, uvVUrl: `${root}-mono-visor-v.png`, paddingPixels, apertureOnly: true });
  const contours = { left: contour, right: structuredClone(contour) };
  const crop = cropForHelmetApertures(contours);
  // Keep full horizontal projection by default; the curved contour still
  // follows the actual opaque corners. Narrow widths are explicit opt-in.
  if ((helmet.openingWidth ?? 100) >= 100) crop.left = crop.right = 0;
  return { contours, crop };
}
