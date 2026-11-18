import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url);
const row=(sequence,timestampMs=1000+sequence*10)=>({sequence,timestampMs,frameMs:10,cpuMs:null,gpuMs:null});
async function fixture(fn){
  const directory=await fs.mkdtemp(path.join(os.tmpdir(),'opengrid-frametimes-'));
  try{await fn(directory);}finally{await fs.rm(directory,{recursive:true,force:true});}
}
const make=(directory,options={})=>require('../desktop/frametime-log.cjs').createFrametimeLog({directory,game:'iracing',startedAtMs:1000,setTimer:()=>0,clearTimer:()=>{},...options});

test('CSV writes ordered actual frame rows, missing timings and visible sequence gaps',async()=>fixture(async directory=>{
  const log=make(directory);
  log.append(row(4,1020));log.append({...row(7,1050),cpuMs:4,gpuMs:6});
  await log.close();
  const csv=await fs.readFile(log.status().filePath,'utf8');
  assert.equal(csv,'sequence,timestamp_ms,elapsed_ms,frame_interval_ms,cpu_ms,gpu_ms,missing_frames_before,timestamp_gap_ms\n4,1020,20,10,,,,\n7,1050,50,10,4,6,2,30\n');
  assert.equal(log.status().writtenRows,2);assert.equal(log.status().state,'complete');
}));

test('underfilled batches flush periodically and on close, without rewriting previous rows',async()=>fixture(async directory=>{
  let periodic;
  const log=make(directory,{batchSize:3,setTimer:fn=>{periodic=fn;return 0;}});
  log.append(row(1));await log.flush();
  log.append(row(2));await periodic();
  assert.equal(log.status().writtenRows,2);
  log.append(row(3));await log.close();await log.close();
  assert.equal((await fs.readFile(log.status().filePath,'utf8')).trim().split('\n').length,4);
  assert.equal(log.status().writtenRows,3);
}));

test('a full batch is written asynchronously while recording continues',async()=>fixture(async directory=>{
  const log=make(directory,{batchSize:2});
  log.append(row(1));log.append(row(2));
  assert.equal(log.status().writtenRows,0);
  const deadline=Date.now()+1000;
  while(log.status().writtenRows<2&&Date.now()<deadline)await new Promise(resolve=>setTimeout(resolve,5));
  assert.equal(log.status().writtenRows,2);assert.equal(log.status().state,'recording');
  await log.close();
}));

test('blocked writes keep total pending rows bounded and expose overflow rather than silently dropping',async()=>fixture(async directory=>{
  let release,entered;
  const blocked=new Promise(resolve=>{release=resolve;}),started=new Promise(resolve=>{entered=resolve;});
  const io={...fs,async open(...args){const handle=await fs.open(...args);return {async writeFile(data){if(!data.startsWith('sequence,')){entered();await blocked;}return handle.writeFile(data);},close:()=>handle.close()};}};
  const log=make(directory,{fs:io,batchSize:1,maxQueuedRows:2});
  log.append(row(1));await started;
  assert.equal(log.append(row(2)),true);
  assert.equal(log.append(row(3)),false);
  assert.equal(log.status().queuedRows,2);assert.match(log.status().error,/queue|backpressure/i);
  assert.equal(log.status().droppedRows,1);
  release();await log.close();
  assert.equal(log.status().writtenRows,2);assert.equal(log.status().state,'error');
  assert.equal((await fs.readFile(log.status().filePath,'utf8')).trim().split('\n').length,3);
}));

test('unwritable destination is visible and never prevents close from settling',async()=>fixture(async directory=>{
  const blocked=path.join(directory,'file');await fs.writeFile(blocked,'file');
  const log=make(blocked);log.append(row(1));await log.close();
  assert.equal(log.status().state,'error');assert.ok(log.status().error);assert.equal(log.status().writtenRows,0);
  assert.equal(log.status().droppedRows,1);assert.ok(log.status().filePath.startsWith(blocked));
}));

test('failed batch is reported as incomplete and later appends cannot masquerade as saved',async()=>fixture(async directory=>{
  const io={...fs,async open(...args){const handle=await fs.open(...args);return {async writeFile(data){if(!data.startsWith('sequence,'))throw new Error('disk full');return handle.writeFile(data);},close:()=>handle.close()};}};
  const log=make(directory,{fs:io});log.append(row(1));await log.flush();
  assert.match(log.status().error,/disk full/);assert.equal(log.append(row(2)),false);await log.close();
  assert.equal(log.status().writtenRows,0);assert.equal(log.status().droppedRows,2);
  assert.equal((await fs.readFile(log.status().filePath,'utf8')).trim().split('\n').length,1);
}));

test('close drains a tail queued while the previous asynchronous drain is settling',async()=>fixture(async directory=>{
  let log,finishTail;
  const tailClosed=new Promise(resolve=>{finishTail=resolve;});
  const io={...fs,async open(...args){const handle=await fs.open(...args);return {
    writeFile(data){return handle.writeFile(data).then(()=>{
      if(data.startsWith('1,'))queueMicrotask(()=>queueMicrotask(()=>{
        log.append(row(2));finishTail(log.close());
      }));
    });},close:()=>handle.close(),
  };}};
  log=make(directory,{fs:io,batchSize:1});log.append(row(1));await tailClosed;
  assert.equal(log.status().queuedRows,0);assert.equal(log.status().writtenRows,2);
  assert.equal((await fs.readFile(log.status().filePath,'utf8')).trim().split('\n').length,3);
}));
