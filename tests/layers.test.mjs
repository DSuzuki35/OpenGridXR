import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { createLayerEditor, createHelperRunner, inspectLocalManifest } = require('../desktop/layers.cjs');

async function fixture(t, overrides = {}) {
  const dataDir = await fs.mkdtemp(path.join(os.tmpdir(), 'opengrid-layers-'));
  t.after(() => fs.rm(dataDir, {recursive:true, force:true}));
  const entry = {scope:'HKCU',view:'64',kind:'implicit',manifest:'C:\\Layers\\Example.json',type:'DWORD',value:0};
  const state = {entries:[entry],errors:[],calls:[],running:false,fail:null,ignoreWrite:false};
  const run = async args => {
    state.calls.push(args);
    if (args[0] === '--inventory') return structuredClone({entries:state.entries,errors:state.errors});
    assert.equal(args.length,7);
    const [,scope,view,kind,manifest,expected,desired] = args;
    const found = state.entries.find(e => e.scope===scope && e.view===view && e.kind===kind && e.manifest===manifest);
    assert.equal(found?.type,'DWORD'); assert.equal(found.value,Number(expected));
    // A durable journal must precede the registry write, including elevation.
    const backups = await fs.readdir(path.join(dataDir,'layer-backups'));
    assert.ok(backups.length);
    const journal = JSON.parse(await fs.readFile(path.join(dataDir,'layer-backups',backups.at(-1)),'utf8'));
    assert.ok(['pending','confirmed'].includes(journal.state));
    if (state.fail) throw state.fail;
    if (!state.ignoreWrite) {
      found.value=Number(desired);
      if(scope==='HKCU')for(const alias of state.entries.filter(e=>e.scope===scope&&e.kind===kind&&e.manifest===manifest))alias.value=Number(desired);
    }
    return {};
  };
  const editor = createLayerEditor({dataDir,run,ensureGamesClosed:async()=>{if(state.running)throw Error('Close sims');},inspectManifest:async()=>({name:'Example layer',library:'C:\\Layers\\example.dll',health:'Manifest and library found'}),...overrides});
  return {editor,state,dataDir,entry};
}

test('inventory is read-only and displays all registry views without declaring layers loaded',async t=>{
  const {editor,state,entry}=await fixture(t);
  state.entries.push({...entry,scope:'HKLM',view:'32',kind:'explicit',value:5});
  const result=await editor.inventory();
  assert.equal(result.entries.length,2); assert.equal(result.entries[1].value,5);
  assert.equal(result.entries[0].name,'Example layer'); assert.equal(result.entries[0].loaded,undefined);
  assert.deepEqual(state.calls,[['--inventory']]);
});
test('HKCU WOW64 aliases appear once as a shared registration while HKLM views remain separate',async t=>{
  const {editor,state,entry}=await fixture(t);
  state.entries.push({...entry,view:'32'},{...entry,scope:'HKLM'},{...entry,scope:'HKLM',view:'32'});
  const {entries}=await editor.inventory();
  assert.equal(entries.length,3);
  const user=entries.find(entry=>entry.scope==='HKCU');
  assert.equal(user.sharedView,true);assert.equal(user.view,'64');
  const changed=await editor.setEnabled({id:user.id,enabled:false,admin:false});
  assert.equal(changed.entries.filter(entry=>entry.scope==='HKCU').length,1);
  assert.equal(changed.entries.find(entry=>entry.scope==='HKCU').value,1);
  state.entries.find(entry=>entry.scope==='HKCU'&&entry.view==='32').value=7;
  await assert.rejects(editor.inventory(),/changed/,'An inconsistent alias snapshot must not hide a concurrent change.');
});
test('legacy HKCU 32-bit backup restores through the same canonical shared key',async t=>{
  const {editor,state,dataDir}=await fixture(t); const {entries}=await editor.inventory();
  const changed=await editor.setEnabled({id:entries[0].id,enabled:false,admin:false});
  const backup=changed.backups[0]; assert.equal(backup.sharedView,true);
  await fs.writeFile(path.join(dataDir,'layer-backups',`${backup.id}.json`),JSON.stringify({...backup,view:'32'}));
  const result=await editor.restore({id:backup.id,admin:false});
  assert.equal(state.entries[0].value,0);assert.equal(result.backups[0].sharedView,true);
});
test('changes only the server-owned entry and backs up its exact DWORD before mutation',async t=>{
  const {editor,state}=await fixture(t);
  state.entries[0].value=17;
  const {entries}=await editor.inventory();
  const result=await editor.setEnabled({id:entries[0].id,enabled:true,admin:false});
  assert.equal(state.entries[0].value,0);
  assert.deepEqual(state.calls.find(a=>a[0]==='--set'),['--set','HKCU','64','implicit','C:\\Layers\\Example.json','17','0']);
  assert.equal(result.backups[0].before,17); assert.equal(result.backups[0].state,'confirmed');
  const restored=await editor.restore({id:result.backups[0].id,admin:false});
  assert.equal(state.entries[0].value,17); assert.equal(restored.backups[0].state,'restored');
});
test('HKLM requires explicit admin opt-in and uses the bounded elevation command',async t=>{
  const {editor,state}=await fixture(t); state.entries[0].scope='HKLM';
  const {entries}=await editor.inventory();
  await assert.rejects(editor.setEnabled({id:entries[0].id,enabled:false,admin:false}),/Admin/);
  assert.equal(state.entries[0].value,0);
  await editor.setEnabled({id:entries[0].id,enabled:false,admin:true});
  assert.equal(state.calls.find(a=>a[0]==='--elevate-set')[1],'HKLM');
});
test('UAC cancellation is reported without changing registration or claiming success',async t=>{
  const {editor,state}=await fixture(t); state.entries[0].scope='HKLM'; state.fail=Object.assign(Error('Administrator prompt cancelled'),{code:1223});
  const {entries}=await editor.inventory();
  await assert.rejects(editor.setEnabled({id:entries[0].id,enabled:false,admin:true}),/cancelled/);
  assert.equal(state.entries[0].value,0);
  const result=await editor.inventory(); assert.equal(result.backups[0].state,'failed'); assert.equal(result.backups[0].canRestore,false);
});
test('rejects forged targets, unsupported types, stale values, and running sims before mutation',async t=>{
  const {editor,state}=await fixture(t); let {entries}=await editor.inventory();
  await assert.rejects(editor.setEnabled({id:'C:\\arbitrary.json',enabled:false,admin:true}),/Refresh/);
  await assert.rejects(editor.setEnabled({id:entries[0].id,enabled:1,admin:false}),/enabled/);
  state.entries[0].value=7;
  await assert.rejects(editor.setEnabled({id:entries[0].id,enabled:false,admin:false}),/changed/);
  state.entries[0].type='REG_SZ'; ({entries}=await editor.inventory());
  assert.equal(entries[0].editable,false);
  await assert.rejects(editor.setEnabled({id:entries[0].id,enabled:false,admin:false}),/DWORD/);
  state.entries[0].type='DWORD'; ({entries}=await editor.inventory()); state.running=true;
  await assert.rejects(editor.setEnabled({id:entries[0].id,enabled:false,admin:false}),/Close sims/);
  assert.ok(state.calls.every(a=>a[0]==='--inventory'));
});
test('restoration refuses an external edit or removed entry and requires HKLM opt-in again',async t=>{
  const {editor,state}=await fixture(t); state.entries[0].scope='HKLM';
  const {entries}=await editor.inventory(); const changed=await editor.setEnabled({id:entries[0].id,enabled:false,admin:true});
  const id=changed.backups[0].id;
  await assert.rejects(editor.restore({id,admin:false}),/Admin/);
  state.entries[0].value=4; await assert.rejects(editor.restore({id,admin:true}),/changed/);
  state.entries=[]; await assert.rejects(editor.restore({id,admin:true}),/changed/);
  assert.equal((await editor.inventory()).backups[0].canRestore,false);
});
test('independent readback detects a helper that reports success without writing',async t=>{
  const {editor,state}=await fixture(t); state.ignoreWrite=true;
  const {entries}=await editor.inventory();
  await assert.rejects(editor.setEnabled({id:entries[0].id,enabled:false,admin:false}),/readback/i);
  const result=await editor.inventory(); assert.equal(result.backups[0].state,'failed'); assert.equal(result.backups[0].canRestore,false);
});
test('backup write failure aborts before invoking the mutation helper',async t=>{
  const {editor,state}=await fixture(t,{writeBackup:async()=>{throw Error('Disk full');}});
  const {entries}=await editor.inventory();
  await assert.rejects(editor.setEnabled({id:entries[0].id,enabled:false,admin:false}),/Disk full/);
  assert.equal(state.entries[0].value,0); assert.ok(state.calls.every(a=>a[0]==='--inventory'));
});
test('serialized simultaneous toggles reject the stale second request',async t=>{
  const {editor,state}=await fixture(t); const {entries}=await editor.inventory();
  const results=await Promise.allSettled([editor.setEnabled({id:entries[0].id,enabled:false,admin:false}),editor.setEnabled({id:entries[0].id,enabled:false,admin:false})]);
  assert.equal(results[0].status,'fulfilled'); assert.equal(results[1].status,'rejected');
  assert.equal(state.calls.filter(a=>a[0]==='--set').length,1);
});
test('backup history survives restart and restoring keeps the original nonzero DWORD',async t=>{
  const {editor,state,dataDir}=await fixture(t); state.entries[0].value=0xffffffff;
  const {entries}=await editor.inventory(); const changed=await editor.setEnabled({id:entries[0].id,enabled:true,admin:false});
  const restarted=createLayerEditor({dataDir,ensureGamesClosed:async()=>{},inspectManifest:async()=>({name:'Example',health:'OK',library:''}),run:async args=>{
    if(args[0]==='--inventory')return structuredClone({entries:state.entries,errors:[]});
    assert.deepEqual(args,['--set','HKCU','64','implicit','C:\\Layers\\Example.json','0','4294967295']);
    state.entries[0].value=4294967295;return {};
  }});
  const restored=await restarted.restore({id:changed.backups[0].id,admin:false});
  assert.equal(restored.entries[0].value,4294967295);assert.equal(restored.backups[0].state,'restored');
});
test('unsupported manifest paths are visible but cannot be used as mutation targets',async t=>{
  const {editor,state}=await fixture(t);
  for(const manifest of ['relative.json','\\\\server\\share\\layer.json','C:\\Layers\\a.json:stream','C:\\Layers\\a.json" --other','C:\\Layers\\layer.dll','C:\\Layers\\..\\layer.json','C:\\Layers \\layer.json','C:\\Layers\\\\layer.json','C:\\Layers\\a\u0085.json',`C:\\${'a'.repeat(8190)}.json`]) {
    state.entries[0].manifest=manifest;
    const {entries}=await editor.inventory();assert.equal(entries[0].editable,false);
    await assert.rejects(editor.setEnabled({id:entries[0].id,enabled:false,admin:false}),/local JSON/);
  }
  assert.ok(state.calls.every(args=>args[0]==='--inventory'));
});
test('helper runner uses direct argument arrays and explains cancelled, stale, and busy results',async()=>{
  const args=['--elevate-set','HKLM','64','implicit','C:\\Layers with spaces\\example.json','0','1'];
  for(const [code,message] of [[1223,/cancelled/],[1306,/changed/],[170,/Close/],[6800,/changed/]]) {
    const runner=createHelperRunner('C:\\OpenGrid XR\\OpenGridXR-Layers.exe',(file,received,options,done)=>{
      assert.equal(file,'C:\\OpenGrid XR\\OpenGridXR-Layers.exe');assert.deepEqual(received,args);
      assert.equal(options.shell,undefined); assert.equal(options.windowsHide,true);
      done(Object.assign(Error('native failed'),{code}),'','Win32 '+code);
    });
    await assert.rejects(runner(args),message);
  }
  const runner=createHelperRunner('helper.exe',(_file,args,_options,done)=>{assert.deepEqual(args,['--inventory']);done(null,'not json','');});
  await assert.rejects(runner(['--inventory']),/invalid inventory/);
});
test('absolute forward-slash manifest registrations remain editable without rewriting their registry name',async t=>{
  const {editor,state}=await fixture(t); state.entries[0].manifest='C:/Layers/Example.json';
  const {entries}=await editor.inventory();assert.equal(entries[0].editable,true);
  await editor.setEnabled({id:entries[0].id,enabled:false,admin:false});
  assert.equal(state.calls.find(args=>args[0]==='--set')[4],'C:/Layers/Example.json');
});
test('corrupt backups are reported and never sent to the helper',async t=>{
  const {editor,state,dataDir}=await fixture(t);
  const {entries}=await editor.inventory();const result=await editor.setEnabled({id:entries[0].id,enabled:false,admin:false});
  const backup=result.backups[0];const file=path.join(dataDir,'layer-backups',`${backup.id}.json`);
  await fs.writeFile(file,JSON.stringify({...backup,scope:'HKEY_USERS'}));
  const inventory=await editor.inventory();assert.equal(inventory.backups.length,0);assert.equal(inventory.errors.length,1);
  const prior=state.calls.filter(args=>args[0]==='--set').length;
  await assert.rejects(editor.restore({id:backup.id,admin:true}),/cannot be restored/);
  assert.equal(state.calls.filter(args=>args[0]==='--set').length,prior);
});
// Manifest inspection accepts only local Windows paths (drive letters, path.win32).
test('manifest health parses local metadata without loading the library and reports missing files',{skip:process.platform!=='win32'},async t=>{
  const dir=await fs.mkdtemp(path.join(os.tmpdir(),'layer-manifest-'));t.after(()=>fs.rm(dir,{recursive:true,force:true}));
  const manifest=path.join(dir,'layer.json'); const library=path.join(dir,'test.dll');
  await fs.writeFile(library,'This is not an executable.');
  await fs.writeFile(manifest,JSON.stringify({api_layer:{name:'XR_APILAYER_example',library_path:'.\\test.dll'}}));
  const result=await inspectLocalManifest(manifest);
  assert.equal(result.name,'XR_APILAYER_example');assert.equal(result.library,library);assert.equal(result.health,'Manifest and library found');
  await fs.unlink(library);assert.equal((await inspectLocalManifest(manifest)).health,'Library file is missing');
  await fs.writeFile(manifest,'not json');assert.match((await inspectLocalManifest(manifest)).health,/parsed/);
  await fs.unlink(manifest);assert.equal((await inspectLocalManifest(manifest)).health,'Manifest file is missing');
});
