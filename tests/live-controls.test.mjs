import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { createRequire } from 'node:module';
const require=createRequire(import.meta.url);
const nativeFs=require('node:fs');
const {createLiveControls}=require('../desktop/live-controls.cjs');
const session='123-1000-test';
async function fixture(t, overrides={}) {
  const root=await fs.mkdtemp(path.join(os.tmpdir(),'opengrid-live-'));
  t.after(()=>fs.rm(root,{recursive:true,force:true}));
  await fs.mkdir(path.join(root,'status'));
  const state={protocolVersion:1,sessionId:session,processId:123,application:'iRacingSim64DX11.exe',updatedAtUnixMilliseconds:1000,acknowledgedRevision:0,capabilities:{drivingControls:true,osd:true,framegenToggle:true},...overrides};
  const file=path.join(root,'status',`${session}.json`);
  await fs.writeFile(file,JSON.stringify(state));
  return {root,state,file,profile:{id:'profile-a',game:'iracing',framegen:{enabled:true},overlay:{enabled:true}}};
}
test('live actions target the matching fresh session, acknowledge, and leave startup preferences untouched',async t=>{
  const f=await fixture(t), before=structuredClone(f.profile);let clock=1000;
  const controls=createLiveControls({root:f.root,now:()=>clock,isAlive:()=>true,delay:async()=>{
    const command=JSON.parse(await fs.readFile(path.join(f.root,'control',`${session}.json`)));
    assert.equal(command.drivingAction,'framegen.toggle'); assert.equal(command.targetSessionId,session);
    assert.ok(command.expiresAtUnixMilliseconds>clock);
    clock+=25;f.state.acknowledgedRevision=command.revision;
    await fs.writeFile(f.file,JSON.stringify(f.state));
  }});
  await controls.execute(f.profile,'framegen.enabled');assert.deepEqual(f.profile,before);
});
test('live actions reject stale, dead, mismatched and unsupported engine sessions',async t=>{
  for(const overrides of [{updatedAtUnixMilliseconds:-5000},{application:'Le Mans Ultimate.exe'},{capabilities:{osd:true}},{sessionId:'../escape'}]) {
    const f=await fixture(t,overrides);
    await assert.rejects(createLiveControls({root:f.root,now:()=>1000,isAlive:()=>true}).execute(f.profile,'overlay.enabled'),/running|supported|session/i);
    await assert.rejects(fs.access(path.join(f.root,'control')));
  }
  const f=await fixture(t);
  await assert.rejects(createLiveControls({root:f.root,now:()=>1000,isAlive:()=>false}).execute(f.profile,'overlay.layout'),/session/i);
});
test('live commands require configured features and an acknowledgement, never silently change the profile',async t=>{
  const f=await fixture(t);let clock=1000;
  const controls=createLiveControls({root:f.root,now:()=>clock,isAlive:()=>true,delay:async()=>{clock+=250;},timeoutMs:500});
  await assert.rejects(controls.execute({...f.profile,framegen:{enabled:false}},'framegen.enabled'),/before starting/i);
  await assert.rejects(controls.execute(f.profile,'view.crop'),/unsupported/i);
  await assert.rejects(controls.execute(f.profile,'overlay.layout'),/acknowledge/i);
  await assert.rejects(fs.access(path.join(f.root,'control',`${session}.json`)));
});
test('two matching sessions are ambiguous and no command is written',async t=>{
  const f=await fixture(t);const other='124-1000-test';
  await fs.writeFile(path.join(f.root,'status',`${other}.json`),JSON.stringify({...f.state,sessionId:other,processId:124}));
  await assert.rejects(createLiveControls({root:f.root,now:()=>1000,isAlive:()=>true}).execute(f.profile,'overlay.enabled'),/one.*session|multiple/i);
});
test('an editor change detected before dispatch cancels the pending command',async t=>{
  const f=await fixture(t);
  await assert.rejects(createLiveControls({root:f.root,now:()=>1000,isAlive:()=>true,canApply:()=>false}).execute(f.profile,'overlay.enabled'),/discard edits/);
  assert.deepEqual(await fs.readdir(path.join(f.root,'control')),[]);
});

test('an edit during temporary command writing prevents publication',async t=>{
  const f=await fixture(t);let dirty=false;
  const originalOpen=fs.open;
  t.after(()=>{fs.open=originalOpen;});
  fs.open=async function(file,...args){
    const handle=await originalOpen.call(this,file,...args);
    if(String(file).startsWith(path.join(f.root,'control')))dirty=true;
    return handle;
  };
  const controls=createLiveControls({root:f.root,now:()=>1000,isAlive:()=>true,canApply:()=>!dirty,
    delay:async()=>assert.fail('A cancelled command must never wait for acknowledgement')});
  await assert.rejects(controls.execute(f.profile,'overlay.enabled'),/discard edits/);
  assert.equal(dirty,true);
  assert.deepEqual(await fs.readdir(path.join(f.root,'control')),[]);
});

test('a clean editor with a changed profile or generation cannot publish a pending action',async t=>{
  for(const change of ['profile','generation']) {
    const f=await fixture(t);let generation=7,profileId=f.profile.id;
    const originalOpen=fs.open;
    fs.open=async function(file,...args){
      const handle=await originalOpen.call(this,file,...args);
      if(String(file).startsWith(path.join(f.root,'control'))) {
        if(change==='profile')profileId='profile-b';else generation++;
      }
      return handle;
    };
    try {
      const controls=createLiveControls({root:f.root,now:()=>1000,isAlive:()=>true,getEditorGeneration:()=>generation,
        canApply:(id,captured)=>{assert.equal(id,f.profile.id);assert.equal(captured,7);return id===profileId&&captured===generation;},
        delay:async()=>assert.fail('A cancelled command must never wait for acknowledgement')});
      await assert.rejects(controls.execute(f.profile,'overlay.layout'),/discard edits/);
      assert.ok(profileId!==f.profile.id||generation!==7,'The editor must change during command writing');
      assert.deepEqual(await fs.readdir(path.join(f.root,'control')),[]);
    } finally {fs.open=originalOpen;}
  }
});

test('a generation captured before profile loading is respected',async t=>{
  const f=await fixture(t);
  const controls=createLiveControls({root:f.root,now:()=>1000,isAlive:()=>true,getEditorGeneration:()=>8,
    canApply:(id,captured)=>{assert.equal(id,f.profile.id);assert.equal(captured,7);return captured===8;},
    delay:async()=>assert.fail('A cancelled command must never wait for acknowledgement')});
  await assert.rejects(controls.execute(f.profile,'overlay.enabled',7),/discard edits/);
  assert.deepEqual(await fs.readdir(path.join(f.root,'control')),[]);
});

test('Windows replacement retries recheck the editor before publishing', {skip:process.platform!=='win32'},async t=>{
  const f=await fixture(t);let dirty=false,attempts=0;
  const originalRename=nativeFs.renameSync;
  t.after(()=>{nativeFs.renameSync=originalRename;});
  nativeFs.renameSync=function(source,target){
    if(String(target)===path.join(f.root,'control',`${session}.json`)) {
      attempts++;dirty=true;
      throw Object.assign(new Error('Reader temporarily holds the command file'),{code:'EPERM'});
    }
    return originalRename.call(this,source,target);
  };
  const controls=createLiveControls({root:f.root,now:()=>1000,isAlive:()=>true,canApply:()=>!dirty,
    delay:async()=>assert.fail('A cancelled command must never wait for acknowledgement')});
  await assert.rejects(controls.execute(f.profile,'overlay.enabled'),/discard edits/);
  assert.equal(attempts,1);
  assert.deepEqual(await fs.readdir(path.join(f.root,'control')),[]);
});
