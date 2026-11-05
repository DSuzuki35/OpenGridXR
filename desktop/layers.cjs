"use strict";
const fs = require('node:fs/promises');
const path = require('node:path');
const { randomUUID } = require('node:crypto');
const { execFile } = require('node:child_process');
const { atomicWrite } = require('./store.cjs');

const DWORD_MAX = 0xffffffff;
const isDword = value => Number.isInteger(value) && value >= 0 && value <= DWORD_MAX;
const isLocalPath = value => typeof value === 'string' && value.length <= 32760 && /^[A-Za-z]:[\\/]/.test(value) && !/[\x00-\x1f\x7f-\x9f"<>|?*]/.test(value) && !value.slice(2).includes(':');
// Match the helper's bounded argument contract without normalizing the registry
// value name: C:/layer.json and C:\\layer.json can be distinct registrations.
const validManifest = value => isLocalPath(value) && value.length >= 8 && value.length <= 8192 && /\.json$/i.test(value) && value.slice(3).split(/[\\/]/).every(part=>part && !/[ .]$/.test(part));
const validTarget = value => value && ['HKCU','HKLM'].includes(value.scope) && ['64','32'].includes(value.view) && ['implicit','explicit'].includes(value.kind) && validManifest(value.manifest);
const key = entry => JSON.stringify([entry.scope,entry.scope==='HKCU'?'shared':entry.view,entry.kind,entry.manifest.toLowerCase()]);
const canonical = entry => entry.scope==='HKCU'?{...entry,view:'64',sharedView:true}:{...entry,sharedView:false};
const isEditable = entry => !!(validTarget(entry) && entry.type === 'DWORD' && isDword(entry.value));

// No shell, PowerShell, script, or caller-selected executable is used. The native
// helper repeats validation and limits the elevated operation to an existing DWORD.
function createHelperRunner(executable,execute=execFile) {
  return args => new Promise((resolve,reject) => {
    execute(executable,args,{windowsHide:true,maxBuffer:4*1024*1024,encoding:'utf8',...(args[0] === '--inventory' ? {timeout:15000} : {})},(error,stdout,stderr)=>{
      if(error) {
        const explanations={1223:'Administrator prompt cancelled. No change was confirmed.',170:'Close iRacing, Le Mans Ultimate and Clean Output before changing layer registrations.',1306:'This registration changed. Refresh before editing it.',6800:'The registry changed during this operation. Refresh before trying again.',5:'Windows denied access to this registration. Check its permissions and refresh.',2:'This registration no longer exists. Refresh the layer list.',13:'This registration is not a valid DWORD. Refresh the layer list.',1117:'Registry readback did not confirm the requested change. Refresh and review its current value.'};
        const failure=new Error(explanations[error.code] || `Layer operation failed: ${String(stderr || error.message).trim().slice(0,500)}`);
        failure.code=error.code; return reject(failure);
      }
      if(args[0]!=='--inventory')return resolve({});
      try { resolve(JSON.parse(stdout)); } catch { reject(new Error('The layer helper returned an invalid inventory.')); }
    });
  });
}

async function inspectLocalManifest(manifest) {
  if(!isLocalPath(manifest))return {name:'Unknown layer',library:'',health:'Unsupported manifest path'};
  try {
    if((await fs.stat(manifest)).size > 1024*1024)return {name:'Unknown layer',library:'',health:'Manifest exceeds the inspection limit'};
    const data=JSON.parse(await fs.readFile(manifest,'utf8'));
    const layer=data?.api_layer;
    if(!layer || typeof layer.name!=='string' || typeof layer.library_path!=='string')return {name:'Unknown layer',library:'',health:'Manifest is missing layer name or library path'};
    const name=layer.name.slice(0,512);
    const library=path.win32.resolve(path.win32.dirname(manifest),layer.library_path);
    if(!isLocalPath(library))return {name,library,health:'Library path cannot be inspected locally'};
    const found=await fs.stat(library).then(stat=>stat.isFile(),()=>false);
    return {name,library,health:found?'Manifest and library found':'Library file is missing'};
  } catch(error) {
    return {name:'Unknown layer',library:'',health:error.code==='ENOENT'?'Manifest file is missing':'Manifest could not be read or parsed'};
  }
}

function createLayerEditor({dataDir,executable,run=createHelperRunner(executable),ensureGamesClosed,inspectManifest=inspectLocalManifest,writeBackup=atomicWrite}) {
  const backupDir=path.join(dataDir,'layer-backups');
  let snapshots=new Map(),queue=Promise.resolve();
  const serial=operation=>{const result=queue.then(operation);queue=result.catch(()=>{});return result;};
  const backupFile=id=>path.join(backupDir,`${id}.json`);
  const write=record=>writeBackup(backupFile(record.id),JSON.stringify(record,null,2));
  const validId=id=>typeof id==='string' && /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(id);
  async function readRecords() {
    const errors=[];
    const files=await fs.readdir(backupDir).catch(error=>{if(error.code==='ENOENT')return [];throw error;});
    const records=[];
    for(const file of files.filter(file=>/^[0-9a-f-]{36}\.json$/.test(file))) {
      try {
        const filename=path.join(backupDir,file);
        if((await fs.stat(filename)).size>65536)throw Error('oversized backup');
        const record=JSON.parse(await fs.readFile(filename,'utf8'));
        if(!validId(record.id) || `${record.id}.json`!==file || record.version!==1 || !validTarget(record) || !isDword(record.before) || !isDword(record.after) || !['pending','confirmed','failed','restored'].includes(record.state))throw Error('invalid backup');
        records.push(canonical(record));
      } catch {errors.push(`A layer backup could not be read: ${file}`);}
    }
    return {records:records.sort((a,b)=>String(b.createdAt).localeCompare(String(a.createdAt))),errors};
  }
  async function rawInventory() {
    const value=await run(['--inventory']);
    if(!value || !Array.isArray(value.entries) || !Array.isArray(value.errors) || value.entries.length>16384)throw Error('The layer helper returned an invalid inventory.');
    const seen=new Map();
    for(const entry of value.entries) {
      if(!entry || !['HKCU','HKLM'].includes(entry.scope) || !['64','32'].includes(entry.view) || !['implicit','explicit'].includes(entry.kind) || typeof entry.manifest!=='string' || typeof entry.type!=='string')throw Error('The layer helper returned an invalid registration.');
      const identity=key(entry);
      if(seen.has(identity)) {
        const previous=seen.get(identity);
        if(entry.scope!=='HKCU')throw Error('The layer helper returned duplicate registrations.');
        if(previous.type!==entry.type || previous.value!==entry.value)throw Error('A shared current-user registration changed during inventory. Refresh the layer list.');
      } else seen.set(identity,canonical(entry));
    }
    return {...value,entries:[...seen.values()]};
  }
  async function inventoryInternal() {
    const [raw,history]=await Promise.all([rawInventory(),readRecords()]);
    const next=new Map();
    const entries=[];
    for(const entry of raw.entries) {
      const id=randomUUID();
      next.set(id,{...entry});
      entries.push({...entry,...await inspectManifest(entry.manifest),id,editable:isEditable(entry)});
    }
    const current=new Map(raw.entries.map(entry=>[key(entry),entry]));
    const backups=history.records.map(record=>({ ...record,canRestore:['confirmed','pending'].includes(record.state) && record.before!==record.after && isEditable(current.get(key(record))) && current.get(key(record)).value===record.after }));
    snapshots=next;
    return {entries,backups,errors:[...raw.errors,...history.errors]};
  }
  function requireAdmin(entry,admin) {
    if(entry.scope==='HKLM' && admin!==true)throw Error('Enable Admin mode to change a machine-wide registration.');
  }
  async function currentEntry(target,expected) {
    const current=(await rawInventory()).entries.find(entry=>key(entry)===key(target));
    if(!current || !isEditable(current) || current.value!==expected)throw Error('This registration changed or was removed. Refresh before editing it.');
    return current;
  }
  async function apply(record,expected,desired,restoring=false) {
    // Recheck after the durable backup and immediately before the helper starts.
    await ensureGamesClosed();
    await currentEntry(record,expected);
    let failure;
    try {await run([record.scope==='HKLM'?'--elevate-set':'--set',record.scope,record.view,record.kind,record.manifest,String(expected),String(desired)]);}
    catch(error){failure=error;}
    let observed;
    try {observed=(await rawInventory()).entries.find(entry=>key(entry)===key(record));}
    catch(error){failure ||= error;}
    const confirmed=!failure && isEditable(observed) && observed.value===desired;
    if(confirmed) {
      record.state=restoring?'restored':'confirmed';
      if(restoring)record.restoredAt=new Date().toISOString();
      delete record.error;
      await write(record);
      return;
    }
    if(!restoring)record.state=isEditable(observed)&&observed.value===expected?'failed':'pending';
    record.error=(failure?.message || 'Registry readback did not confirm the requested change.').slice(0,1000);
    await write(record);
    throw failure || Error(record.error);
  }
  return {
    inventory:()=>serial(inventoryInternal),
    setEnabled:request=>serial(async()=>{
      if(!request || typeof request.enabled!=='boolean')throw Error('The enabled value must be true or false.');
      const target=snapshots.get(request.id);
      if(!target)throw Error('Refresh the layer list before editing this registration.');
      if(!isEditable(target))throw Error('Only existing DWORD registrations with local JSON manifest paths can be edited.');
      requireAdmin(target,request.admin);
      await ensureGamesClosed();
      await currentEntry(target,target.value);
      const after=request.enabled?0:1;
      if((target.value===0)===request.enabled)return inventoryInternal();
      const metadata=await inspectManifest(target.manifest);
      const record={version:1,id:randomUUID(),scope:target.scope,view:target.view,sharedView:target.sharedView,kind:target.kind,manifest:target.manifest,name:metadata.name,before:target.value,after,state:'pending',createdAt:new Date().toISOString()};
      await write(record);
      await apply(record,target.value,after);
      return inventoryInternal();
    }),
    restore:request=>serial(async()=>{
      if(!validId(request?.id))throw Error('Unknown layer backup. Refresh the list.');
      const record=(await readRecords()).records.find(record=>record.id===request.id);
      if(!record || !['confirmed','pending'].includes(record.state) || record.before===record.after)throw Error('This layer backup cannot be restored.');
      requireAdmin(record,request.admin);
      await ensureGamesClosed();
      await currentEntry(record,record.after);
      await apply(record,record.after,record.before,true);
      return inventoryInternal();
    }),
  };
}
module.exports={createLayerEditor,createHelperRunner,inspectLocalManifest};
