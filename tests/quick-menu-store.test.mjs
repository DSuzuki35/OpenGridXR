import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs/promises';import os from 'node:os';import path from 'node:path';import {createRequire} from 'node:module';
import {createDocument} from '../src/model.mjs';const {createStore}=createRequire(import.meta.url)('../desktop/store.cjs');
test('source invalidation during asynchronous profile writing cannot commit a queued menu toggle',async t=>{
 const directory=await fs.mkdtemp(path.join(os.tmpdir(),'opengrid-menu-guard-'));t.after(()=>fs.rm(directory,{recursive:true,force:true}));
 const store=await createStore(directory),initial=await store.load();await store.save(createDocument(),initial.revision);const before=await store.load();
 let current=true;const originalOpen=fs.open;t.after(()=>{fs.open=originalOpen;});
 fs.open=async function(file,...args){const handle=await originalOpen.call(this,file,...args);if(String(file).endsWith('.tmp'))current=false;return handle;};
 const next=structuredClone(before.document);next.profiles[0].name='Should not commit';
 await assert.rejects(store.save(next,before.revision,{beforeReplace:()=>{if(!current)throw Error('Menu source changed');}}),/source changed/);
 assert.deepEqual((await store.load()).document,before.document);
});
