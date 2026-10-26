import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
let createRuntimeOptics;
try { ({ createRuntimeOptics } = require('../desktop/runtime-optics.cjs')); } catch (e) { if (e.code !== 'MODULE_NOT_FOUND') throw e; }
const original = Buffer.from('\ufeff{\r\n  "extra": {"large":9007199254740993,"unicode":"café"},\r\n  "dreamAir": { "eyeRotation":2, "fovZoom":1, "flatFovZoom":1, "autoEyeRotation":false, "distortionZoom":1.02 },\r\n  "dreamAirSE": {"eyeRotation":3,"fovZoom":1}\r\n}\r\n');
async function fixture(t, ensureStopped = async () => {}) {
  assert.equal(typeof createRuntimeOptics, 'function', 'runtime optics adapter must be implemented');
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'opengrid-optics-'));
  t.after(() => fs.rm(root, { recursive:true, force:true }));
  const neutral = path.join(root, 'CustomHeadset', 'settings.json');
  const pimax = path.join(root, 'Pimax', 'CustomHeadset', 'settings.json');
  for (const file of [neutral,pimax]) { await fs.mkdir(path.dirname(file), { recursive:true }); await fs.writeFile(file,original); }
  const backupDir = path.join(root,'backups');
  const api = createRuntimeOptics({appData:root, backupDir, ensureStopped});
  return {root,neutral,pimax,backupDir,api};
}
const select = (api, variant='neutral', headset='dreamAir') => api.choose({variant,headset});

test('discovery never guesses an installation or headset; selection is restricted to known targets', async t => {
  const s = await fixture(t), state = await s.api.inspect();
  assert.equal(state.selected,null);
  assert.equal(state.candidates.length,2);
  assert.deepEqual(state.candidates.map(c=>c.path),[s.neutral,s.pimax]);
  assert.equal(state.capabilities.pimaxNative.supported,false);
  await assert.rejects(s.api.preview({eyeRotation:4},'x'),/select/i);
  await assert.rejects(s.api.choose({variant:'../arbitrary',headset:'dreamAir'}),/target|variant/i);
  await assert.rejects(s.api.choose({variant:'neutral',headset:'crystal'}),/target|headset/i);
  const chosen = await select(s.api,'pimax','dreamAirSE');
  assert.equal(chosen.path,s.pimax); assert.equal(chosen.values.eyeRotation,3);
  await fs.unlink(s.neutral);
  assert.equal((await select(s.api)).available,false);
  await assert.rejects(fs.access(s.neutral));
});

test('review is read-only; scoped apply preserves every unrelated byte and exact backup; restore is reviewed', async t => {
  const s = await fixture(t), state = await select(s.api);
  const review = await s.api.preview({eyeRotation:4.1,autoEyeRotation:true},state.revision);
  assert.deepEqual(review.changes.map(c=>[c.key,c.before,c.after]),[['dreamAir.eyeRotation',2,4.1],['dreamAir.autoEyeRotation',false,true]]);
  assert.deepEqual(await fs.readFile(s.neutral),original);
  await assert.rejects(fs.access(s.backupDir));
  const applied = await s.api.apply(review.token);
  const edited = Buffer.from(original.toString().replace('"eyeRotation":2','"eyeRotation":4.1').replace('"autoEyeRotation":false','"autoEyeRotation":true'));
  assert.deepEqual(await fs.readFile(s.neutral),edited);
  assert.deepEqual(await fs.readFile(s.pimax),original);
  assert.deepEqual(await fs.readFile(path.join(s.backupDir,applied.backups[0].id)),original);
  await assert.rejects(s.api.apply(review.token),/review/i);
  const restore = await s.api.previewRestore(applied.backups[0].id,applied.revision);
  assert.equal(restore.kind,'restore'); assert.equal(restore.wholeFile,true);
  assert.deepEqual(await fs.readFile(s.neutral),edited);
  const restored = await s.api.apply(restore.token);
  assert.deepEqual(await fs.readFile(s.neutral),original);
  assert.equal(restored.backups.length,2);
  const backups = await Promise.all(restored.backups.map(b=>fs.readFile(path.join(s.backupDir,b.id))));
  assert(backups.some(b=>b.equals(edited)));
});

test('stale review, runtime running, target changes and invalid tokens cannot write or create backups', async t => {
  let running = false;
  const s = await fixture(t,async()=>{if(running)throw new Error('Close SteamVR first');});
  let state = await select(s.api), review = await s.api.preview({fovZoom:0.95},state.revision);
  await fs.appendFile(s.neutral,' ');
  await assert.rejects(s.api.apply(review.token),/changed/i);
  state = await select(s.api); review = await s.api.preview({fovZoom:0.95},state.revision);
  running = true;
  await assert.rejects(s.api.apply(review.token),/SteamVR/);
  running = false;
  review = await s.api.preview({fovZoom:0.95},state.revision);
  await select(s.api,'pimax');
  await assert.rejects(s.api.apply(review.token),/review/i);
  await assert.rejects(fs.access(s.backupDir));
});

test('validation refuses unsupported fields and invalid proposed values while retaining unusual existing values', async t => {
  const s = await fixture(t);
  await fs.writeFile(s.neutral,original.toString().replace('"eyeRotation":2','"eyeRotation":99'));
  const state = await select(s.api);
  assert.equal(state.values.eyeRotation,99);
  for (const changes of [{eyeRotation:Infinity},{eyeRotation:NaN},{eyeRotation:16},{fovZoom:0},{flatFovZoom:1.16},{autoEyeRotation:1},{maxFovX:100},{distortionZoom:1},{eyeRotation:'4'},[]])
    await assert.rejects(s.api.preview(changes,state.revision),/unsupported|range|finite|boolean|invalid/i);
  const review = await s.api.preview({fovZoom:0.95},state.revision);
  await s.api.apply(review.token);
  assert.equal((await s.api.inspect()).values.eyeRotation,99);
});

test('malformed, duplicate, overdeep, oversized and wrong headset objects are rejected without writes', async t => {
  const s = await fixture(t);
  for (const content of ['{','[]','{"dreamAir":[]}','{"dreamAirSE":{}}','{"dreamAir":{"eyeRotation":2,"eye\\u0052otation":3}}','{"dreamAir":{},"other":{"x":1,"x":2}}','{"dreamAir":{},"nested":'+ '['.repeat(70)+'0'+']'.repeat(70)+'}',' '.repeat(1024*1024+1)]) {
    await fs.writeFile(s.neutral,content);
    await assert.rejects(select(s.api),/JSON|object|duplicate|depth|MiB|headset/i);
    assert.equal(await fs.readFile(s.neutral,'utf8'),content);
  }
  await assert.rejects(fs.access(s.backupDir));
});

test('missing fields can be added without rewriting other targets; full restore exposes unrelated changes and rejects stale hashes', async t => {
  const s = await fixture(t), state = await select(s.api,'pimax','dreamAirSE');
  const review = await s.api.preview({flatFovZoom:0.9},state.revision);
  const applied = await s.api.apply(review.token);
  const body = await fs.readFile(s.pimax,'utf8');
  assert.equal(JSON.parse(body.slice(1)).dreamAirSE.flatFovZoom,0.9);
  assert.match(body,/"large":9007199254740993/);
  await fs.writeFile(s.pimax,body.replace('"unicode":"café"','"unicode":"changed"'));
  const current = await s.api.inspect();
  const restore = await s.api.previewRestore(applied.backups[0].id,current.revision);
  assert(restore.changes.some(c=>c.key === 'extra.unicode' && c.before === 'changed' && c.after === 'café'));
  await fs.appendFile(s.pimax,' ');
  await assert.rejects(s.api.apply(restore.token),/changed/i);
  await assert.rejects(s.api.previewRestore('../settings.json',current.revision),/backup/i);
});

test('a failed backup leaves the original intact', async t => {
  const s = await fixture(t), state = await select(s.api);
  await fs.writeFile(s.backupDir,'not a directory');
  const review = await s.api.preview({eyeRotation:5},state.revision);
  await assert.rejects(s.api.apply(review.token));
  assert.deepEqual(await fs.readFile(s.neutral),original);
});

test('restore distinguishes a removed null field from an unchanged value', async t => {
  const s = await fixture(t), state = await select(s.api);
  const review = await s.api.preview({eyeRotation:5},state.revision);
  const applied = await s.api.apply(review.token);
  const bytes = await fs.readFile(s.neutral,'utf8');
  await fs.writeFile(s.neutral,bytes.replace('"unicode":"café"','"unicode":"café","added":null'));
  const current = await s.api.inspect();
  const restore = await s.api.previewRestore(applied.backups[0].id,current.revision);
  const removed = restore.changes.find(c=>c.key==='extra.added');
  assert.equal(removed.beforePresent,true);
  assert.equal(removed.afterPresent,false);
});

test('atomic replacement failure preserves target and exact backup and cleans the sibling temporary file', async t => {
  const s = await fixture(t), state = await select(s.api);
  const review = await s.api.preview({eyeRotation:5},state.revision);
  const rename = fs.rename;
  fs.rename = async (from,to) => { if(to===s.neutral) throw Object.assign(new Error('Injected target lock'),{code:'EIO'}); return rename(from,to); };
  try { await assert.rejects(s.api.apply(review.token),/target lock/); }
  finally { fs.rename = rename; }
  assert.deepEqual(await fs.readFile(s.neutral),original);
  const backups = await fs.readdir(s.backupDir);
  assert.equal(backups.length,1);
  assert.deepEqual(await fs.readFile(path.join(s.backupDir,backups[0])),original);
  assert.deepEqual(await fs.readdir(path.dirname(s.neutral)),['settings.json']);
});

test('a file changed during offline checks cannot be overwritten and concurrent target switches are rejected', async t => {
  let release;
  const gate = new Promise(resolve=>{release=resolve;});
  const s = await fixture(t,async()=>gate);
  const state = await select(s.api), review = await s.api.preview({eyeRotation:5},state.revision);
  const applying = s.api.apply(review.token);
  await assert.rejects(select(s.api,'pimax'),/already running/i);
  await fs.appendFile(s.neutral,' ');
  release();
  await assert.rejects(applying,/changed/i);
  assert.deepEqual(await fs.readFile(s.neutral),Buffer.concat([original,Buffer.from(' ')]));
  await assert.rejects(fs.access(s.backupDir));
});

test('the reviewed diff distinguishes JSON strings from numeric settings', async t => {
  const s = await fixture(t);
  await fs.writeFile(s.neutral,original.toString().replace('"eyeRotation":2','"eyeRotation":"5"'));
  const state = await select(s.api), review = await s.api.preview({eyeRotation:5},state.revision);
  assert.equal(review.changes[0].beforeJson,'"5"');
  assert.equal(review.changes[0].afterJson,'5');
});
