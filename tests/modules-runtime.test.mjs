import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import {EventEmitter} from 'node:events';
import {createRequire} from 'node:module';
import {runInNewContext} from 'node:vm';
import {promisify} from 'node:util';
import {createDocument} from '../src/model.mjs';
import {effectiveDocument, moduleDefaults} from '../src/modules.mjs';

test('runtime config and output use module overrides after checking the original saved profile', async t => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(),'opengrid-module-runtime-'));
  t.after(() => fs.rm(dir,{recursive:true,force:true}));
  const file = new URL('../desktop/runtime.cjs',import.meta.url);
  const require = createRequire(file);
  let started = 0, killed = 0;
  const execFile = () => {};
  execFile[promisify.custom] = async (exe,args) => ({stdout:args[0]==='--status'
    ? JSON.stringify({machineDirectory:dir,machine:{present:true,enabled:true},legacyMachine:{present:false,enabled:false},user:{present:false,enabled:false}})
    : 'REG_DWORD    0x0'});
  const module = {exports:{}};
  runInNewContext(`(function(require,module,process){${await fs.readFile(file,'utf8')}\n})`)(
    name => name === 'node:child_process' ? {execFile,spawn: () => {
      started++;
      const child = new EventEmitter(); child.exitCode = null;
      child.kill = () => { killed++; child.exitCode = 0; };
      process.nextTick(() => child.emit('spawn'));
      return child;
    }} : require(name), module, {platform:'win32',kill:process.kill,env:{}});
  const enabled = {...moduleDefaults(),helmet:false,overlay:false,framegen:false};
  let prepared;
  const runtime = module.exports.createRuntime({sourceDir:dir,runtimeDir:dir,
    transformDocument: doc => effectiveDocument(doc,enabled), outputEnabled: () => enabled.stream,
    prepareAssets: async doc => {prepared=doc;}});
  const doc = createDocument(), p = doc.profiles[0];
  p.helmet.enabled = p.overlay.enabled = p.framegen.enabled = true;
  await runtime.writeConfig(doc);
  assert.equal(prepared.profiles[0].helmet.enabled,false);
  assert.match(await fs.readFile(path.join(dir,'OpenGridXR.ini'),'utf8'),/^EnhancementsEnabled=0$/m);
  await assert.rejects(runtime.launch(doc,{...p,name:'unsaved'},[]),/Save this profile/);
  await runtime.launch(doc,p,[{id:'1',primary:true,bounds:{x:0,y:0,width:1920,height:1080}}]);
  assert.equal(started,1);
  assert.equal(p.helmet.enabled,true);
  runtime.closeOutput();
  assert.equal(killed,1);
  enabled.stream = false;
  await assert.rejects(runtime.launch(doc,p,[]),/disabled/i);
  assert.equal(started,1);
});
