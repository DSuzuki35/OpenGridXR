import {test} from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import {createProfile} from '../src/model.mjs';
import {EventEmitter} from 'node:events';
const require=createRequire(import.meta.url);
const {buildEnvironment,readProviderManifest,qvfrProfileText,validateExecutable,createLmuRendering}=require('../desktop/lmu-rendering.cjs');
const FIXTURE_HASH='66942337813B301B87D12EDFA330F33354A2A4B8CD9B048885CBF77EB6F940CF';
const manifestDocument=layer=>({file_format_version:'1.0.0',api_layer:{api_version:'1.0',implementation_version:'1',description:'OpenXR API layer test fixture',...layer}});
test('integrated launch isolates inherited controls and selects only its requested provider', () => {
  const settings=createProfile('lmu').lmuRendering; settings.mode='quad'; settings.provider='vectorxr';
  const parent={PATH:'keep',LMU_QV_DYNAMIC_FOCUS:'1',lmu_qv_capture_trigger:'hostile',LMU_QV_CPU_UPLOAD_BATCH:'1',XR_ENABLE_API_LAYERS:'other',DISABLE_OPENGRIDXR:'1'};
  const before=structuredClone(parent);
  const env=buildEnvironment(settings,{parent,runtimeDir:path.resolve('runtime'),manifestDirectory:path.resolve('run'),log:path.resolve('run/log.txt')});
  assert.deepEqual(parent,before); assert.equal(env.LMU_QV_DYNAMIC_FOCUS,'0');
  assert.equal(env.LMU_QV_CPU_UPLOAD_BATCH,'0'); assert.equal(env.lmu_qv_capture_trigger,undefined);
  assert.equal(env.DISABLE_OPENGRIDXR,'1'); // implicit original disabled; explicit owned manifest has no switch
  assert.equal(env.XR_ENABLE_API_LAYERS,'XR_APILAYER_OPENGRIDXR');
  assert.equal(env.OPENGRIDXR_LMU_PROVIDER,'vectorxr'); assert.equal(env.PATH,'keep');
  assert.equal(env.DISABLE_XR_APILAYER_DIENERTECH_VECTORXR,'1');
  settings.mode='stereoVrs';
  const vrs=buildEnvironment(settings,{parent,runtimeDir:path.resolve('runtime'),manifestDirectory:path.resolve('run')});
  assert.equal(vrs.LMU_QV_MODE,'vrs'); assert.equal(vrs.LMU_QV_NATIVE_OUTER,'0'); assert.equal(vrs.LMU_QV_VRS_EXCLUSIVE,'1');
});
test('provider validation resolves owned identity and preserves source manifest', async t => {
  const dir=await fs.mkdtemp(path.join(os.tmpdir(),'og-qv-provider-')); t.after(()=>fs.rm(dir,{recursive:true,force:true}));
  const file=path.join(dir,'provider.json'); await fs.writeFile(path.join(dir,'q.dll'),'dll');
  const doc=manifestDocument({name:'XR_APILAYER_MBUCCHIA_quad_views_foveated',library_path:'q.dll',disable_environment:'DISABLE_QUAD_VIEWS_FOVEATED'});
  await fs.writeFile(file,JSON.stringify(doc));
  const provider=await readProviderManifest(file); assert.equal(provider.library,path.join(dir,'q.dll'));
  assert.deepEqual(JSON.parse(await fs.readFile(file,'utf8')),doc);
  await assert.rejects(readProviderManifest(file,'vectorxr'),/identity/i);
  doc.api_layer.disable_environment='PATH'; await fs.writeFile(file,JSON.stringify(doc));
  await assert.rejects(readProviderManifest(file),/disable/i);
});
test('provider manifest metadata rejects missing or malformed loader versions before launch',async t=>{
  const dir=await fs.mkdtemp(path.join(os.tmpdir(),'og-manifest-version-'));t.after(()=>fs.rm(dir,{recursive:true,force:true}));
  const file=path.join(dir,'provider.json');await fs.writeFile(path.join(dir,'provider.dll'),'fixture');
  const valid=manifestDocument({name:'XR_APILAYER_MBUCCHIA_quad_views_foveated',library_path:'./provider.dll',disable_environment:'DISABLE_QUAD_VIEWS_FOVEATED'});
  for(const [key,values] of [['file_format_version',[undefined,'','1.0.1',1]],['api_version',[undefined,'',1,'1.0.0','2.0','1.-1']],['implementation_version',[undefined,'',1,'-1','1.5','4294967296']]]){
    for(const value of values){const bad=structuredClone(valid);if(key==='file_format_version')bad[key]=value;else bad.api_layer[key]=value;await fs.writeFile(file,JSON.stringify(bad));await assert.rejects(readProviderManifest(file),new RegExp(key),`${key}=${String(value)}`);}
  }
  valid.api_layer.api_version='1.1';valid.api_layer.implementation_version='4294967295';await fs.writeFile(file,JSON.stringify(valid));assert.equal((await readProviderManifest(file)).document.api_layer.api_version,'1.1');
});
test('executable hash is reported while runtime anchors decide hook compatibility', async () => {
  const exe=path.resolve('Le Mans Ultimate.exe');
  assert.equal((await validateExecutable(exe,{stat:async()=>({isFile:()=>true}),hash:async()=> 'new-build'})).sha256,'new-build');
  assert.equal((await validateExecutable(exe,{stat:async()=>({isFile:()=>true}),hash:async()=>FIXTURE_HASH})).supported,true);
});

test('0.5.4 environments isolate and clamp optimizations with local CPU preference',()=>{
 const settings={...createProfile('lmu').lmuRendering,mode:'quad',cpuUploadReuse:true,cpuUploadBatch:true,cpuOuterReuse:true,outerSsr:true,outerSmoothing:3};
 const options={parent:{LMU_QV_CPU_AFFINITY:'hostile'},runtimeDir:path.resolve('runtime'),manifestDirectory:path.resolve('run'),preferences:{cpuCacheAffinity:false}};
 const env=buildEnvironment(settings,options);
 assert.equal(buildEnvironment(settings,{...options,preferences:{}}).LMU_QV_CPU_AFFINITY,'off');
 assert.equal(env.LMU_QV_CPU_UPLOAD_BATCH,'1');assert.equal(env.LMU_QV_CPU_UPLOAD_REUSE,'0');assert.equal(env.LMU_QV_CPU_OUTER_REUSE,'0');assert.equal(env.LMU_QV_OUTER_SSR,'1');
 assert.equal(env.LMU_QV_CPU_AFFINITY,'off');assert.equal(env.LMU_QV_OUTER_BLUR,'0');assert.equal(env.LMU_QV_DIAGNOSTICS,'0');
 assert.equal(buildEnvironment({...settings,nativeOuterResolution:false},options).LMU_QV_OUTER_SSR,'0');
 const vrs=buildEnvironment({...settings,mode:'stereoVrs'},{...options,preferences:{cpuCacheAffinity:true}});
 for(const suffix of ['CPU_UPLOAD_BATCH','CPU_UPLOAD_REUSE','CPU_OUTER_REUSE','OUTER_SSR'])assert.equal(vrs[`LMU_QV_${suffix}`],'0');
 assert.equal(vrs.LMU_QV_CPU_AFFINITY,'x3d');
 assert.equal(buildEnvironment(settings,{...options,capture:{directory:'capture',trigger:'request'}}).LMU_QV_DIAGNOSTICS,'1');
});

test('CPU preference persists only in machine-local installation state',async t=>{
 const dir=await fs.mkdtemp(path.join(os.tmpdir(),'og-lmu-cpu-'));t.after(()=>fs.rm(dir,{recursive:true,force:true}));
 const options={dataDir:dir,runtimeDir:path.join(dir,'runtime'),discover:async()=>({executables:[],providers:[]})};
 const service=createLmuRendering(options);
 assert.deepEqual(await service.preferences(),{cpuCacheAffinity:false});
 await assert.rejects(service.setPreferences({cpuCacheAffinity:'false'}),/preference/i);
 await assert.rejects(service.setPreferences({cpuCacheAffinity:false,unexpected:true}),/preference/i);
 await service.setPreferences({cpuCacheAffinity:false});
 assert.deepEqual(await createLmuRendering(options).preferences(),{cpuCacheAffinity:false});
 await service.setPreferences({cpuCacheAffinity:true});
 assert.deepEqual(await createLmuRendering(options).preferences(),{cpuCacheAffinity:true});
 assert.equal((await service.status()).sourceVersion,'0.5.4');
});

test('existing installation without a CPU preference leaves affinity opt-in',async t=>{
 const dir=await fs.mkdtemp(path.join(os.tmpdir(),'og-lmu-cpu-absent-'));t.after(()=>fs.rm(dir,{recursive:true,force:true}));
 await fs.writeFile(path.join(dir,'lmu-installation.json'),JSON.stringify({executable:'',qvfrManifest:''}));
 const service=createLmuRendering({dataDir:dir,runtimeDir:dir,discover:async()=>({executables:[],providers:[]})});
 assert.deepEqual(await service.preferences(),{cpuCacheAffinity:false});
});
test('QVFR block preserves unrelated content and rejects malformed backup scope', () => {
  const settings=createProfile('lmu').lmuRendering;
  const original='[exe:another.exe]\nfocus_multiplier=2\n';
  const next=qvfrProfileText(original,settings);
  assert.ok(next.startsWith(original)); assert.match(next,/\[exe:Le Mans Ultimate.exe\]/);
  assert.equal(qvfrProfileText(next,settings),next);
  assert.throws(()=>qvfrProfileText('# BEGIN OPENGRIDXR LMU PROFILE\n',settings),/Malformed/);
});
test('QVFR LMU overrides provider offsets and advertisement without changing other scopes',()=>{
  const original='vertical_focus_offset=0.15\nvertical_fixed_offset=-0.2\nunadvertise=1\n[exe:another.exe]\nvertical_focus_offset=0.3\nunadvertise=1\n[exe:Le Mans Ultimate.exe]\nvertical_fixed_offset=0.4\n';
  const settings=createProfile('lmu').lmuRendering;settings.focusOffsetY=20;
  const next=qvfrProfileText(original,settings),owned=next.slice(next.indexOf('# BEGIN OPENGRIDXR LMU PROFILE'));
  assert.ok(next.startsWith(original),'Global and existing application settings remain byte-identical.');
  for(const key of ['vertical_focus_offset','vertical_fixed_offset','unadvertise'])assert.match(owned,new RegExp(`^${key}=0$`,'m'));
  assert.equal(qvfrProfileText(next,settings),next);
});
test('machine-local state persists without profiles; capture rejects unowned runs',async t=>{
  const dir=await fs.mkdtemp(path.join(os.tmpdir(),'og-lmu-local-')); t.after(()=>fs.rm(dir,{recursive:true,force:true}));
  const service=createLmuRendering({dataDir:dir,runtimeDir:path.join(dir,'runtime'),discover:async()=>({executables:[],providers:[]})});
  await assert.rejects(service.triggerCapture('../escape'),/capture/i);
  assert.equal((await service.status()).compatibility.supported,false);
});
async function launchFixture(t,provider='native',failure=false,verifySignatures=async()=>{}){
  const dir=await fs.mkdtemp(path.join(os.tmpdir(),'og-lmu-launch-'));t.after(()=>fs.rm(dir,{recursive:true,force:true}));
  const runtime=path.join(dir,'runtime');await fs.mkdir(runtime);
  for(const [file,text] of Object.entries({'OpenGridXR.json':JSON.stringify(manifestDocument({name:'XR_APILAYER_OPENGRIDXR',library_path:'OpenGridXR.dll',disable_environment:'DISABLE_OPENGRIDXR'})),'OpenGridXR.dll':'fixture dll','OpenGridXR.ini':'[lmu]\nEnabled=1','OpenGridXR-enhancements-lmu.json':'{"version":3}'}))await fs.writeFile(path.join(runtime,file),text);
  const exe=path.join(dir,'Le Mans Ultimate.exe');await fs.writeFile(exe,'test executable');
  await fs.writeFile(path.join(dir,'start_protected_game.exe'),'protected fixture');
  await fs.writeFile(path.join(runtime,'OpenGridXR-Enhancements.dll'),'engine fixture');
  const manifest=path.join(dir,'qvfr.json');await fs.writeFile(path.join(dir,'qvfr.dll'),'fixture');await fs.writeFile(manifest,JSON.stringify(manifestDocument({name:'XR_APILAYER_MBUCCHIA_quad_views_foveated',library_path:'qvfr.dll',disable_environment:'DISABLE_CUSTOM_QUAD'})));
  await fs.writeFile(path.join(dir,'lmu-installation.json'),JSON.stringify({executable:exe,qvfrManifest:manifest}));
  const qvfrSettingsPath=path.join(dir,'settings.cfg');await fs.writeFile(qvfrSettingsPath,'[exe:unrelated.exe]\nkeep=yes\n');
  const calls=[];
  const service=createLmuRendering({dataDir:dir,runtimeDir:runtime,qvfrSettingsPath,verifySignatures,discover:async()=>({executables:[],providers:[]}),registered:async()=>[manifest],validateGame:async executable=>({executable,supported:true,sha256:FIXTURE_HASH}),isAlive:()=>true,spawnProcess:(...args)=>{calls.push(args);const child=new EventEmitter();child.pid=1234;child.unref=()=>{};queueMicrotask(()=>failure?child.emit('error',new Error('fixture spawn failed')):child.emit('spawn'));return child;}});
  const profile=createProfile('lmu');profile.lmuRendering.mode='quad';profile.lmuRendering.provider=provider;
  return {dir,runtime,service,profile,calls,qvfrSettingsPath};
}

test('automatic QVFR prepares an isolated byte-identical provider and scoped settings without spawning or registration',async t=>{
 const f=await launchFixture(t,'qvfr');
 const source=await fs.readFile(path.join(f.dir,'qvfr.dll'));
 await f.service.prepareAutomatic(f.profile);
 const target=path.join(f.runtime,'providers','qvfr','OpenGridXR-QVFR.dll');
 assert.deepEqual(await fs.readFile(target),source);
 const before=(await fs.stat(target)).mtimeMs;
 await f.service.prepareAutomatic(f.profile);
 assert.equal((await fs.stat(target)).mtimeMs,before,'unchanged provider is not replaced while possibly loaded');
 assert.match(await fs.readFile(f.qvfrSettingsPath,'utf8'),/^keep=yes$/m);
 assert.equal(f.calls.length,0);
 f.profile.lmuRendering.mode='off';await f.service.prepareAutomatic(f.profile);
 assert.equal(f.calls.length,0);
});

test('Steam launch forwards the real protected bootstrapper with quad setup and preserves arguments',async t=>{
 const checked=[];const f=await launchFixture(t,'qvfr',false,async files=>checked.push(...files));
 const bootstrap=path.join(f.dir,'start_protected_game.exe');
 const result=await f.service.launch(f.profile,{protectedArgs:[bootstrap,'+trace=2','argument with spaces']});
 assert.equal(f.calls[0][0],await fs.realpath(bootstrap));
 assert.deepEqual(f.calls[0][1],['+trace=2','argument with spaces','+XR']);
 assert.equal(f.calls[0][2].shell,false);
 assert.equal(f.calls[0][2].env.OPENGRIDXR_LMU_INTEGRATED,'1');
 assert.equal(f.calls[0][2].env.LMU_QV_DYNAMIC_FOCUS,'0');
 assert.equal(f.calls[0][2].env.LMU_QV_CPU_AFFINITY,'off');
 assert.ok(checked.includes(await fs.realpath(bootstrap)));assert.ok(checked.includes(path.join(f.runtime,'OpenGridXR.dll')));
 assert.match(result.message,/EAC/);assert.doesNotMatch(result.message,/started offline/);
 const record=JSON.parse(await fs.readFile(path.join(f.dir,'lmu-runs',result.runId,'run.json'),'utf8'));
 assert.equal(record.launchMode,'steam-eac');
});

test('Steam launch refuses direct game, alternate bootstrapper, invalid arguments and unsigned DLLs without offline fallback',async t=>{
 const f=await launchFixture(t,'qvfr');const before=await fs.readFile(f.qvfrSettingsPath,'utf8');
 for(const args of [[],[path.join(f.dir,'Le Mans Ultimate.exe')],[path.join(f.dir,'other','start_protected_game.exe')],['start_protected_game.exe'],[path.join(f.dir,'start_protected_game.exe'),'bad\0arg']]) {
   await assert.rejects(f.service.launch(f.profile,{protectedArgs:args}),/protected|Steam|argument/i);
 }
 assert.equal(f.calls.length,0);assert.equal(await fs.readFile(f.qvfrSettingsPath,'utf8'),before);
 const unsigned=await launchFixture(t,'native',false,async()=>{throw Error('Invalid signature');});
 await assert.rejects(unsigned.service.launch(unsigned.profile,{protectedArgs:[path.join(unsigned.dir,'start_protected_game.exe')]}),/signature/);
 assert.equal(unsigned.calls.length,0);
});
test('owned manifest missing or malformed metadata cannot prepare a launch or create run files',async t=>{
  const f=await launchFixture(t),file=path.join(f.runtime,'OpenGridXR.json'),valid=JSON.parse(await fs.readFile(file,'utf8'));
  for(const [key,value] of [['file_format_version',undefined],['api_version',undefined],['implementation_version',undefined],['file_format_version','2.0.0'],['api_version','bad'],['implementation_version','bad']]){
    const bad=structuredClone(valid);if(key==='file_format_version')bad[key]=value;else bad.api_layer[key]=value;await fs.writeFile(file,JSON.stringify(bad));await assert.rejects(f.service.preview(f.profile),new RegExp(key));
  }
  assert.equal(f.calls.length,0);await assert.rejects(fs.stat(path.join(f.dir,'lmu-runs')),e=>e.code==='ENOENT');
});
test('launch snapshots configuration, isolates manifests and arms only owned scene capture',async t=>{
 const f=await launchFixture(t);const result=await f.service.launch(f.profile,{capture:true});
 assert.equal(f.calls.length,1);const [exe,args,options]=f.calls[0];assert.equal(path.basename(exe),'Le Mans Ultimate.exe');assert.deepEqual(args,['+XR']);assert.equal(options.shell,false);
 assert.equal(options.env.DISABLE_CUSTOM_QUAD,'1');assert.equal(options.env.OPENGRIDXR_STATUS_DIR,f.runtime);
 assert.equal(options.env.LMU_QV_CPU_AFFINITY,'off');
 const snapshot=await fs.readFile(options.env.OPENGRIDXR_CONFIG,'utf8');await fs.writeFile(path.join(f.runtime,'OpenGridXR.ini'),'changed');assert.equal(await fs.readFile(options.env.OPENGRIDXR_CONFIG,'utf8'),snapshot);
 const manifest=JSON.parse(await fs.readFile(path.join(options.env.XR_API_LAYER_PATH,'OpenGridXR.json'),'utf8'));assert.equal(manifest.api_layer.disable_environment,undefined);assert.equal(manifest.api_layer.library_path,path.join(f.runtime,'OpenGridXR.dll'));
 assert.equal((await f.service.captureStatus()).state,'armed');await assert.rejects(f.service.triggerCapture('wrong-run'),/capture/i);
 assert.equal((await f.service.triggerCapture(result.runId)).state,'requested');await assert.rejects(f.service.triggerCapture(result.runId),/already/);
 for(const file of ['outer0.bmp','outer1.bmp','app0.bmp','app1.bmp'])await fs.writeFile(path.join(options.env.LMU_QV_CAPTURE_DIR,file),'BMP fixture');
 assert.equal((await f.service.captureStatus()).state,'complete');
});
test('failed spawn rolls back only its QVFR change and preserves a unique backup',async t=>{
 const f=await launchFixture(t,'qvfr',true),before=await fs.readFile(f.qvfrSettingsPath,'utf8');
 await assert.rejects(f.service.launch(f.profile),/fixture spawn failed/);
 assert.equal(await fs.readFile(f.qvfrSettingsPath,'utf8'),before);
 const backups=(await fs.readdir(f.dir)).filter(name=>name.startsWith('settings.cfg.opengrid-backup-'));assert.equal(backups.length,1);assert.equal(await fs.readFile(path.join(f.dir,backups[0]),'utf8'),before);
 const dirs=await fs.readdir(path.join(f.dir,'lmu-runs'));const metadata=JSON.parse(await fs.readFile(path.join(f.dir,'lmu-runs',dirs[0],'run.json'),'utf8'));assert.equal(metadata.status,'failed');
});
