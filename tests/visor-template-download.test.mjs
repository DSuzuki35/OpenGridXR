import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs/promises';import os from 'node:os';import path from 'node:path';import {createRequire} from 'node:module';
let createVisorTemplates;try{({createVisorTemplates}=createRequire(import.meta.url)('../desktop/visor-templates.cjs'));}catch{}
async function fixture(t){assert.equal(typeof createVisorTemplates,'function');const directory=await fs.mkdtemp(path.join(os.tmpdir(),'opengrid-template-save-'));t.after(()=>fs.rm(directory,{recursive:true,force:true}));let choice={canceled:true},dialogs=[];const service=createVisorTemplates({assetDirectory:directory,showSaveDialog:async options=>{dialogs.push(options);return choice;}});return{service,directory,dialogs,choose:value=>choice=value};}
test('template exports use fixed style/format assets and preserve PSD bytes',async t=>{
 const f=await fixture(t),bytes=Buffer.from('8BPS-template-fixture');await fs.writeFile(path.join(f.directory,'visor-template-gt.psd'),bytes);const target=path.join(f.directory,'my-art.psd');f.choose({canceled:false,filePath:target});
 assert.deepEqual(await f.service.download({style:'gt',format:'psd'}),{saved:true,fileName:'my-art.psd'});assert.deepEqual(await fs.readFile(target),bytes);assert.equal(f.dialogs[0].defaultPath,'visor-template-gt.psd');assert.deepEqual(f.dialogs[0].filters[0].extensions,['psd']);
});
test('cancel exports no files; unsupported/path-injection requests never show a dialog',async t=>{
 const f=await fixture(t);await fs.writeFile(path.join(f.directory,'visor-template.png'),'fixture');assert.deepEqual(await f.service.download({style:'formula',format:'png'}),{saved:false,canceled:true});
 for(const request of [null,[],{style:'../gt',format:'psd'},{style:'gt',format:'../secret'},{style:'gt',format:'png',path:'private'}])await assert.rejects(f.service.download(request),/template|unsupported/i);
 assert.equal(f.dialogs.length,1);assert.deepEqual(await fs.readdir(f.directory),['visor-template.png']);
});
test('missing templates fail clearly without writing or changing profile data',async t=>{
 const f=await fixture(t);await assert.rejects(f.service.download({style:'open',format:'guide'}),/template.*(missing|available|build)/i);assert.equal(f.dialogs.length,0);
});
