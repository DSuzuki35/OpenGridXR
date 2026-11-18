import {test} from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {createDocument} from '../src/model.mjs';
import {effectiveDocument,moduleDefaults} from '../src/modules.mjs';
const require=createRequire(import.meta.url);
const {launchSavedSteam}=require('../desktop/lmu-steam-entry.cjs');
const {steamLaunchOption,protectedTarget}=require('../desktop/lmu-steam.cjs');
const {toRuntimeIni,toEnhancementsConfig}=require('../desktop/runtime-config.cjs');
const {createModuleSettings}=require('../desktop/module-settings.cjs');
const {createLmuRendering}=require('../desktop/lmu-rendering.cjs');

test('Steam launch option quotes the app path and forwards the original command',()=>{
 const exe=path.resolve('installed app','OpenGrid XR.exe');
 assert.equal(steamLaunchOption(exe),`"${exe}" --lmu-steam-launch %command%`);
 for(const bad of ['relative.exe',exe+'"',exe+'\n',exe+'%PATH%'])assert.throws(()=>steamLaunchOption(bad));
});
test('saved Steam launch refuses disabled modules, stale runtime config and revisions without writing profiles',async t=>{
 const dir=await fs.mkdtemp(path.join(os.tmpdir(),'og-steam-entry-'));t.after(()=>fs.rm(dir,{recursive:true,force:true}));
 const document=createDocument(),enabled=moduleDefaults();document.profiles[1].lmuRendering.mode='quad';
 const state={document,revision:'saved'},calls=[];
 const fixture={runtimeDir:dir,args:['protected launcher'],effectiveDocument,store:{load:async()=>structuredClone(state)},moduleSettings:{get:()=>({enabled})},
   runtime:{ensureGamesClosed:async()=>{},status:async()=>({registered:true})},rendering:{preferences:async()=>({cpuCacheAffinity:true}),launch:async(profile,options)=>{await options.ensureStopped();calls.push({profile,options});return {pid:9};}}};
 await fs.writeFile(path.join(dir,'OpenGridXR.ini'),'stale');
 await assert.rejects(launchSavedSteam(fixture),/Runtime settings/);assert.equal(calls.length,0);
 await fs.writeFile(path.join(dir,'OpenGridXR.ini'),toRuntimeIni(effectiveDocument(document,enabled),{lmuPreferences:{cpuCacheAffinity:true}}));
 await fs.writeFile(path.join(dir,'OpenGridXR-enhancements-lmu.json'),JSON.stringify(toEnhancementsConfig(effectiveDocument(document,enabled),'lmu')));
 assert.equal((await launchSavedSteam(fixture)).pid,9);assert.equal(calls[0].profile.game,'lmu');assert.deepEqual(calls[0].options.protectedArgs,fixture.args);
 enabled.lmuRendering=false;await assert.rejects(launchSavedSteam(fixture),/disabled/);enabled.lmuRendering=true;
 fixture.rendering.launch=async(_p,options)=>{state.revision='changed';await options.ensureStopped();throw Error('should not reach spawn');};
 await assert.rejects(launchSavedSteam(fixture),/changed during launch/);
 assert.equal(state.document,document);
});

test('saved Steam launch validates runtime INI against the persisted local CPU preference',async t=>{
 const dir=await fs.mkdtemp(path.join(os.tmpdir(),'og-steam-cpu-'));t.after(()=>fs.rm(dir,{recursive:true,force:true}));
 const document=createDocument(),enabled=moduleDefaults();document.profiles[1].lmuRendering.mode='quad';
 const service=createLmuRendering({dataDir:dir,runtimeDir:dir,discover:async()=>({executables:[],providers:[]})});
 await service.setPreferences({cpuCacheAffinity:false});
 const doc=effectiveDocument(document,enabled);
 await fs.writeFile(path.join(dir,'OpenGridXR.ini'),toRuntimeIni(doc,{lmuPreferences:await service.preferences()}));
 await fs.writeFile(path.join(dir,'OpenGridXR-enhancements-lmu.json'),JSON.stringify(toEnhancementsConfig(doc,'lmu')));
 let launched=0;
 const fixture={runtimeDir:dir,args:['protected launcher'],effectiveDocument,store:{load:async()=>({document,revision:'saved'})},moduleSettings:{get:()=>({enabled})},
   runtime:{ensureGamesClosed:async()=>{},status:async()=>({registered:true})},rendering:{preferences:service.preferences,launch:async(_profile,options)=>{await options.ensureStopped();launched++;return {pid:12};}}};
 assert.equal((await launchSavedSteam(fixture)).pid,12);assert.equal(launched,1);
 await service.setPreferences({cpuCacheAffinity:true});
 await fs.writeFile(path.join(dir,'OpenGridXR.ini'),toRuntimeIni(doc,{lmuPreferences:await service.preferences()}));
 assert.equal((await launchSavedSteam(fixture)).pid,12);assert.equal(launched,2);
 await service.setPreferences({cpuCacheAffinity:false});
 await fs.writeFile(path.join(dir,'OpenGridXR.ini'),toRuntimeIni(doc,{lmuPreferences:{cpuCacheAffinity:true}}));
 await assert.rejects(launchSavedSteam(fixture),/Runtime settings do not match/);assert.equal(launched,2);
 await fs.rm(path.join(dir,'lmu-installation.json'));
 const fresh=createLmuRendering({dataDir:dir,runtimeDir:dir,discover:async()=>({executables:[],providers:[]})});
 fixture.rendering.preferences=fresh.preferences;
 await fs.writeFile(path.join(dir,'OpenGridXR.ini'),toRuntimeIni(doc));
 assert.equal((await launchSavedSteam(fixture)).pid,12);assert.equal(launched,3);
 assert.equal(document.profiles[1].lmuRendering.cpuCacheAffinity,undefined);
});
test('protected arguments keep a single existing OpenXR flag and reject a launcher outside the game directory',async t=>{
 const dir=await fs.mkdtemp(path.join(os.tmpdir(),'og-steam-target-'));t.after(()=>fs.rm(dir,{recursive:true,force:true}));
 const game=path.join(dir,'Le Mans Ultimate.exe'),exe=path.join(dir,'start_protected_game.exe');await fs.writeFile(exe,'test');
 assert.deepEqual((await protectedTarget(game,[exe,'+XR','+trace=2'])).args,['+XR','+trace=2']);
 await assert.rejects(protectedTarget(game,[path.join(dir,'..','start_protected_game.exe')]),/Steam must/);
});

test('a different editor process disabling the module during preparation cancels the Steam launch',async t=>{
 const dir=await fs.mkdtemp(path.join(os.tmpdir(),'og-steam-modules-'));t.after(()=>fs.rm(dir,{recursive:true,force:true}));
 const document=createDocument();document.profiles[1].lmuRendering.mode='quad';
 const moduleSettings=await createModuleSettings(dir),editor=await createModuleSettings(dir);
 const doc=effectiveDocument(document,moduleSettings.get().enabled);
 await fs.writeFile(path.join(dir,'OpenGridXR.ini'),toRuntimeIni(doc));
 await fs.writeFile(path.join(dir,'OpenGridXR-enhancements-lmu.json'),JSON.stringify(toEnhancementsConfig(doc,'lmu')));
 await assert.rejects(launchSavedSteam({runtimeDir:dir,args:['fixture'],effectiveDocument,moduleSettings,
   store:{load:async()=>({revision:'same',document})},runtime:{ensureGamesClosed:async()=>{},status:async()=>({registered:true})},
   reloadModules:async()=>(await createModuleSettings(dir)).get(),
   rendering:{preferences:async()=>({cpuCacheAffinity:false}),launch:async(_p,options)=>{const next=editor.get();next.enabled.lmuRendering=false;await editor.save(next);await options.ensureStopped();throw Error('must not launch');}}
 }),/changed during launch/);
});
