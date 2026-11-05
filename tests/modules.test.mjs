import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { createRequire } from 'node:module';
import { createDocument, validateDocument } from '../src/model.mjs';
import { MODULES, moduleDefaults, validateModuleSettings, effectiveDocument, moduleForAction } from '../src/modules.mjs';
const require = createRequire(import.meta.url);
const { createModuleSettings } = require('../desktop/module-settings.cjs');
const { toRuntimeIni, toEnhancementsConfig } = require('../desktop/runtime-config.cjs');

test('disabled modules cannot reach native configuration and saved profiles remain intact', () => {
  const doc = createDocument();
  for (const p of doc.profiles) {
    p.framegen.enabled = p.overlay.enabled = p.helmet.enabled = p.telemetry.enabled = p.spatialOverlays.enabled = true;
    p.sharpening.mode = 'cas'; p.stream.preserveFullFrame = true;
    p.view.crop.left = 15; p.mask.enabled = true;
  }
  const original = structuredClone(doc);
  const enabled = Object.fromEntries(MODULES.map(({id}) => [id, false]));
  const effective = effectiveDocument(doc, enabled);
  validateDocument(effective);
  assert.deepEqual(doc, original);
  assert.equal(effective.profiles[0].telemetry.enabled, false);
  assert.equal(effective.profiles[0].inputBindings.enabled, false);
  assert.equal(effective.profiles[0].view.crop.left, 15);
  assert.equal(effective.profiles[0].mask.enabled, true);
  const ini = toRuntimeIni(effective);
  for (const line of ['EnhancementsEnabled=0', 'ColorCorrectionEnabled=0', 'SharpeningMode=off', 'Mode=driving', 'ExtraLeft=0', 'MaskEnabled=1']) assert.ok(ini.includes(line), line);
  const engine = toEnhancementsConfig(effective, 'iracing');
  assert.equal(engine.core.enabled, false);
  assert.equal(engine.modules.framegen.enabled, false);
  assert.deepEqual(engine.modules.framegen.toggleBinding, {type:'none'});
  assert.deepEqual(engine.core.osd.toggleBinding, {type:'none'});
  assert.deepEqual(effectiveDocument(doc, moduleDefaults()), doc);
});

test('module validation rejects malformed settings and missing benchmark dependency', () => {
  const value = {version:1, enabled:moduleDefaults()};
  assert.deepEqual(validateModuleSettings(value), value);
  for (const invalid of [null, {}, {...value,version:2}, {...value,enabled:{...value.enabled,stream:'false'}}, {...value,enabled:{...value.enabled,unknown:true}}, {...value,enabled:{...value.enabled,telemetry:false}}]) assert.throws(() => validateModuleSettings(invalid));
  assert.equal(moduleForAction('spatialOverlays.11111111-1111-4111-8111-111111111111.enabled'), 'spatial');
  assert.equal(moduleForAction('overlay.layout'), 'overlay');
  assert.equal(moduleForAction('mask.enabled'), undefined);
});

test('module preferences persist independently from profiles and preserve previous settings', async t => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'opengrid-modules-'));
  t.after(() => fs.rm(dir,{recursive:true,force:true}));
  const file = path.join(dir,'module-settings.json');
  const profiles = path.join(dir,'profiles.json');
  await fs.writeFile(profiles,'original profiles');
  const settings = await createModuleSettings(dir);
  assert.equal(settings.get().enabled.helmet,true);
  const next = {version:1,enabled:{...moduleDefaults(),helmet:false}};
  await settings.save(next);
  await settings.save({...next,enabled:{...next.enabled,stream:false}});
  assert.deepEqual(JSON.parse(await fs.readFile(`${file}.bak`,'utf8')),next);
  assert.equal((await createModuleSettings(dir)).get().enabled.stream,false);
  assert.equal(await fs.readFile(profiles,'utf8'),'original profiles');
});

test('corrupt preferences are preserved and optional modules fail closed', async t => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'opengrid-modules-bad-'));
  t.after(() => fs.rm(dir,{recursive:true,force:true}));
  const file = path.join(dir,'module-settings.json');
  await fs.writeFile(file,'broken json');
  const settings = await createModuleSettings(dir);
  assert.match(settings.get().error,/module settings/i);
  assert.equal(settings.get().enabled.stream,false);
  assert.equal(await fs.readFile(file,'utf8'),'broken json');
  await settings.save({version:1,enabled:moduleDefaults()});
  assert.equal(await fs.readFile(`${file}.bak`,'utf8'),'broken json');
});

test('an editor change during module save leaves current preferences unchanged', async t => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'opengrid-modules-edit-'));
  t.after(() => fs.rm(dir,{recursive:true,force:true}));
  const settings = await createModuleSettings(dir);
  await settings.save({version:1,enabled:moduleDefaults()});
  let checks = 0;
  await assert.rejects(settings.save({version:1,enabled:{...moduleDefaults(),helmet:false}}, () => {
    if (++checks === 2) throw new Error('The editor changed');
  }), /editor changed/);
  assert.equal(settings.get().enabled.helmet,true);
  assert.equal(JSON.parse(await fs.readFile(path.join(dir,'module-settings.json'),'utf8')).enabled.helmet,true);
});
