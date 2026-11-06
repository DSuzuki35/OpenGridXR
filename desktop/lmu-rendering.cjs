'use strict';
const fs = require('node:fs/promises');
const SOURCE_VERSION=require('../package.json').version;
const {createReadStream} = require('node:fs');
const path = require('node:path');
const {createHash,randomUUID} = require('node:crypto');
const {spawn,execFile} = require('node:child_process');
const {promisify} = require('node:util');
const {pathToFileURL} = require('node:url');
const {atomicWrite} = require('./store.cjs');
const {protectedTarget,verifySignatures:checkSignatures} = require('./lmu-steam.cjs');
const run=promisify(execFile);
const NAMES={qvfr:'XR_APILAYER_MBUCCHIA_quad_views_foveated',vectorxr:'XR_APILAYER_DIENERTECH_VECTORXR'};
const DISABLES=['DISABLE_OPENGRIDXR','DISABLE_XR_APILAYER_MBUCCHIA_quad_views_foveated','DISABLE_QUAD_VIEWS_FOVEATED','DISABLE_XR_APILAYER_DIENERTECH_VECTORXR','DISABLE_XR_APILAYER_MBUCCHIA_toolkit'];
async function hashFile(file){const hash=createHash('sha256'); for await(const chunk of createReadStream(file))hash.update(chunk); return hash.digest('hex').toUpperCase();}
async function readJson(file){if((await fs.stat(file)).size>1024*1024)throw new Error('Configuration exceeds 1 MiB.');return JSON.parse((await fs.readFile(file,'utf8')).replace(/^\uFEFF/,''));}
function safePath(file){if(typeof file!=='string'||!path.isAbsolute(file)||/[;\u0000-\u001f]/.test(file))throw new Error('Select an absolute path without control characters or semicolons.');return path.resolve(file);}
async function validateExecutable(file,{stat=fs.stat,hash=hashFile}={}){
  file=safePath(file);
  if(path.basename(file).toLowerCase()!=='le mans ultimate.exe')throw new Error('Hooked rendering requires Le Mans Ultimate.exe.');
  const info=await stat(file).catch(()=>null); if(!info?.isFile())throw new Error('LMU executable not found.');
  const sha256=await hash(file);
  return {supported:true,sha256,executable:file};
}
function validateLayerMetadata(document){
  // Khronos API-layer manifests use format 1.0.0, major.minor API strings,
  // and decimal-string implementation versions (XrApiLayerProperties uint32).
  // https://registry.khronos.org/OpenXR/specs/1.1/loader.html#api-layer-manifest-file-format
  if(document?.file_format_version!=='1.0.0')throw new Error('Invalid OpenXR manifest file_format_version; expected "1.0.0".');
  const layer=document.api_layer;
  if(!layer||typeof layer!=='object'||Array.isArray(layer))throw new Error('Invalid OpenXR manifest api_layer.');
  if(typeof layer.api_version!=='string'||!/^1\.\d+$/.test(layer.api_version)||Number(layer.api_version.split('.')[1])>65535)
    throw new Error('Invalid OpenXR manifest api_version; expected a supported major.minor string.');
  if(typeof layer.implementation_version!=='string'||!/^\d+$/.test(layer.implementation_version)||Number(layer.implementation_version)>0xffffffff)
    throw new Error('Invalid OpenXR manifest implementation_version; expected a uint32 decimal string.');
}
async function readProviderManifest(file,provider='qvfr'){
  file=safePath(file); const document=await readJson(file),layer=document?.api_layer;
  validateLayerMetadata(document);
  if(!NAMES[provider]||layer?.name!==NAMES[provider])throw new Error(`Invalid ${provider} provider identity.`);
  const disable=layer.disable_environment;
  if(typeof disable!=='string'||!/^[A-Za-z_][A-Za-z0-9_]*$/.test(disable)||!/disable/i.test(disable)||!(provider==='qvfr'?/quad/i:/vectorxr/i).test(disable))throw new Error('Provider has no recognizable disable switch; cannot isolate its installed copy.');
  if(typeof layer.library_path!=='string'||!layer.library_path.trim())throw new Error('Provider library path is missing.');
  const library=safePath(path.resolve(path.dirname(file),layer.library_path));
  if(!(await fs.stat(library).catch(()=>null))?.isFile())throw new Error('Provider library not found. Repair its installation.');
  return {file,library,disable,name:layer.name,document};
}
async function registryValues(key){
  if(process.platform!=='win32')return '';
  try{return (await run('reg.exe',['query',key,'/reg:64'],{windowsHide:true,timeout:5000})).stdout;}catch{return '';}
}
async function registeredLayers(){
  const text=(await Promise.all(['HKCU','HKLM'].map(h=>registryValues(`${h}\\SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit`)))).join('\n');
  return [...text.matchAll(/^\s*(.+?\.json)\s+REG_DWORD\s+0x0\s*$/gmi)].map(m=>m[1].trim());
}
async function jsonFiles(dir,depth=1){
  const entries=await fs.readdir(dir,{withFileTypes:true}).catch(()=>[]),result=[];
  for(const entry of entries.slice(0,100)){const file=path.join(dir,entry.name);if(entry.isFile()&&/\.json$/i.test(entry.name))result.push(file);else if(entry.isDirectory()&&depth>0)result.push(...await jsonFiles(file,depth-1));}return result;
}
async function discoverInstallations(){
  const registered=await registeredLayers(),local=process.env.LOCALAPPDATA||'',program=process.env.ProgramFiles||'C:\\Program Files';
  const manifests=[...registered];
  for(const directory of [path.join(program,'OpenXR-Quad-Views-Foveated'),path.join(program,'Quad-Views-Foveated'),path.join(program,'VectorXR'),...(local?[path.join(local,'Programs','VectorXR'),path.join(local,'VectorXR')]:[])])manifests.push(...await jsonFiles(directory,2));
  const providers=[];
  for(const file of new Set(manifests)){try{const name=(await readJson(file))?.api_layer?.name;const provider=Object.keys(NAMES).find(k=>NAMES[k]===name);if(provider)providers.push({provider,file});}catch{}}
  const steamText=await registryValues('HKCU\\Software\\Valve\\Steam');
  const steam=steamText.match(/^\s*SteamPath\s+REG_SZ\s+(.+)$/mi)?.[1]?.trim();
  const libraries=new Set([...(steam?[steam]:[]),path.join(process.env['ProgramFiles(x86)']||'C:\\Program Files (x86)','Steam')]);
  for(const root of [...libraries]){try{const vdf=await fs.readFile(path.join(root,'steamapps','libraryfolders.vdf'),'utf8');for(const match of vdf.matchAll(/"path"\s+"([^"\r\n]+)"/g))libraries.add(match[1].replace(/\\\\/g,'\\'));}catch{}}
  const executables=[];
  for(const root of libraries){const file=path.join(root,'steamapps','common','Le Mans Ultimate','Le Mans Ultimate.exe');if((await fs.stat(file).catch(()=>null))?.isFile())executables.push(file);}
  return {executables,providers};
}
async function isolationSwitches(manifests){
  const switches=new Set(DISABLES);
  for(const file of manifests){let layer;try{layer=(await readJson(file))?.api_layer;}catch{continue;}
    if(![...Object.values(NAMES),'XR_APILAYER_MBUCCHIA_toolkit','XR_APILAYER_LMU_quadview','XR_APILAYER_OPENGRIDXR'].includes(layer?.name))continue;
    if(typeof layer.disable_environment!=='string'||!/^[A-Za-z_][A-Za-z0-9_]*$/.test(layer.disable_environment)||!/disable/i.test(layer.disable_environment))throw new Error(`Cannot isolate installed ${layer.name}: missing disable switch.`);
    switches.add(layer.disable_environment);
  }
  return [...switches];
}
function buildEnvironment(settings,{parent=process.env,runtimeDir,manifestDirectory,provider,disables=DISABLES,log='',capture,preferences={}}={}){
  const env={...parent};
  const disabledNames=new Set([...DISABLES,...disables,...(provider?[provider.disable]:[])].map(key=>key.toUpperCase()));
  for(const key of Object.keys(env))if(/^(LMU_QV_|OPENGRIDXR_LMU_|OPENGRIDXR_STATUS_DIR$|OPENGRIDXR_CONFIG$|OPENGRIDXR_ENHANCEMENTS_CONFIG$|XR_API_LAYER_PATH$|XR_ENABLE_API_LAYERS$)/i.test(key)||disabledNames.has(key.toUpperCase()))delete env[key];
  env.XR_API_LAYER_PATH=safePath(manifestDirectory);env.XR_ENABLE_API_LAYERS='XR_APILAYER_OPENGRIDXR'+(settings.mode==='quad'&&settings.provider==='qvfr'?`;${NAMES.qvfr}`:'');
  for(const key of [...DISABLES,...disables,...(provider?[provider.disable]:[])])env[key]='1';
  const vrs=settings.mode==='stereoVrs';
  Object.assign(env,{OPENGRIDXR_LMU_INTEGRATED:'1',OPENGRIDXR_LMU_PROVIDER:settings.provider,OPENGRIDXR_STATUS_DIR:safePath(runtimeDir),
    LMU_QV_MODE:vrs?'vrs':'quad',LMU_QV_USE_RUNTIME_SETTINGS:vrs?'0':'1',LMU_QV_FOVEATION_MODE:settings.gaze,
    LMU_QV_DIAGNOSTICS:capture?'1':'0',LMU_QV_LOG:capture?log:'',LMU_QV_BENCHMARK:'0',LMU_QV_DYNAMIC_FOCUS:'0',
    LMU_QV_CPU_AFFINITY:preferences.cpuCacheAffinity===true?'x3d':'off',
    LMU_QV_CPU_UPLOAD_BATCH:!vrs&&settings.cpuUploadBatch?'1':'0',
    LMU_QV_CPU_UPLOAD_REUSE:!vrs&&!settings.cpuUploadBatch&&(settings.cpuUploadReuse||settings.cpuOuterReuse)?'1':'0',
    LMU_QV_CPU_OUTER_REUSE:!vrs&&!settings.cpuUploadBatch&&settings.cpuOuterReuse?'1':'0',
    LMU_QV_OUTER_SSR:!vrs&&settings.nativeOuterResolution&&settings.outerSsr?'1':'0',
    LMU_QV_NATIVE_OUTER:!vrs&&settings.nativeOuterResolution?'1':'0',LMU_QV_VRS_EXCLUSIVE:vrs?'1':'0',
    LMU_QV_VECTORXR_DIRECT_INPUTS:!vrs&&settings.provider==='vectorxr'?'1':'0',
    LMU_QV_OUTER_SHADING_RATE:String(vrs?settings.peripheralShadingRate:2),LMU_QV_DEBUG_FOCUS:settings.debugFocus?'1':'0',
    LMU_QV_OUTER_BLUR:'0',LMU_QV_CROP_WIDTH_PERCENT:'100',LMU_QV_CROP_HEIGHT_PERCENT:'100',
    LMU_QV_FOCUS_OFFSET_X_PERCENT:String(settings.focusOffsetX),LMU_QV_FOCUS_OFFSET_Y_PERCENT:String(settings.focusOffsetY),
    LMU_QV_FOCUS_WIDTH_PERCENT:String(vrs?settings.focusWidth:100),LMU_QV_FOCUS_HEIGHT_PERCENT:String(vrs?settings.focusHeight:100),
    LMU_QV_OUTER_PIXELS_PERCENT:'100',LMU_QV_FOCUS_PIXELS_PERCENT:'100',LMU_QV_FOCUS_BOX_PERCENT:'100',
    LMU_QV_CAPTURE_DIR:capture?.directory||'',LMU_QV_CAPTURE_TRIGGER:capture?.trigger||'',
    LMU_QV_PIXEL_STATS:capture?'scene':'0',LMU_QV_GPU_TIMING_TRIGGER:'',LMU_QV_BENCH_CONTROL:''});
  for(const key of ['PROFILE','STEREO_AUDIT','STEREO_SHADER_CAPTURE','STEREO_DRAW_PROOF'])env[`LMU_QV_${key}`]='0';
  return env;
}
const BEGIN='# BEGIN OPENGRIDXR LMU PROFILE',END='# END OPENGRIDXR LMU PROFILE';
function qvfrProfileText(existing,settings){
  const count=token=>existing.split(token).length-1;
  if(count(BEGIN)!==count(END)||count(BEGIN)>1)throw new Error('Malformed LMU profile markers; provider settings were not changed.');
  let clean=existing;
  if(count(BEGIN)){
    const pattern=/^# BEGIN OPENGRIDXR LMU PROFILE\r?$[\s\S]*?^# END OPENGRIDXR LMU PROFILE\r?$(?:\n)?/gm,match=pattern.exec(existing);
    if(!match)throw new Error('Malformed LMU profile block.');
    let suffixScope=false;
    for(const line of existing.slice(match.index+match[0].length).split('\n')){const text=line.trim();if(text.startsWith('['))break;if(text&&!/^[#;]/.test(text)){suffixScope=true;break;}}
    clean=existing.replace(pattern,suffixScope?'[exe:Le Mans Ultimate.exe]\n':'');
  }
  const n=value=>String(value/100);
  // The adapter owns offsets. Override inherited provider shifts and ensure quad
  // views remain advertised, scoped only to the final LMU application block.
  return clean.replace(/[\r\n]+$/,'')+'\n\n'+BEGIN+'\n[exe:Le Mans Ultimate.exe]\nprefer_foveated_rendering=0\nforce_no_eye_tracking=0\nvertical_focus_offset=0\nvertical_fixed_offset=0\nunadvertise=0\n'+
    `peripheral_multiplier=${n(settings.peripheralScale)}\nfocus_multiplier=${n(settings.focusScale)}\nhorizontal_focus_section=${n(settings.focusWidth)}\nvertical_focus_section=${n(settings.focusHeight)}\nhorizontal_fixed_section=${n(settings.focusWidth)}\nvertical_fixed_section=${n(settings.focusHeight)}\nturbo_mode=0\n${END}\n`;
}
function createLmuRendering({dataDir,runtimeDir,qvfrSettingsPath=path.join(process.env.LOCALAPPDATA||dataDir,'Quad-Views-Foveated','settings.cfg'),discover=discoverInstallations,registered=registeredLayers,spawnProcess=spawn,validateGame=validateExecutable,verifySignatures=checkSignatures,isAlive=pid=>{try{process.kill(pid,0);return true;}catch{return false;}}}){
  const localFile=path.join(dataDir,'lmu-installation.json'),runs=path.join(dataDir,'lmu-runs');
  let installation,discovery,discoveredAt=0,compatibilityCache,currentRun=null;
  async function paths(){
    if(!installation){try{const doc=await readJson(localFile);installation={executable:typeof doc.executable==='string'?doc.executable:'',qvfrManifest:typeof doc.qvfrManifest==='string'?doc.qvfrManifest:'',cpuCacheAffinity:doc.cpuCacheAffinity===true};}catch(error){if(error.code!=='ENOENT')throw new Error(`Could not read LMU installation paths: ${error.message}`);installation={executable:'',qvfrManifest:'',cpuCacheAffinity:false};}}
    // Rediscover at most once a minute while LMU or a provider is still missing,
    // so an installation made after startup is found without restarting.
    if(!discovery||(((!installation.executable&&!discovery.executables.length)||(!installation.qvfrManifest&&!discovery.providers.some(p=>p.provider==='qvfr')))&&Date.now()-discoveredAt>60000)){discovery=await discover();discoveredAt=Date.now();}
    return {...installation,executable:installation.executable||discovery.executables[0]||'',qvfrManifest:installation.qvfrManifest||discovery.providers.find(p=>p.provider==='qvfr')?.file||''};
  }
  async function preferences(){await paths();return {cpuCacheAffinity:installation.cpuCacheAffinity};}
  async function setPreferences(value){
    if(!value||typeof value!=='object'||Array.isArray(value)||Object.keys(value).length!==1||typeof value.cpuCacheAffinity!=='boolean')throw new Error('Invalid LMU machine preference.');
    await paths();const next={...installation,cpuCacheAffinity:value.cpuCacheAffinity};
    await atomicWrite(localFile,JSON.stringify(next,null,2)+'\n');installation=next;return {...await preferences(),restartRequired:true};
  }
  async function setPath(kind,file){if(!['executable','qvfrManifest'].includes(kind))throw new Error('Unsupported LMU installation path.');await paths();file=safePath(file);if(kind==='qvfrManifest')await readProviderManifest(file);else if(path.basename(file).toLowerCase()!=='le mans ultimate.exe')throw new Error('Select Le Mans Ultimate.exe.');const next={...installation,[kind]:file};await atomicWrite(localFile,JSON.stringify(next,null,2)+'\n');installation=next;compatibilityCache=null;return status();}
  async function compatibility(file){
    if(!file)return {supported:false,message:'Choose an installed LMU executable.'};
    const stat=await fs.stat(file).catch(()=>null),key=`${file}|${stat?.size}|${stat?.mtimeMs}`;
    if(compatibilityCache?.key===key)return compatibilityCache.value;
    let value;try{value={...await validateGame(file),message:'LMU executable found. Native code anchors and resource guards determine hook compatibility during startup.'};}catch(error){value={supported:false,message:error.message};}
    compatibilityCache={key,value};return value;
  }
  async function captureStatus(){if(!currentRun?.capture)return {state:'inactive'};const files=['outer0.bmp','outer1.bmp','app0.bmp','app1.bmp'];const captured=await Promise.all(files.map(file=>fs.access(path.join(currentRun.capture.directory,file)).then(()=>true,()=>false)));return {runId:currentRun.id,state:captured.every(Boolean)?'complete':!isAlive(currentRun.pid)?'stopped':currentRun.triggered?'requested':'armed',directory:currentRun.capture.directory,files:files.filter((_,i)=>captured[i]),message:'Four scene views are captured once after the trigger, with matching FOV and pose metadata in the run log.'};}
  async function status(){const local=await paths();let qvfr={available:false,message:'Choose a QVFR manifest or install QVFR.'};if(local.qvfrManifest)try{const p=await readProviderManifest(local.qvfrManifest);qvfr={available:true,manifest:p.file,message:'QVFR manifest and library found.'};}catch(error){qvfr.message=error.message;}return {paths:local,compatibility:await compatibility(local.executable),preferences:await preferences(),sourceVersion:SOURCE_VERSION,providers:{qvfr,vectorxr:{available:!!(await fs.stat(path.join(runtimeDir,'OpenGridXR-Enhancements.dll')).catch(()=>null))?.isFile(),embedded:true}},capture:await captureStatus(),validation:'Headset/provider validation is still required for the integrated path.'};}
  async function prepare(profile){
    const {validateLmuRendering}=await import(pathToFileURL(path.join(__dirname,'../src/lmu-rendering.mjs')).href);validateLmuRendering(profile.lmuRendering,profile.game);
    if(profile.game!=='lmu'||!profile.lmuRendering||profile.lmuRendering.mode==='off')throw new Error('Choose quad views or stereo VRS in the saved LMU profile.');
    const local=await paths(),game=await validateGame(local.executable),settings=profile.lmuRendering;
    const ownPath=path.join(runtimeDir,'OpenGridXR.json'),document=await readJson(ownPath),layer=document?.api_layer;
    validateLayerMetadata(document);
    if(layer?.name!=='XR_APILAYER_OPENGRIDXR'||!['OpenGridXR.dll','./OpenGridXR.dll','.\\OpenGridXR.dll'].includes(layer.library_path))throw new Error('Installed OpenGridXR manifest is invalid.');
    const library=path.join(runtimeDir,'OpenGridXR.dll');if(!(await fs.stat(library).catch(()=>null))?.isFile())throw new Error('Install the OpenGridXR runtime first.');
    if(settings.mode==='quad'&&settings.provider==='vectorxr'&&!(await fs.stat(path.join(runtimeDir,'OpenGridXR-Enhancements.dll')).catch(()=>null))?.isFile())throw new Error('Embedded VectorXR engine is missing; reinstall the runtime.');
    const provider=settings.mode==='quad'&&settings.provider==='qvfr'?await readProviderManifest(local.qvfrManifest):null;
    const disables=await isolationSwitches(await registered());
    return {local,game,settings,document,library,provider,disables};
  }
    async function prepareAutomatic(profile){
      if(profile?.game!=='lmu'||profile.lmuRendering?.mode!=='quad'||profile.lmuRendering.provider!=='qvfr')return;
      const {validateLmuRendering}=await import(pathToFileURL(path.join(__dirname,'../src/lmu-rendering.mjs')).href);
      validateLmuRendering(profile.lmuRendering,profile.game);
      const local=await paths(),provider=await readProviderManifest(local.qvfrManifest);
      const directory=path.join(runtimeDir,'providers','qvfr'),destination=path.join(directory,'OpenGridXR-QVFR.dll');
      // A separate file gives QVFR its own module state if its registered copy
      // has already initialized above us. Keep its Authenticode bytes intact.
      await fs.mkdir(directory,{recursive:true});
      if(await hashFile(destination).catch(()=>null)!==await hashFile(provider.library)){
        const temp=destination+'.new';await fs.copyFile(provider.library,temp);await fs.rename(temp,destination);
      }
      // QVFR reads its installation defaults before per-user settings.
      const defaults=path.join(path.dirname(provider.library),'settings.cfg');
      try {await atomicWrite(path.join(directory,'settings.cfg'),await fs.readFile(defaults));}
      catch(error){if(error.code!=='ENOENT')throw error;await fs.rm(path.join(directory,'settings.cfg'),{force:true});}
      let old=null;try{old=await fs.readFile(qvfrSettingsPath,'utf8');}catch(error){if(error.code!=='ENOENT')throw error;}
      const updated=qvfrProfileText(old||'',profile.lmuRendering);
      if(updated!==old){
        await fs.mkdir(path.dirname(qvfrSettingsPath),{recursive:true});
        if(old!==null)await fs.copyFile(qvfrSettingsPath,`${qvfrSettingsPath}.opengrid-backup-${randomUUID()}`,require('node:fs').constants.COPYFILE_EXCL);
        await atomicWrite(qvfrSettingsPath,updated);
      }
    }
    async function preview(profile){const plan=await prepare(profile);return {executable:plan.game.executable,sha256:plan.game.sha256,mode:plan.settings.mode,provider:plan.settings.mode==='stereoVrs'?'Runtime stereo':plan.settings.provider,providerManifest:plan.provider?.file||null,layers:['XR_APILAYER_OPENGRIDXR',...(plan.provider?[plan.provider.name]:[])],message:'Launch guards passed. Native hook anchors and headset rendering are checked during the session.'};}
  async function launch(profile,{capture=false,ensureStopped=async()=>{},protectedArgs=null}={}){
    if(typeof capture!=='boolean')throw new Error('Invalid capture option.');
    await ensureStopped();const plan=await prepare(profile);if(capture&&plan.settings.mode!=='quad')throw new Error('Four-view capture requires quad mode.');
    const target=protectedArgs===null?{executable:plan.game.executable,args:['+XR']}:await protectedTarget(plan.game.executable,protectedArgs);
    if(protectedArgs!==null){
      if(capture)throw new Error('Use the offline launcher for four-view capture.');
      await verifySignatures([target.executable,plan.library,path.join(runtimeDir,'OpenGridXR-Enhancements.dll')]);
    }
    const id=randomUUID(),directory=path.join(runs,id),manifestDirectory=path.join(directory,'layers'),log=path.join(directory,'render.log');
    await fs.mkdir(manifestDirectory,{recursive:true});
    const own=structuredClone(plan.document);own.api_layer.library_path=plan.library;delete own.api_layer.disable_environment;delete own.api_layer.enable_environment;
    await atomicWrite(path.join(manifestDirectory,'OpenGridXR.json'),JSON.stringify(own,null,2));
    if(plan.provider){const doc=structuredClone(plan.provider.document);doc.api_layer.library_path=plan.provider.library;delete doc.api_layer.disable_environment;delete doc.api_layer.enable_environment;await atomicWrite(path.join(manifestDirectory,'QVFR.json'),JSON.stringify(doc,null,2));}
    const capturePlan=capture?{directory:path.join(directory,'views'),trigger:path.join(directory,'capture.request')}:null;
    if(capturePlan)await fs.mkdir(capturePlan.directory);
    const env=buildEnvironment(plan.settings,{runtimeDir,manifestDirectory,provider:plan.provider,disables:plan.disables,log,capture:capturePlan,preferences:await preferences()});
    // Freeze the saved launch settings while runtime assets remain beside the DLL.
    for(const [file,key] of [['OpenGridXR.ini','OPENGRIDXR_CONFIG'],['OpenGridXR-enhancements-lmu.json','OPENGRIDXR_ENHANCEMENTS_CONFIG']]){
      const destination=path.join(directory,file);await fs.copyFile(path.join(runtimeDir,file),destination);env[key]=destination;
    }
    const record={id,status:'prepared',launchMode:protectedArgs===null?'offline':'steam-eac',launcher:target.executable,executable:plan.game.executable,exeSHA256:plan.game.sha256,layerSHA256:await hashFile(plan.library),providerSHA256:plan.provider?await hashFile(plan.provider.library):null,settings:plan.settings,crop:profile.view.crop,capture:capturePlan,preparedUTC:new Date().toISOString()};
    const metadata=path.join(directory,'run.json');await atomicWrite(metadata,JSON.stringify(record,null,2));
    let rollback=null;
    try{
      await ensureStopped();await validateGame(plan.game.executable);
      if(plan.provider){const file=qvfrSettingsPath;let old=null;try{old=await fs.readFile(file,'utf8');}catch(error){if(error.code!=='ENOENT')throw error;}const updated=qvfrProfileText(old||'',plan.settings);if(updated!==old){await fs.mkdir(path.dirname(file),{recursive:true});if(old!==null)await fs.copyFile(file,`${file}.opengrid-backup-${id}`,require('node:fs').constants.COPYFILE_EXCL);await atomicWrite(file,updated);rollback={file,old,updated};}}
      const pid=await new Promise((resolve,reject)=>{const child=spawnProcess(target.executable,target.args,{cwd:path.dirname(plan.game.executable),env,shell:false,detached:true,windowsHide:false,stdio:'ignore'});child.once('error',reject);child.once('spawn',()=>{child.unref();resolve(child.pid);});});
      currentRun={id,pid,capture:capturePlan,triggered:false};record.status='started';record.pid=pid;
      await atomicWrite(metadata,JSON.stringify(record,null,2)).catch(()=>{});
      return {pid,runId:id,message:protectedArgs===null?`LMU started offline with ${plan.settings.mode==='quad'?'quad views':'stereo VRS'}. Native hook and headset validation remain session-dependent.`:'LMU EAC launcher started with the saved rendering settings. EAC acceptance and in-headset rendering still require validation.',capture:await captureStatus()};
    }catch(error){if(rollback){try{if(await fs.readFile(rollback.file,'utf8')===rollback.updated){if(rollback.old===null)await fs.unlink(rollback.file);else await atomicWrite(rollback.file,rollback.old);}}catch{}}
      record.status='failed';record.error=error.message;await atomicWrite(metadata,JSON.stringify(record,null,2)).catch(()=>{});throw error;
    }
  }
  async function triggerCapture(runId){if(!currentRun?.capture||currentRun.id!==runId||!isAlive(currentRun.pid))throw new Error('No active owned capture run.');if(currentRun.triggered)throw new Error('This capture was already requested.');await fs.writeFile(currentRun.capture.trigger,'',{flag:'wx'});currentRun.triggered=true;return captureStatus();}
    return {status,setPath,preferences,setPreferences,preview,launch,captureStatus,triggerCapture,prepareAutomatic};
}
module.exports={buildEnvironment,readProviderManifest,qvfrProfileText,validateExecutable,createLmuRendering,discoverInstallations,isolationSwitches};
