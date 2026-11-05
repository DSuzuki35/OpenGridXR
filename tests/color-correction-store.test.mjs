import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {createRequire} from 'node:module';
import {createDocument} from '../src/model.mjs';
const {createStore}=createRequire(import.meta.url)('../desktop/store.cjs');
test('legacy correction stays off when the app Helmet module was disabled',async()=>{
  const directory=await fs.mkdtemp(path.join(os.tmpdir(),'opengrid-color-migration-'));
  try {
    const doc=createDocument(),p=doc.profiles[0];delete p.colorCorrection;
    p.helmet.enabled=true;p.helmet.visorBrightness=70;
    await fs.writeFile(path.join(directory,'profiles.json'),JSON.stringify(doc));
    const store=await createStore(directory,{helmetModuleEnabled:()=>false});
    const result=await store.load();
    assert.equal(result.document.profiles[0].colorCorrection.enabled,false);
    assert.equal(result.document.profiles[0].colorCorrection.presets.custom.brightness,70);
  }finally{await fs.rm(directory,{recursive:true,force:true});}
});
