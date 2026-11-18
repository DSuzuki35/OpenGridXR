import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {createRequire} from 'node:module';
import {createDocument,createProfile,parseProfile,validateProfile} from '../src/model.mjs';
import {moduleDefaults,validateModuleSettings,MODULES,moduleForAction} from '../src/modules.mjs';
const {createStore}=createRequire(import.meta.url)('../desktop/store.cjs');

test('retired iRacing settings are absent from new profiles and ignored when importing legacy profiles',()=>{
 const profile=createProfile('iracing');assert.equal(Object.hasOwn(profile,'nativeFoveation'),false);
 profile.nativeFoveation={obsolete:'unused data'};
 assert.doesNotThrow(()=>validateProfile(profile));
 const imported=parseProfile(JSON.stringify(profile));assert.equal(Object.hasOwn(imported,'nativeFoveation'),false);
 assert.equal(Object.hasOwn(profile,'nativeFoveation'),true,'Import normalization must not mutate its input.');
 assert.equal(imported.lmuRendering.mode,'off');
});
test('legacy module preferences drop only the retired module without failing closed',()=>{
 const enabled={...moduleDefaults(),foveation:true,helmet:false};
 const value=validateModuleSettings({version:1,enabled});assert.equal(Object.hasOwn(value.enabled,'foveation'),false);
 assert.equal(value.enabled.helmet,false);assert.equal(value.enabled.lmuRendering,true);
 assert.equal(MODULES.some(module=>module.id==='foveation'),false);assert.equal(moduleForAction('nativeFoveation.enabled'),undefined);
});
test('legacy disk profiles normalize in memory and remove retired settings on save while backing up original bytes',async t=>{
 const dir=await fs.mkdtemp(path.join(os.tmpdir(),'og-retired-foveation-'));t.after(()=>fs.rm(dir,{recursive:true,force:true}));
 const doc=createDocument();doc.profiles[0].nativeFoveation={outerResolution:35,focusWidth:40,resolutionScale:100};
 const file=path.join(dir,'profiles.json'),original=JSON.stringify(doc);await fs.writeFile(file,original);
 const store=await createStore(dir),state=await store.load();assert.equal(state.recoveryError,undefined);assert.equal(Object.hasOwn(state.document.profiles[0],'nativeFoveation'),false);
 assert.equal(await fs.readFile(file,'utf8'),original,'Read-only loading preserves disk bytes.');
 const saved=await store.save(state.document,state.revision);assert.equal(Object.hasOwn(JSON.parse(await fs.readFile(file,'utf8')).profiles[0],'nativeFoveation'),false);
 assert.equal(await fs.readFile(path.join(dir,'backups',saved.backups[0].id),'utf8'),original);
});
