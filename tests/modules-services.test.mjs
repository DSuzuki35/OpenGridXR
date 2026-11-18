import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
const require = createRequire(import.meta.url);
const { createModuleServices } = require('../desktop/module-services.cjs');
const { createBenchmarkController } = require('../desktop/benchmark-controller.cjs');

test('optional services stay dormant, close on disable, and can be recreated', async () => {
  let enabled = true, created = 0, resources = 0;
  const services = createModuleServices({isEnabled: () => enabled, factories: { telemetry: () => {
    created++; resources++;
    return { close() { resources--; } };
  }}});
  assert.equal(created, 0);
  const [first, concurrent] = await Promise.all([services.get('telemetry'), services.get('telemetry')]);
  assert.equal(first, concurrent);
  assert.equal(resources, 1);
  enabled = false;
  await services.close('telemetry');
  assert.equal(resources, 0);
  await assert.rejects(services.get('telemetry'), /disabled/i);
  enabled = true;
  assert.notEqual(await services.get('telemetry'), first);
  assert.equal(resources, 1);
  await services.closeAll();
  assert.equal(resources, 0);
  await assert.rejects(services.get('telemetry'), /closed/i);
});

test('disabling during asynchronous creation disposes the late controller', async () => {
  let resolve, enabled = true, resources = 0;
  const services = createModuleServices({isEnabled: () => enabled, factories: { spatial: async () => {
    await new Promise(done => { resolve = done; });
    resources++;
    return { close() { resources--; } };
  }}});
  const pending = services.get('spatial');
  const rejected = assert.rejects(pending, /disabled|stopped/i);
  await new Promise(done => setImmediate(done));
  enabled = false;
  const closing = services.close('spatial');
  resolve();
  await Promise.all([closing, rejected]);
  assert.equal(resources, 0);
  assert.equal(services.peek('spatial'), undefined);
});

test('shutdown waits for recorder finalization and never creates unused controllers', async () => {
  let finish, finalized = false;
  const services = createModuleServices({isEnabled: () => true, factories: {
    benchmark: () => ({async close() { await new Promise(done => { finish = done; }); finalized = true; }}),
    spatial: () => { throw new Error('Unused spatial controller should stay unloaded'); },
  }});
  await services.get('benchmark');
  const closing = services.closeAll();
  await new Promise(done => setImmediate(done));
  assert.equal(finalized, false);
  finish();
  await closing;
  assert.equal(finalized, true);
});

test('disabling the benchmark finalizes its real report and removes its native lease', async t => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'opengrid-module-recording-'));
  t.after(() => fs.rm(root,{recursive:true,force:true}));
  const sessionId = '123-100-modules', timestamp = Date.now();
  await fs.mkdir(path.join(root,'status'),{recursive:true});
  await fs.writeFile(path.join(root,'status',`${sessionId}.json`),JSON.stringify({protocolVersion:1,sessionId,processId:123,application:'iRacingSim64DX11.exe',updatedAtUnixMilliseconds:timestamp,capabilities:{benchmark:true}}));
  let enabled = true;
  const reportPath = path.join(root,'report.json');
  const services = createModuleServices({isEnabled: () => enabled, factories: {benchmark: () => createBenchmarkController({
    root, isAlive: () => true, telemetry: async () => ({record:null}),
    onReport: report => fs.writeFile(reportPath,JSON.stringify(report)),
  })}});
  t.after(() => services.closeAll());
  const recorder = await services.get('benchmark');
  await recorder.start({id:'profile',name:'Saved profile',game:'iracing'});
  assert.equal(recorder.status().running,true);
  enabled = false;
  await services.close('benchmark');
  assert.equal(recorder.status().running,false);
  assert.equal(JSON.parse(await fs.readFile(reportPath,'utf8')).game,'iracing');
  await assert.rejects(fs.access(path.join(root,'control',`${sessionId}_benchmark-request.json`)));
});
