import test from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { createUpdateController, ensureUpdateInstall } = require('../desktop/update-controller.cjs');

function fixture(options = {}) {
  const updater = new EventEmitter();
  let checks = 0, downloads = 0, installs = 0, stored = {};
  updater.setFeedURL = value => { updater.feed = value; };
  updater.checkForUpdates = async () => { checks++; updater.emit('update-available', {version:'0.3.0'}); };
  updater.downloadUpdate = async () => { downloads++; updater.emit('download-progress', {percent:50}); updater.emit('update-downloaded', {version:'0.3.0'}); };
  updater.quitAndInstall = () => { installs++; };
  const controller = createUpdateController({ updater, enabled:true, currentVersion:'0.2.1', readPreferences:async()=>stored, writePreferences:async value=>{stored=value;}, ensureCanInstall:async()=>true, ...options });
  return {updater,controller, counts:()=>({checks,downloads,installs}), stored:()=>stored};
}

test('checks fixed stable GitHub feed without downloading or installing on quit', async()=>{
  const f=fixture(); await f.controller.check();
  assert.equal(f.updater.autoDownload,false); assert.equal(f.updater.autoInstallOnAppQuit,false);
  assert.equal(f.updater.allowPrerelease,false); assert.equal(f.updater.allowDowngrade,false);
  assert.equal(f.updater.feed.repo,'OpenGridXR'); assert.equal(f.updater.feed.owner,'DSuzuki35');
  assert.deepEqual(f.counts(),{checks:1,downloads:0,installs:0});
  assert.equal(f.controller.status().phase,'available');
  await f.controller.decide('later'); assert.equal(f.controller.status().prompt,false);
  assert.equal(f.counts().downloads,0);
});
test('skip persists across restart and manual check can offer skipped version again',async()=>{
  const f=fixture(); await f.controller.check(); await f.controller.decide('skip');
  assert.equal(f.stored().skippedVersion,'0.3.0');
  const next=fixture({readPreferences:async()=>f.stored()}); await next.controller.check();
  assert.equal(next.controller.status().prompt,false);
  await next.controller.check(true); assert.equal(next.controller.status().prompt,true);
});
test('download requires choice, install requires readiness and confirmation',async()=>{
  let permitted=false;
  const f=fixture({ensureCanInstall:async()=>{if(!permitted) throw new Error('Save your profile first.'); return true;}});
  await assert.rejects(()=>f.controller.install(),/not ready/i);
  await f.controller.check(); await f.controller.decide('download');
  assert.equal(f.controller.status().phase,'downloaded'); assert.equal(f.counts().installs,0);
  await assert.rejects(()=>f.controller.install(),/Save your profile/);
  assert.equal(f.controller.status().phase,'downloaded'); permitted=true;
  await f.controller.install(); assert.equal(f.counts().installs,1);
});
test('canceling install confirmation preserves downloaded update',async()=>{
  const f=fixture({ensureCanInstall:async()=>false}); await f.controller.check(); await f.controller.decide('download');
  await f.controller.install(); assert.equal(f.counts().installs,0); assert.equal(f.controller.status().phase,'downloaded');
});
test('checks and duplicate actions cannot replace an active download',async()=>{
  const f=fixture(); let release;
  f.updater.downloadUpdate=()=>new Promise(resolve=>{release=resolve;});
  await f.controller.check(); const pending=f.controller.decide('download');
  await f.controller.check(true); await assert.rejects(()=>f.controller.decide('download'),/available/i);
  assert.equal(f.counts().checks,1); assert.equal(f.controller.status().phase,'downloading');
  release(); await pending;
});
test('disabled source/test builds cannot check, download or install',async()=>{
  const f=fixture({enabled:false}); await f.controller.check();
  assert.equal(f.controller.status().phase,'unsupported');
  await assert.rejects(()=>f.controller.decide('download'));
  await assert.rejects(()=>f.controller.install());
  assert.deepEqual(f.counts(),{checks:0,downloads:0,installs:0});
});
test('network errors remain recoverable, no unhandled error events',async()=>{
  const f=fixture(); f.updater.checkForUpdates=async()=>{throw new Error('offline');};
  await f.controller.check(); assert.equal(f.controller.status().phase,'error');
  f.updater.emit('error',new Error('checksum mismatch'));
  assert.match(f.controller.status().message,/checksum/); assert.equal(f.counts().installs,0);
});
test('closing controller detaches listeners and stops background checks',async()=>{
  const f=fixture(); f.controller.close(); await f.controller.check();
  assert.equal(f.counts().checks,0); assert.equal(f.updater.listenerCount('update-available'),0);
});
test('editing during the final stopped-process check blocks installation',async()=>{
  let ready=true, scans=0;
  await assert.rejects(()=>ensureUpdateInstall({ isReady:()=>ready, confirm:async()=>true,
    ensureStopped:async()=>{ if(++scans===2) ready=false; } }),/Save your profile/);
  assert.equal(scans,2);
});
