import { test } from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { createRequire } from 'node:module';
import path from 'node:path';
const require = createRequire(import.meta.url);
const { launchLmuOffline } = require('../desktop/lmu-offline.cjs');

function fixture() {
  const calls = [];
  return { calls, options: {
    environment: {PATH:'fixture'},
    executable: path.resolve('fixture/Le Mans Ultimate.exe'),
    ensureStopped: async () => calls.push('closed'),
    stat: async () => ({ isFile: () => true }),
    spawn: (...args) => {
      calls.push(args);
      const child = new EventEmitter();
      child.unref = () => calls.push('unref');
      queueMicrotask(() => child.emit('spawn'));
      return child;
    },
  } };
}
test('LMU offline starts directly in OpenXR with its working directory and no shell', async () => {
  const { calls, options } = fixture();
  await launchLmuOffline(options);
  assert.equal(calls[0], 'closed');
  assert.deepEqual(calls[1], [options.executable, ['+XR'], {
    cwd: path.dirname(options.executable), shell: false, detached: true,
    windowsHide: false, stdio: 'ignore',
    env: {PATH:'fixture',OPENGRIDXR_LMU_INTEGRATED:'0',LMU_QV_DYNAMIC_FOCUS:'0'},
  }]);
  assert.equal(calls[2], 'unref');
});
test('ordinary LMU launch cannot inherit an enabled integrated adapter or capture',async()=>{
  const {calls,options}=fixture();
  await launchLmuOffline({...options,environment:{PATH:'keep',OPENGRIDXR_LMU_INTEGRATED:'1',LMU_QV_MODE:'quad',lmu_qv_capture_trigger:'stale',OPENGRIDXR_LMU_PROVIDER:'qvfr'}});
  assert.deepEqual(calls[1][2].env,{PATH:'keep',OPENGRIDXR_LMU_INTEGRATED:'0',LMU_QV_DYNAMIC_FOCUS:'0'});
});
test('rejects protected launcher and arbitrary files without starting a process', async () => {
  for (const filename of ['start_protected_game.exe', 'cmd.exe', 'Le Mans Ultimate.exe.bat']) {
    const { calls, options } = fixture();
    await assert.rejects(launchLmuOffline({ ...options, executable: path.resolve(filename) }), /game executable/i);
    assert.equal(calls.filter(Array.isArray).length, 0);
  }
});
test('running game, missing file and launch errors reach the caller', async () => {
  const { options } = fixture();
  await assert.rejects(launchLmuOffline({ ...options, ensureStopped: async () => { throw new Error('Close the game'); } }), /Close the game/);
  await assert.rejects(launchLmuOffline({ ...options, stat: async () => { throw new Error('ENOENT'); } }), /not found/);
  await assert.rejects(launchLmuOffline({ ...options, spawn: () => {
    const child = new EventEmitter();
    queueMicrotask(() => child.emit('error', new Error('EACCES')));
    return child;
  } }), /EACCES/);
});
