const {_electron} = require('playwright-core');
const fs = require('node:fs/promises'), path = require('node:path'), os = require('node:os'), assert = require('node:assert/strict');

(async () => {
  const {createDocument, parseProfile} = await import('../src/model.mjs');
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'opengrid-gsi-'));
  const document = createDocument(), profile = document.profiles[0];
  profile.helmet.enabled = true;
  profile.helmet.detail = 'performance';
  profile.helmet.visor.text = 'MY TEAM';
  profile.helmet.visor.font = 'georgia';
  profile.helmet.visor.graphicMirror = false;
  profile.helmet.visor.graphicX = 8;
  await fs.writeFile(path.join(dir, 'profiles.json'), JSON.stringify(document));
  const env = {...process.env, OPENGRIDXR_DATA_DIR:dir, OPENGRIDXR_TEST_MODE:'1'};
  delete env.ELECTRON_RUN_AS_NODE;
  const app = await _electron.launch({executablePath:process.env.OPENGRIDXR_TEST_EXE || require('electron'), args:process.env.OPENGRIDXR_TEST_EXE ? [] : [path.resolve('.')], env});
  try {
    const page = await app.firstWindow(), errors = [], remoteRequests = [];
    page.on('pageerror', error => errors.push(error.message));
    page.on('request', request => { if (/^https?:/.test(request.url())) remoteRequests.push(request.url()); });
    await page.getByRole('navigation', {name:'Main navigation'}).waitFor();
    await page.getByRole('group', {name:'View & crop section'}).getByRole('button', {name:'Helmet', exact:true}).click();
    await page.getByRole('group', {name:'Helmet editor section'}).getByRole('button', {name:'Graphic', exact:true}).click();
    const gsi = page.getByRole('button', {name:'Use GSI logo', exact:true});
    assert.equal(await gsi.count(), 1, 'The bundled sponsor is available without uploading a file');
    await gsi.click();
    const thumbnail = page.getByRole('img', {name:'Your visor graphic', exact:true});
    await thumbnail.waitFor();
    const graphic = await thumbnail.getAttribute('src');
    assert.match(graphic, /^data:image\/png;base64,/);
    const pixels = await thumbnail.evaluate(async image => {
      await image.decode();
      const canvas = Object.assign(document.createElement('canvas'), {width:image.naturalWidth, height:image.naturalHeight});
      const ctx = canvas.getContext('2d'); ctx.drawImage(image, 0, 0);
      const rgba = ctx.getImageData(0, 0, canvas.width, canvas.height).data;
      let opaque = 0, transparent = 0;
      for (let i=3; i<rgba.length; i+=4) { if (rgba[i]===255) opaque++; if (rgba[i]===0) transparent++; }
      return {width:canvas.width, height:canvas.height, opaque, transparent};
    });
    assert.deepEqual([pixels.width, pixels.height], [1024,393]);
    assert.ok(pixels.opaque>10000 && pixels.transparent>10000, 'The white logo retains transparent negative space');
    await page.getByRole('button', {name:'Save profile', exact:true}).click();
    await page.getByRole('button', {name:'Profile saved', exact:true}).waitFor();
    await fs.mkdir('test-results', {recursive:true});
    await page.screenshot({path:'test-results/gsi-decal.png', fullPage:true});
    const state = await page.evaluate(() => window.openGrid.load());
    const saved = state.document.profiles.find(p => p.id===state.document.selectedProfileId);
    assert.equal(saved.helmet.visor.graphic, graphic);
    assert.equal(saved.helmet.detail, 'performance', 'Selecting a logo does not increase texture resolution');
    assert.equal(saved.helmet.visor.graphicPlacement, 'sunstrip');
    assert.equal(saved.helmet.visor.graphicMirror, false);
    assert.equal(saved.helmet.visor.graphicX, 8);
    assert.equal(saved.helmet.visor.text, 'MY TEAM');
    assert.equal(saved.helmet.visor.font, 'georgia');
    assert.deepEqual(parseProfile(JSON.stringify({version:1, profile:saved})), saved, 'Embedded sponsor survives profile exchange');
    await page.reload();
    await page.getByRole('group', {name:'View & crop section'}).getByRole('button', {name:'Helmet', exact:true}).click();
    await page.getByRole('group', {name:'Helmet editor section'}).getByRole('button', {name:'Graphic', exact:true}).click();
    assert.equal(await thumbnail.getAttribute('src'), graphic);
    const layout = page.getByRole('group', {name:'Graphic layout', exact:true});
    await layout.getByRole('button', {name:'Custom', exact:true}).click();
    assert.equal(await gsi.count(), 0, 'Full visor artwork is not replaced by a sponsor preset');
    assert.equal(await thumbnail.getAttribute('src'), graphic);
    await layout.getByRole('button', {name:'Logo / strip', exact:true}).click();
    await page.getByLabel('Upload visor graphic', {exact:true}).setInputFiles({name:'custom.png', mimeType:'image/png', buffer:Buffer.from(graphic.split(',')[1], 'base64')});
    await page.getByText(/Graphic added at .+ pixels/).waitFor();
    await page.getByRole('button', {name:'Remove graphic', exact:true}).click();
    await page.waitForFunction(() => !document.querySelector('.helmet-editor-graphic-thumb'));
    assert.equal(remoteRequests.length, 0, 'Sponsor selection and reload are entirely offline');
    assert.deepEqual(errors, []);
    console.log('GSI offline selection, transparency, persistence, profile exchange, custom upload and removal passed.');
  } finally {
    await app.evaluate(({BrowserWindow}) => BrowserWindow.getAllWindows().forEach(window => window.destroy())).catch(() => {});
    await app.close();
  }
})().catch(error => {console.error(error); process.exitCode=1;});
