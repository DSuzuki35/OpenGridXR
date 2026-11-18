import test from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import * as fs from "node:fs/promises";
import path from "node:path";
import os from "node:os";
import { convertWheel, stageWheel } from "../desktop/wheel-import.cjs";
import { createSpatialOverlay, validateSpatialOverlays } from "../src/spatial-overlays.mjs";
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
