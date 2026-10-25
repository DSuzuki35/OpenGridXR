import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { createStore, atomicWrite } from "../desktop/store.cjs";
import {spawn} from 'node:child_process';
import { toRuntimeIni } from "../desktop/runtime-config.cjs";
import { createDocument } from "../src/model.mjs";

async function fixture(t) {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), "opengrid-store-"));
  t.after(() => fs.rm(dir, { recursive: true, force: true }));
  return { dir, store: await createStore(dir) };
}

test('atomic save tolerates a short Windows reader lock without deleting the original', {skip:process.platform!=='win32'}, async(t)=>{
  const {dir}=await fixture(t),file=path.join(dir,'locked.json');
  await fs.writeFile(file,'original');
  const reader=spawn('powershell.exe',['-NoProfile','-NonInteractive','-Command',"$lockedStream=[IO.File]::Open($env:OPENGRID_LOCK_FILE,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite); [Console]::WriteLine('LOCKED'); [Console]::ReadLine() | Out-Null; $lockedStream.Dispose();"],{windowsHide:true,env:{...process.env,OPENGRID_LOCK_FILE:file},stdio:['pipe','pipe','pipe']});
  const exited=new Promise(resolve=>reader.once('exit',resolve));
  let writing;
  try {
    await new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(new Error('Reader lock startup timed out')),10000);reader.stdout.on('data',data=>{if(String(data).includes('LOCKED')){clearTimeout(timer);resolve();}});reader.once('error',reject);reader.once('exit',code=>{if(code)reject(new Error(`Reader lock failed: ${code}`));});});
    let settled=false,result;writing=atomicWrite(file,'replacement');writing.then(()=>{settled=true;result='succeeded';},error=>{settled=true;result=error.code;});
    await new Promise(resolve=>setTimeout(resolve,80));
    assert.equal(settled,false,'A temporary reader lock should be retried rather than fail the save: '+result);
    assert.equal(await fs.readFile(file,'utf8'),'original');
    reader.stdin.end('\n');await exited;await writing;
    assert.equal(await fs.readFile(file,'utf8'),'replacement');
    assert.deepEqual((await fs.readdir(dir)).filter(name=>name.endsWith('.tmp')),[]);
  } finally {reader.stdin.end('\n');await exited;await writing?.catch(()=>{});}
});
test("save persists independent game profiles and rejects stale writers", async (t) => {
  const { store, dir } = await fixture(t);
  const a = await store.load();
  a.document.profiles[0].name = "My race setup";
  const b = await store.save(a.document, a.revision);
  assert.equal((await store.load()).document.profiles[0].name, "My race setup");
  await assert.rejects(() => store.save(a.document, a.revision), /changed/i);
  assert.notEqual(a.revision, b.revision);
  assert.equal(
    JSON.parse(await fs.readFile(path.join(dir, "profiles.json"), "utf8"))
      .version,
    1,
  );
});
test("backup restores exact prior profile and preserves current document", async (t) => {
  const { store } = await fixture(t);
  const a = await store.load();
  const b = await store.save(a.document, a.revision);
  b.document.profiles[0].view.crop.left = 12;
  const c = await store.save(b.document, b.revision);
  assert.equal(c.backups.length, 1);
  const d = await store.restore(c.backups[0].id, c.revision);
  assert.equal(d.document.profiles[0].view.crop.left, 0);
  assert.equal(d.backups.length, 2);
});
test("malformed current data remains on disk and can be recovered from backup", async (t) => {
  const { store, dir } = await fixture(t);
  let a = await store.load();
  a = await store.save(a.document, a.revision);
  a.document.profiles[0].name = "Changed";
  a = await store.save(a.document, a.revision);
  await fs.writeFile(path.join(dir, "profiles.json"), "{broken");
  const broken = await store.load();
  assert.ok(broken.recoveryError);
  await assert.rejects(
    () => store.save(broken.document, broken.revision),
    /restore/i,
  );
  const restored = await store.restore(broken.backups[0].id, broken.revision);
  assert.equal(restored.recoveryError, undefined);
  const raw = await Promise.all(
    restored.backups.map((b) =>
      fs.readFile(path.join(dir, "backups", b.id), "utf8"),
    ),
  );
  assert.ok(raw.includes("{broken"));
});
test("imported malicious paths cannot escape backup folder", async (t) => {
  const { store } = await fixture(t);
  const a = await store.load();
  await assert.rejects(
    () => store.restore("../profiles.json", a.revision),
    /backup/i,
  );
});
test("runtime conversion never injects profile names and disables contour during streaming", () => {
  const doc = createDocument();
  const p = doc.profiles[0];
  p.name = "My custom setup";
  p.mask.enabled = true;
  p.mode = "streaming";
  p.view.crop.left = 12;
  const ini = toRuntimeIni(doc);
  assert.match(ini, /\[iracing\]/);
  assert.match(ini, /Mode=streaming/);
  assert.match(ini, /CropLeft=12/);
  assert.match(ini, /MaskEnabled=0/);
  assert.doesNotMatch(ini, /My custom setup/);
  p.mode = "driving";
  assert.match(toRuntimeIni(doc), /MaskEnabled=1/);
});
test("invalid save leaves original and backups unchanged", async (t) => {
  const { store } = await fixture(t);
  let a = await store.load();
  a = await store.save(a.document, a.revision);
  a.document.profiles[0].mask.left[0].x = 99;
  await assert.rejects(() => store.save(a.document, a.revision));
  const b = await store.load();
  assert.equal(b.revision, a.revision);
  assert.equal(b.backups.length, 0);
});
test("a crashed writer lock is recovered without bypassing the revision check", async (t) => {
  const { store, dir } = await fixture(t);
  const a = await store.load();
  await fs.writeFile(
    path.join(dir, "profiles.lock"),
    JSON.stringify({ pid: 2147483647 }),
  );
  const b = await store.save(a.document, a.revision);
  assert.notEqual(b.revision, a.revision);
});
test("oversized document is rejected before either backup or data changes", async (t) => {
  const { store } = await fixture(t);
  let a = await store.load();
  a = await store.save(a.document, a.revision);
  a.document.profiles[0].extra = "x".repeat(2 * 1024 * 1024);
  await assert.rejects(() => store.save(a.document, a.revision), /2 MiB/);
  const b = await store.load();
  assert.equal(b.revision, a.revision);
  assert.equal(b.backups.length, 0);
});
test("an oversized existing file can be restored and is preserved exactly", async (t) => {
  const { store, dir } = await fixture(t);
  let a = await store.load();
  a = await store.save(a.document, a.revision);
  a.document.profiles[0].name = "Backup";
  a = await store.save(a.document, a.revision);
  const large = Buffer.alloc(2 * 1024 * 1024 + 1, 120);
  await fs.writeFile(path.join(dir, "profiles.json"), large);
  const bad = await store.load();
  assert.ok(bad.recoveryError);
  const recovered = await store.restore(bad.backups[0].id, bad.revision);
  assert.ok(!recovered.recoveryError);
  assert.ok(
    (
      await Promise.all(
        recovered.backups.map((b) => fs.stat(path.join(dir, "backups", b.id))),
      )
    ).some((s) => s.size === large.length),
  );
});
