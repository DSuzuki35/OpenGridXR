const assert = require('node:assert/strict');

// Native output quality and desktop preview resolution are intentionally independent.
module.exports = async function assertHelmetImagesMatch(page, preview, baked) {
  const comparison = await page.evaluate(async ({ preview, baked }) => {
    const images = await Promise.all([preview, baked].map(async src => { const img = new Image(); img.src = src; await img.decode(); return img; }));
    const pixels = images.map(img => {
      const canvas = document.createElement('canvas'); canvas.width = 480; canvas.height = 270;
      const context = canvas.getContext('2d'); context.drawImage(img, 0, 0, 480, 270);
      return context.getImageData(0, 0, 480, 270).data;
    });
    let difference = 0;
    // Straight RGB in almost-transparent texels is unstable across raster sizes.
    // Compare their visible premultiplied contribution, while retaining alpha.
    for (let i = 0; i < pixels[0].length; i += 4) {
      for (let channel = 0; channel < 3; channel++) difference += Math.abs(pixels[0][i + channel] * pixels[0][i + 3] / 255 - pixels[1][i + channel] * pixels[1][i + 3] / 255);
      difference += Math.abs(pixels[0][i + 3] - pixels[1][i + 3]);
    }
    return { width: images[1].naturalWidth, height: images[1].naturalHeight, meanDifference: difference / pixels[0].length };
  }, { preview, baked: 'data:image/png;base64,' + baked.toString('base64') });
  assert.equal(comparison.width, 2048);
  assert.equal(comparison.height, 1152);
  // Resampling and raster-size-dependent blur may differ by under 1% of the
  // channel range; the UI tests separately assert aperture and lettering bounds.
  assert.ok(comparison.meanDifference < 255 * .01, `Preview and native bake must agree at a common size: ${JSON.stringify(comparison)}`);
};
