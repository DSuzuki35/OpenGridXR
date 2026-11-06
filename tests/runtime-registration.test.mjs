import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import {createRequire} from 'node:module';
import {runInNewContext} from 'node:vm';
import {promisify} from 'node:util';
import {EventEmitter, once} from 'node:events';
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
  const registry={...initial}, calls=[], spawned=[];
  const spawn=(exe,args,options)=>{
    const child=new EventEmitter();child.exitCode=null;
    child.stdin=Object.assign(new EventEmitter(),{writable:true,destroyed:false,
      write:line=>{child.stdin.emit('written',line);return true;}});
    child.kill=()=>{child.exitCode=0;child.emit('exit',0);};
    spawned.push({exe,args,options,child});process.nextTick(()=>child.emit('spawn'));
    return child;
  };
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
  runInNewContext(`(function(require,module,process){${await fs.readFile(file,'utf8')}\n})`)(name=>name==='node:child_process'?{execFile:execute,spawn}:realRequire(name),module,{platform:'win32',env:{},kill:process.kill});
  const runtime=module.exports.createRuntime({sourceDir:source,runtimeDir});
  t.after(()=>runtime.closeOutput());
  return {runtime,runtimeDir,machineDirectory,registry,calls,spawned,doc:createDocument(),cancel:()=>{cancel=true;},noWrite:()=>{noWrite=true;},readDenied:()=>{readDenied=true;}};
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
test('runtime status notices same-size replacements even when modification time is preserved',async t=>{
  const f=await fixture(t,{HKLM:0});
  const file=path.join(f.machineDirectory,'OpenGridXR.dll');
  const old=new Date('2020-01-01T00:00:00Z');await fs.utimes(file,old,old);
  assert.equal((await f.runtime.status()).updateRequired,false);
  await fs.writeFile(file,'changed');await fs.utimes(file,old,old);
  assert.equal((await f.runtime.status()).updateRequired,true);
});

test('fresh status detects changed contents even when Windows metadata is unchanged; save checks reuse hashes',async t=>{
  const f=await fixture(t,{HKLM:0});
  const file=path.join(f.machineDirectory,'OpenGridXR.dll');
  const originalStat=fs.stat, frozen=await originalStat(file,{bigint:true});
  t.mock.method(fs,'stat',async function(target,options){
    if(target===file&&options?.bigint)return frozen;
    return originalStat.call(this,target,options);
  });
  const read=fs.readFile;let reads=0;
  t.mock.method(fs,'readFile',async function(target,...args){
    if(target===file)reads++;
    return read.call(this,target,...args);
  });
  assert.equal((await f.runtime.status()).updateRequired,false);
  const firstReads=reads;
  assert.equal((await f.runtime.status({cached:true})).updateRequired,false);
  assert.equal(reads,firstReads,'ordinary save status reuses unchanged binary hashes');
  await fs.writeFile(file,'changed');
  assert.equal((await f.runtime.status()).updateRequired,true,'explicit refresh and launch checks hash current bytes');
});
test('fresh status bypasses an old digest published by a concurrent cached check',async t=>{
  const f=await fixture(t,{HKLM:0}),file=path.join(f.machineDirectory,'OpenGridXR.dll');
  const stat=fs.stat,read=fs.readFile,access=fs.access,frozen=await stat(file,{bigint:true});
  let captured=false,blockAccess=false,readCaptured,returnOldRead,allowFreshAccess;
  const capturedGate=new Promise(resolve=>{readCaptured=resolve;});
  const readGate=new Promise(resolve=>{returnOldRead=resolve;});
  const accessGate=new Promise(resolve=>{allowFreshAccess=resolve;});
  t.mock.method(fs,'stat',async function(target,options){
    return target===file&&options?.bigint?frozen:stat.call(this,target,options);
  });
  t.mock.method(fs,'readFile',async function(target,...args){
    const bytes=await read.call(this,target,...args);
    if(target===file&&!captured){captured=true;readCaptured();await readGate;}
    return bytes;
  });
  t.mock.method(fs,'access',async function(target,...args){
    if(blockAccess)await accessGate;
    return access.call(this,target,...args);
  });
  const older=f.runtime.status({cached:true});
  let fresh;
  try {
    await capturedGate;
    await fs.writeFile(file,'changed');
    // Pause the fresh check after entry, before it compares any files. The old
    // check then publishes its earlier bytes into the shared digest cache.
    blockAccess=true;fresh=f.runtime.status();
    returnOldRead();await older;
    allowFreshAccess();
    assert.equal((await fresh).updateRequired,true,'fresh checks ignore peer cache publications');
  } finally {
    returnOldRead();allowFreshAccess();await Promise.allSettled([older,fresh]);
  }
});

test('clean output refuses an outdated installed runtime before sending new preview flags',async t=>{
  const f=await fixture(t,{HKLM:0});
  await fs.writeFile(path.join(f.runtimeDir,'OpenGridXR-Output.exe'),'old output binary');
  await assert.rejects(f.runtime.launch(f.doc,f.doc.profiles[0],[]),/Update.*Setup/i);
  assert.equal(f.spawned.length,0);
});
test('runtime launch owns preview stdin and binds unsaved updates to its launched profile',async t=>{
  const f=await fixture(t,{HKLM:0}),p=f.doc.profiles[0];
  await f.runtime.launch(f.doc,p,[{primary:true,bounds:{x:0,y:0,width:1920,height:1080}}]);
  assert.equal(f.spawned.length,1);const launched=f.spawned[0];
  assert.ok(launched.args.includes('--framing-stdin'));
  assert.deepEqual(Array.from(launched.options.stdio),['pipe','ignore','ignore']);
  const ini=await fs.readFile(path.join(f.runtimeDir,'OpenGridXR.ini'),'utf8');
  const delivered=once(launched.child.stdin,'written');
  assert.equal(f.runtime.previewOutputFraming({profileId:p.id,game:p.game,
    reserve:{left:13,right:13,top:13,bottom:13},offsetX:31,offsetY:6},p.id),true);
  assert.equal((await delivered)[0],'F 13 13 13 13 31 6\n');
  assert.equal(await fs.readFile(path.join(f.runtimeDir,'OpenGridXR.ini'),'utf8'),ini);
  const reset=once(launched.child.stdin,'written');f.runtime.resetOutputFraming();
  assert.equal((await reset)[0],'R\n');assert.equal(f.spawned.length,1);
});
