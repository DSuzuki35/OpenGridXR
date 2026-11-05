import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import {createRequire} from 'node:module';
import {runInNewContext} from 'node:vm';
import {promisify} from 'node:util';
import {createDocument} from '../src/model.mjs';

async function fixture(t, initial) {
  const dir=await fs.mkdtemp(path.join(os.tmpdir(),'opengrid-registration-'));
  t.after(()=>fs.rm(dir,{recursive:true,force:true}));
  const source=path.join(dir,'source'), runtimeDir=path.join(dir,'runtime'), machineDirectory=path.join(dir,'machine');
  await fs.mkdir(source); await fs.mkdir(runtimeDir); await fs.mkdir(machineDirectory);
  for(const file of ['OpenGridXR.dll','OpenGridXR-Output.exe','OpenGridXR-Enhancements.dll','OpenGridXR-Connection.exe','OpenGridXR.json']) {
    const bytes=file.endsWith('.json')?JSON.stringify({api_layer:{name:'XR_APILAYER_OPENGRIDXR',library_path:'.\\OpenGridXR.dll'}}):'fixture';
    await fs.writeFile(path.join(source,file),bytes); await fs.writeFile(path.join(runtimeDir,file),bytes); await fs.writeFile(path.join(machineDirectory,file),bytes);
  }
  const registry={...initial}, calls=[];
  let cancel=false, noWrite=false, readDenied=false;
  const execute=()=>{};
  execute[promisify.custom]=async(exe,args)=>{
    calls.push({exe,args});
    if(exe==='powershell.exe')return {stdout:''}; // process-idle check
    if(exe==='reg.exe') {
      const hive=args[1].slice(0,4);
      if(args[0]==='query') {
        if(!(hive in registry))throw Object.assign(new Error('missing'),{code:1});
        return {stdout:`${args[3]}    REG_DWORD    0x${registry[hive].toString(16)}`};
      }
      if(args[0]==='add')registry[hive]=Number(args[args.indexOf('/d')+1]);
      else if(args[0]==='delete')delete registry[hive];
      return {stdout:''};
    }
    if(path.basename(exe)==='OpenGridXR-Connection.exe') {
      if(args[0]==='--status') {
        if(readDenied)throw Object.assign(new Error('Access denied'),{code:5});
        const state=key=>({present:key in registry,enabled:registry[key]===0});
        return {stdout:JSON.stringify({machineDirectory,machine:state('HKLM'),legacyMachine:state('legacyHKLM'),user:state('HKCU')})};
      }
      if(cancel)throw Object.assign(new Error('cancelled'),{code:1223});
      if(!noWrite) {if(args[0]==='--install')registry.HKLM=0;else if(args[0]==='--remove')delete registry.HKLM;else assert.fail('Unexpected helper operation');delete registry.legacyHKLM;}
      return {stdout:''};
    }
    assert.fail(`Unexpected executable ${exe}`);
  };
  const file=new URL('../desktop/runtime.cjs',import.meta.url), realRequire=createRequire(file), module={exports:{}};
  runInNewContext(`(function(require,module,process){${await fs.readFile(file,'utf8')}\n})`)(name=>name==='node:child_process'?{execFile:execute}:realRequire(name),module,{platform:'win32',env:{},kill:process.kill});
  const runtime=module.exports.createRuntime({sourceDir:source,runtimeDir});
  return {runtime,runtimeDir,machineDirectory,registry,calls,doc:createDocument(),cancel:()=>{cancel=true;},noWrite:()=>{noWrite=true;},readDenied:()=>{readDenied=true;}};
}

test('legacy user-only registration requires repair even when all installed files match',async t=>{
  const f=await fixture(t,{HKCU:0});const state=await f.runtime.status();
  assert.equal(state.registered,true);assert.equal(state.updateRequired,true);assert.equal(state.registrationRepairRequired,true);
});
test('install enables machine registration before retiring the legacy user entry',async t=>{
  const f=await fixture(t,{HKCU:0});await f.runtime.install(f.doc);
  assert.deepEqual(f.registry,{HKLM:0});assert.equal((await f.runtime.status()).updateRequired,false);
  assert.ok(f.calls.findIndex(c=>c.args[0]==='--install')<f.calls.findIndex(c=>c.args[0]==='delete'));
});
test('cancelled elevation preserves the legacy connection and reports cancellation',async t=>{
  const f=await fixture(t,{HKCU:0});f.cancel();
  await assert.rejects(f.runtime.install(f.doc),/cancel/i);assert.deepEqual(f.registry,{HKCU:0});
});
test('failed machine readback cannot retire a working legacy registration',async t=>{
  const f=await fixture(t,{HKCU:0});f.noWrite();
  await assert.rejects(f.runtime.install(f.doc),/verif|confirm/i);assert.deepEqual(f.registry,{HKCU:0});
});
test('remove deletes both owned registrations including disabled ones',async t=>{
  const f=await fixture(t,{HKLM:1,HKCU:1});
  assert.equal((await f.runtime.status()).registrationPresent,true);
  await f.runtime.remove();assert.deepEqual(f.registry,{});
});
test('cancelled machine removal leaves both registrations in place',async t=>{
  const f=await fixture(t,{HKLM:0,HKCU:0});f.cancel();
  await assert.rejects(f.runtime.remove(),/cancel/i);assert.deepEqual(f.registry,{HKLM:0,HKCU:0});
});
test('a stale machine registration can be removed after the runtime folder was deleted',async t=>{
  const f=await fixture(t,{HKLM:0});
  await fs.rm(f.runtimeDir,{recursive:true});
  await f.runtime.remove();assert.deepEqual(f.registry,{});
});
test('registry access denied cannot be reported as absent or successfully removed',async t=>{
  const f=await fixture(t,{HKLM:0});f.readDenied();
  await assert.rejects(f.runtime.status(),/Access denied/);
  await assert.rejects(f.runtime.remove(),/Access denied/);
  assert.deepEqual(f.registry,{HKLM:0});
});
test('the temporary HKLM user-cache registration is migrated into protected storage',async t=>{
  const f=await fixture(t,{legacyHKLM:0,HKCU:0});
  assert.equal((await f.runtime.status()).registrationRepairRequired,true);
  await f.runtime.install(f.doc);assert.deepEqual(f.registry,{HKLM:0});
});
test('a changed or newly prepared QVFR provider requires a machine runtime update',async t=>{
  const f=await fixture(t,{HKLM:0});
  const dir=path.join(f.runtimeDir,'providers','qvfr');await fs.mkdir(dir,{recursive:true});
  await fs.writeFile(path.join(dir,'OpenGridXR-QVFR.dll'),'new provider');
  assert.equal((await f.runtime.status()).updateRequired,true);
});
