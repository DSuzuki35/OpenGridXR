const {revealAppearance} = require('./appearance-access.cjs');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { _electron } = require('playwright-core');

(async () => {
  const { createDocument } = await import('../src/model.mjs');
  const { createSpatialOverlay } = await import('../src/spatial-overlays.mjs');
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'opengrid-accessibility-'));
  const profiles = createDocument();
  const profile = profiles.profiles.find(p => p.game === 'lmu');
  profiles.selectedProfileId = profile.id;
  profile.lmuRendering = { ...profile.lmuRendering, mode: 'quad', provider: 'qvfr' };
  profile.spatialOverlays.items = [{ ...createSpatialOverlay(), wheelModel: "hyper-p1", wheelLights: true }];
  await fs.writeFile(path.join(dir, 'profiles.json'), JSON.stringify(profiles));
  const env = { ...process.env, OPENGRIDXR_DATA_DIR: dir, OPENGRIDXR_TEST_MODE: '1' };
  delete env.ELECTRON_RUN_AS_NODE;
  let app;
  try {
    app = await _electron.launch({
      executablePath: process.env.OPENGRIDXR_TEST_EXE || require('electron'),
      args: process.env.OPENGRIDXR_TEST_EXE ? [] : [path.resolve('.')],
      env,
    });
    const page = await app.firstWindow(); await revealAppearance(page);
    await page.locator('.nav-item').first().waitFor();
    await page.getByRole('button', { name: 'Profile saved', exact: true }).waitFor();
    await page.evaluate(await fs.readFile(require.resolve('axe-core/axe.min.js'), 'utf8'));
    const nav = page.getByRole('navigation', { name: 'Main navigation' });
    const names = await nav.getByRole('button').evaluateAll(nodes => nodes.map(n => n.getAttribute('aria-label')));
    assert.ok(names.includes('LMU QVFR & VRS') && names.includes('Spatial overlays'));
    const failures = [];
    for (const viewport of [{width: 1440, height: 900}, {width: 1050, height: 720}]) {
      await page.setViewportSize(viewport);
      for (const name of [...names, "Helmet"]) {
        const pageName = name === "Helmet" ? "View & crop" : name;
        await nav.getByRole('button', { name:pageName, exact: true }).click();
        if (name === 'Helmet') await page.getByRole('group',{name:'View & crop section'}).getByRole('button',{name}).click();
        await page.getByRole('heading', { name:pageName, level:1, exact:true }).waitFor();
        await page.getByText('Loading module…', { exact: true }).waitFor({state:'hidden'});
        const violations = await page.evaluate(async () => {
          const { violations } = await window.axe.run(document, { runOnly: {
            type: 'tag', values: ['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa'],
          } });
          return violations.flatMap(v => v.nodes.map(n => ({ rule: v.id, target: n.target, problem: n.failureSummary })));
        });
        if (violations.length) failures.push({ page: name, viewport, violations });
      }
    }
    assert.deepEqual(failures, []);
    console.log(`Accessibility checks passed across ${names.length} editor pages.`);
  } finally {
    if (app) {
      await app.evaluate(({BrowserWindow}) => BrowserWindow.getAllWindows().forEach(w => w.destroy()));
      await app.close();
    }
    await fs.rm(dir, { recursive: true, force: true });
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
