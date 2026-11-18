"use strict";
const fs = require("node:fs/promises");
const path = require("node:path");
const { createHash, randomUUID } = require("node:crypto");
const { Worker, isMainThread, workerData, parentPort } = require("node:worker_threads");

const maxBytes = 32 * 1024 * 1024;
const maxTriangles = 250_000;
const builtin = ["hyper-p1", "hyper-sl", "gxl", "gt-max", "fpe2"];
const staged = new Map(), staging = new Map();
const { createReadStream } = require("node:fs");
const { setTimeout: delay } = require("node:timers/promises");
const cacheDirectory = () => path.join(process.env.LOCALAPPDATA, "OpenGridXR", "wheels");
function bad(message) { throw new Error(message); }

function convertWheel(input, extension) {
  if (!Buffer.isBuffer(input) || input.length > maxBytes) bad("Choose a wheel file up to 32 MiB.");
  const positions = [], texcoords = [], triangles = [];
  const minimum = [Infinity,Infinity,Infinity], maximum = [-Infinity,-Infinity,-Infinity];
  function triangle(a,b,c, uv, group) {
    if (triangles.length >= maxTriangles) bad("Wheel exceeds 250,000 triangles.");
    const u = b.map((x,i) => x-a[i]), v = c.map((x,i) => x-a[i]);
    const normal = [u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]];
    const length = Math.hypot(...normal);
    if (!Number.isFinite(length)) bad("Invalid wheel coordinates.");
    if (!length) return;
    const points = [a,b,c];
    for (const point of points) for (let i=0;i<3;i++) {
      minimum[i] = Math.min(minimum[i], point[i]);
      maximum[i] = Math.max(maximum[i], point[i]);
    }
    triangles.push({points,normal:normal.map(n => n/length),uv,group});
  }
  function coordinates(fields, n) {
    if (fields.length < n) bad("Missing wheel coordinates.");
    const result = fields.slice(0,n).map(Number);
    if (result.some(x => !Number.isFinite(x) || Math.abs(x)>1e9)) bad("Invalid wheel coordinates.");
    return result;
  }
  if (extension === ".stl") {
    const count = input.length >= 84 ? input.readUInt32LE(80) : 0;
    if (!count || count > maxTriangles || input.length !== 84+50*count) bad("Choose a complete binary STL with up to 250,000 triangles.");
    for (let i=0;i<count;i++) {
      const offset = 96+50*i;
      const points = [0,12,24].map(step => [0,4,8].map(axis => input.readFloatLE(offset+step+axis)));
      triangle(...points,null,0);
    }
  } else if (extension === ".obj") {
    let group = 0;
    function index(text, length) {
      if (!/^-?\d+$/.test(text)) bad("Invalid OBJ index.");
      const n = Number(text), i = n < 0 ? length+n : n-1;
      if (!Number.isSafeInteger(i) || i<0 || i>=length) bad("OBJ face references a missing vertex or UV.");
      return i;
    }
    // Scan incrementally: do not allocate an array proportional to a malicious newline count.
    const source = input.toString("utf8");
    for (let start=0;start<source.length;) {
      let end=source.indexOf("\n",start); if(end<0) end=source.length;
      if (end-start>16384) bad("OBJ line exceeds its size limit.");
      const line=source.slice(start,end).split("#",1)[0].trim();
      start=end+1;
      const [op,...values]=line.split(/\s+/);
      if (op==="v" || op==="vt") {
        const list=op==="v"?positions:texcoords;
        if(list.length>=750_000) bad("Too many OBJ vertices or UVs.");
        list.push(coordinates(values,op==="v"?3:2));
      } else if (op==="usemtl") {
        const name=values.join(" ").toLowerCase(), led=/^led_([1-9]\d?)$/.exec(name);
        group=name==="screen"?200:led && +led[1]<=96?200 + Number(led[1]):0;
      } else if (op==="f") {
        if(values.length<3 || values.length>256) bad("OBJ faces must have 3 to 256 vertices.");
        const refs=values.map(token=>{
          const [vertex,texture]=token.split("/");
          const uv = texture ? texcoords[index(texture,texcoords.length)] : null;
          if(group===200 && (!uv || uv.some(n=>n<0 || n>1))) bad("Screen faces need UV coordinates between 0 and 1.");
          return {p:positions[index(vertex,positions.length)],uv};
        });
        for(let j=1;j+1<refs.length;j++){
          const face=[refs[0],refs[j],refs[j+1]];
          triangle(...face.map(ref=>ref.p), group===200?face.map(ref=>[ref.uv[0],1-ref.uv[1]]):null,group);
        }
      }
    }
  } else bad("Choose a Wavefront OBJ or binary STL file.");
  const width=maximum[0]-minimum[0];
  if(!triangles.length || !(width>0) || maximum.some((n,i)=>(n-minimum[i])/width>10)) bad("Wheel must have finite geometry with a measurable width along X.");
  const count=3*triangles.length, result=Buffer.alloc(32+40*count+4);
  result.write("AGMESH1"); [5,count,count,1,1,0].forEach((n,i)=>result.writeUInt32LE(n,8+4*i));
  let vertex=0;
  for(const {points,normal,uv,group} of triangles) for(let j=0;j<3;j++){
    const point=points[j].map((n,i)=>(n-(maximum[i]+minimum[i])/2)/width);
    [...point,...normal,...(uv?.[j] || [0,0]),group].forEach((n,k)=>result.writeFloatLE(n,32+36*vertex+4*k));
    result.writeUInt32LE(vertex,32+36*count+4*vertex); vertex++;
  }
  result.set([105,110,120,255],result.length-4);
  return result;
}

async function importWheelFile(filename, directory=cacheDirectory()) {
  // Read through the selected descriptor, bound even if the file grows after stat.
  const handle=await fs.open(filename,"r");
  let bytes;
  try {
    const stat=await handle.stat();
    if(!stat.isFile() || stat.size>maxBytes) bad("Choose a regular file up to 32 MiB.");
    bytes=Buffer.alloc(stat.size);
    for(let position=0;position<bytes.length;) {
      const {bytesRead}=await handle.read(bytes,position,bytes.length-position,position);
      if(!bytesRead) bad("Wheel file changed during import.");
      position+=bytesRead;
    }
  } finally { await handle.close(); }
  const mesh=convertWheel(bytes,path.extname(filename).toLowerCase());
  const id=createHash("sha256").update(mesh).digest("hex");
  await fs.mkdir(directory,{recursive:true});
  const temp=path.join(directory,randomUUID()+".tmp");
  const dest=path.join(directory,id+".agm");
  const existing=await fs.stat(dest).catch(error=>{if(error.code!=="ENOENT")throw error;});
  if(existing?.size!==mesh.length || await fileHash(dest)!==id) {
    try { await fs.writeFile(temp,mesh,{flag:"wx"}); await publishFile(temp,dest); }
    finally { await fs.rm(temp,{force:true}); }
  }
  return {id,name:path.basename(filename,path.extname(filename)).replace(/[\x00-\x1f\x7f]/g,"").slice(0,80) || "Custom wheel"};
}
function importInWorker(filename) {
  return new Promise((resolve,reject)=>{
    const worker=new Worker(__filename.replace(/([\\/])app\.asar([\\/])/, "$1app.asar.unpacked$2"),{workerData:filename,resourceLimits:{maxOldGenerationSizeMb:512}});
    worker.once("message",m=>m.error?reject(Error(m.error)):resolve(m.wheel));
    worker.once("error",reject);
    worker.once("exit",code=>{if(code)reject(Error("Wheel exceeded import resources."));});
  });
}
async function fileHash(filename) {
  const hash=createHash("sha256");
  for await(const chunk of createReadStream(filename)) hash.update(chunk);
  return hash.digest("hex");
}

async function publishFile(temp,dest) {
  for(let retry=0;;retry++) {
    try {await fs.rename(temp,dest);return;}
    catch(error) {
      if(!["EPERM","EBUSY","EACCES"].includes(error.code) || retry>=6)throw error;
      await delay(40*2**retry);
    }
  }
}
async function stageBuiltin(id,directory,builtinDirectory) {
  const filename=id+".agm",dest=path.join(directory,filename);
  const source=path.join(builtinDirectory,filename), stat=await fs.stat(source);
  const cached=staged.get(dest);
  const existing=await fs.stat(dest).catch(error=>{if(error.code!=="ENOENT")throw error;});
  if(cached && cached.source===source && cached.mtime===stat.mtimeMs && cached.size===stat.size &&
     existing?.size===cached.size && existing.mtimeMs===cached.destMtime)return;
  const manifest=JSON.parse(await fs.readFile(path.join(builtinDirectory,id+".conversion.json"),"utf8"));
  if(manifest.file!==filename || manifest.bytes!==stat.size || !/^[a-f0-9]{64}$/.test(manifest.sha256))
    bad("Bundled wheel manifest does not match the model.");
  const remember=destStat=>staged.set(dest,{source,mtime:stat.mtimeMs,size:stat.size,destMtime:destStat.mtimeMs});
  if(existing?.size===manifest.bytes && await fileHash(dest)===manifest.sha256) {
    remember(existing);
    return;
  }
  await fs.mkdir(directory,{recursive:true});
  const temp=path.join(directory,randomUUID()+".tmp");
  try {
    await fs.copyFile(source,temp);
    if(await fileHash(temp)!==manifest.sha256)bad("Bundled wheel failed its integrity check.");
    await publishFile(temp,dest);
    remember(await fs.stat(dest));
  } finally {await fs.rm(temp,{force:true});}
}
async function stageWheel(id,builtinDirectory) {
  if(!builtin.includes(id) && !/^[a-f0-9]{64}$/.test(id)) bad("Invalid wheel model.");
  const directory=cacheDirectory(), dest=path.join(directory,id+".agm");
  if(!builtin.includes(id)) {
    await fs.access(dest).catch(()=>bad("Import this custom wheel on this PC, then save the profile."));
    return;
  }
  if(staging.has(dest))return staging.get(dest);
  const task=stageBuiltin(id,directory,builtinDirectory);
  staging.set(dest,task);
  try {await task;} finally {staging.delete(dest);}
}
if(!isMainThread && workerData) importWheelFile(workerData).then(wheel=>parentPort.postMessage({wheel}),e=>parentPort.postMessage({error:e.message}));
module.exports={convertWheel,importWheelFile,importInWorker,stageWheel,builtin};
