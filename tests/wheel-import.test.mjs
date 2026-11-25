import test from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import * as fs from "node:fs/promises";
import path from "node:path";
import os from "node:os";
import { convertWheel, stageWheel } from "../desktop/wheel-import.cjs";
import { createSpatialOverlay, validateSpatialOverlays, wheelModels } from "../src/spatial-overlays.mjs";
const obj = Buffer.from("v -3 0 0\nv 3 0 0\nv 0 2 1\nf -3 -2 -1\n");
test("OBJ and binary STL become width-normalized native wheel triangles", () => {
  const stl = Buffer.alloc(134);
  stl.writeUInt32LE(1,80);
  [-3,0,0,3,0,0,0,2,1].forEach((v,i) => stl.writeFloatLE(v,96+4*i));
  for (const bytes of [convertWheel(obj,".obj"),convertWheel(stl,".stl")]) {
    assert.equal(bytes.toString("ascii",0,7),"AGMESH1");
    assert.equal(bytes.readUInt32LE(12),3);
    assert.equal(bytes.readFloatLE(32+36)-bytes.readFloatLE(32),1);
  }
});
test("invalid indices, nonfinite coordinates, truncated STL and degenerate geometry fail", () => {
  for (const bytes of [Buffer.from("v 0 0 0\nf 1 2 3"),Buffer.from("v nan 0 0"),Buffer.from("v 1 0 0\nf 1 1 1")])
    assert.throws(() => convertWheel(bytes,".obj"));
  assert.throws(() => convertWheel(Buffer.alloc(100),".stl"));
});
test("wheel profile references cannot name files or external URLs", () => {
  const item = {...createSpatialOverlay(), wheelModel:"hyper-p1", wheelLights:true};
  const validate = i => validateSpatialOverlays({enabled:true,pixelBudgetMP:60,items:[i]});
  validate(item);
  assert.throws(() => validate({...item,wheelModel:"../../file"}));
  assert.throws(() => validate({...item,wheelLights:"true"}));
  for (const strength of [0, 42.5, 100]) validate({...item,wheelAmbientLight:strength});
  for (const strength of [-1, 101, NaN, "50"]) assert.throws(() => validate({...item,wheelAmbientLight:strength}));
});

test("experimental driver validates themes and only enables for supported wheels", () => {
  const wheel = { ...createSpatialOverlay(), wheelModel: "ascher-artura" };
  const validate = item => validateSpatialOverlays({ enabled: true, pixelBudgetMP: 60, items: [item] });
  validate(wheel);
  validate({ ...wheel, driverEnabled: true, driverGloveTheme: 0, driverSuitTheme: 4, driverTorso: true, driverLegs: false });
  for (const model of wheelModels) validate({ ...wheel, wheelModel: model.id, driverEnabled: true });
  for (const patch of [{ driverEnabled: 1 }, { driverGloveTheme: 1.5 }, { driverSuitTheme: "4" },
    { driverSuitTheme: 5 }, { driverTorso: 0 }, { driverLegs: "false" }, { wheelModel: "a".repeat(64), driverEnabled: true }])
    assert.throws(() => validate({ ...wheel, ...patch }));
});

test("all GSI wheels share one verified driver asset", async () => {
  const { stageDriver } = await import("../desktop/driver-assets.cjs");
  const root = await fs.mkdtemp(path.join(os.tmpdir(), "driver-gsi-"));
  try {
    const source = path.resolve("assets/drivers");
    await Promise.all(wheelModels.filter(m=>m.id!=="ascher-artura").map(m=>stageDriver(m.id,source,root)));
    const folder=path.join(root,"OpenGridXR","wheels");
    assert.deepEqual(await fs.readdir(folder),["fpe2.agd"]);
    assert.deepEqual(await fs.readFile(path.join(folder,"fpe2.agd")),await fs.readFile(path.join(source,"fpe2.agd")));
  } finally { await fs.rm(root,{recursive:true,force:true}); }
});

test("stages verified driver assets beside wheels and preserves the cache on corrupt input", async () => {
  const { stageDriver } = await import("../desktop/driver-assets.cjs");
  const root = await fs.mkdtemp(path.join(os.tmpdir(), "driver-stage-"));
  const bundle = path.join(root, "bundle");
  const name = "ascher-artura.agd";
  const source = path.join(bundle, name), dest = path.join(root, "OpenGridXR", "wheels", name);
  const manifest = path.join(bundle, "ascher-artura.driver.json");
  const bytes = Buffer.from("verified driver asset"), corrupt = Buffer.alloc(bytes.length);
  const stage = () => stageDriver("ascher-artura", bundle, root);
  try {
    await assert.rejects(() => stageDriver("../ascher-artura", bundle, root), /Unsupported/);
    await assert.rejects(stage, /missing/i);
    await fs.mkdir(bundle);
    await fs.writeFile(source, bytes);
    const metadata = { file: name, bytes: bytes.length, sha256: createHash("sha256").update(bytes).digest("hex") };
    await fs.writeFile(manifest, JSON.stringify(metadata));
    await Promise.all([stage(), stage()]);
    assert.deepEqual(await fs.readFile(dest), bytes);
    await fs.writeFile(dest, corrupt);
    await fs.utimes(dest, new Date(0), new Date(0));
    await stage();
    assert.deepEqual(await fs.readFile(dest), bytes);
    await fs.writeFile(manifest, JSON.stringify({ ...metadata, sha256: "0".repeat(64) }));
    await assert.rejects(stage, /integrity/);
    assert.deepEqual(await fs.readFile(dest), bytes);
    assert.deepEqual(await fs.readdir(path.dirname(dest)), [name]);
  } finally { await fs.rm(root, { recursive: true, force: true }); }
});

test("concurrent staging reuses a verified disk model and repairs same-size corruption", async () => {
  const root=await fs.mkdtemp(path.join(os.tmpdir(),"wheel-stage-")), previous=process.env.LOCALAPPDATA;
  const source=path.join(root,"bundle"), target=path.join(root,"OpenGridXR","wheels"), id="gxl";
  const filename=id+".agm",bytes=Buffer.from("checked wheel bytes"), dest=path.join(target,filename);
  try {
    process.env.LOCALAPPDATA=root;
    await Promise.all([fs.mkdir(source),fs.mkdir(target,{recursive:true})]);
    await fs.writeFile(path.join(source,filename),bytes);
    await fs.writeFile(path.join(source,id+".conversion.json"),JSON.stringify({
      file:filename,bytes:bytes.length,sha256:createHash("sha256").update(bytes).digest("hex"),
    }));
    await fs.writeFile(dest,bytes);
    const old=new Date("2020-01-01T00:00:00Z");
    await fs.utimes(dest,old,old);
    await fs.chmod(dest,0o444);
    await Promise.all([stageWheel(id,source),stageWheel(id,source)]);
    assert.equal((await fs.stat(dest)).mtimeMs,old.getTime(),"valid read-only file survives a cold cache");
    await fs.chmod(dest,0o666);
    await fs.writeFile(dest,Buffer.alloc(bytes.length,42));
    await Promise.all([stageWheel(id,source),stageWheel(id,source)]);
    assert.deepEqual(await fs.readFile(dest),bytes);
    assert.deepEqual(await fs.readdir(target),[filename]);
  } finally {
    if(previous===undefined)delete process.env.LOCALAPPDATA;else process.env.LOCALAPPDATA=previous;
    await fs.chmod(dest,0o666).catch(()=>{});await fs.rm(root,{recursive:true,force:true});
  }
});
