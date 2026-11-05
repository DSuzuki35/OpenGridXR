import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import controller from '../desktop/benchmark-controller.cjs';

async function fixture(fn) {
  const root=await fs.mkdtemp(path.join(os.tmpdir(),'opengrid-benchmark-'));
  const sessionId='123-100-fixture';let clock=10000,recording=false;const accepted=[],started=[];
  const recorder={start(options){started.push(options);recording=true;return this.status();},accept(p,t){accepted.push([p,t]);},stop(reason){recording=false;return {reason};},status(){return {running:recording};},report(){return {ok:true};}};
  await fs.mkdir(path.join(root,'status'),{recursive:true});
  await fs.writeFile(path.join(root,'status',`${sessionId}.json`),JSON.stringify({protocolVersion:1,sessionId,processId:123,application:'iRacingSim64DX11.exe',updatedAtUnixMilliseconds:clock,capabilities:{benchmark:true}}));
  const app=controller.createBenchmarkController({root,now:()=>clock,isAlive:()=>true,createRecorder:()=>recorder,telemetry:async()=>({record:{game:'iracing',timestampMs:clock}}),setTimer:()=>0,clearTimer:()=>{}});
  try{await fn({app,root,sessionId,accepted,started,setClock:v=>clock=v});}finally{await app.close();await fs.rm(root,{recursive:true,force:true});}
}

test('benchmark metadata records explicit full render area policy with legacy fallback',async()=>{
  for(const [profile,expected] of [[{stream:{preserveFullFrame:true}},true],[{mode:'streaming'},true],[{mode:'streaming',stream:{preserveFullFrame:false}},false],[{},false]]) {
    await fixture(async({app,started})=>{
      await app.start({game:'iracing',...profile});
      assert.equal(started[0].metadata.preserveFullFrame,expected);
      assert.equal(Object.hasOwn(started[0].metadata,'mode'),false);
    });
  }
});
test('benchmark starts only for a fresh matching capable session and owns its lease',async()=>fixture(async({app,root,sessionId,accepted,setClock})=>{
  await app.start({id:'p',game:'iracing',name:'Test'});
  const request=JSON.parse(await fs.readFile(path.join(root,'control',`${sessionId}_benchmark-request.json`),'utf8'));
  assert.equal(request.sessionId,sessionId);assert.equal(request.expiresAtMs-request.requestedAtMs,4000);
  await fs.writeFile(path.join(root,'status',`${sessionId}_benchmark.json`),JSON.stringify({sessionId,timestampMs:10000,frames:[],counters:{application:10,generated:5}}));
  setClock(10250);await app.tick();assert.equal(accepted.at(-1)[0].counters.generated,5);
  await app.stop();assert.equal(app.status().running,false);
  await assert.rejects(fs.access(path.join(root,'control',`${sessionId}_benchmark-request.json`)));
}));
test('benchmark refuses stale or ambiguous sessions and cannot start twice',async()=>fixture(async({app,root,sessionId,setClock})=>{
  setClock(15000);await assert.rejects(app.start({game:'iracing'}),/running engine/);
  setClock(10000);await app.start({game:'iracing'});await assert.rejects(app.start({game:'iracing'}),/already/);
}));
test('benchmark ignores wrong-session feeds and stops when its process disappears from live status',async()=>fixture(async({app,root,sessionId,accepted,setClock})=>{
  await app.start({game:'iracing'});
  await fs.writeFile(path.join(root,'status',`${sessionId}_benchmark.json`),JSON.stringify({sessionId:'999-1-other',timestampMs:10000,frames:[]}));
  await app.tick();assert.equal(accepted.filter(([p])=>p!==null).length,0);
  setClock(16000);await app.tick();assert.equal(app.status().running,false);
}));

async function realFixture(fn,{unwritable=false}={}){
  const root=await fs.mkdtemp(path.join(os.tmpdir(),'opengrid-benchmark-csv-'));
  const logDirectory=path.join(root,'logs'),sessionId='123-100-fixture';let clock=10000;
  await fs.mkdir(path.join(root,'status'));
  const statusFile=path.join(root,'status',`${sessionId}.json`);
  await fs.writeFile(statusFile,JSON.stringify({protocolVersion:1,sessionId,processId:123,application:'iRacingSim64DX11.exe',updatedAtUnixMilliseconds:clock,capabilities:{benchmark:true}}));
  if(unwritable)await fs.writeFile(logDirectory,'blocked');
  const app=controller.createBenchmarkController({root,logDirectory,now:()=>clock,isAlive:()=>true,telemetry:async()=>null,setTimer:()=>0,clearTimer:()=>{}});
  const publish=async(timestampMs,frames)=>{clock=timestampMs;await fs.writeFile(path.join(root,'status',`${sessionId}_benchmark.json`),JSON.stringify({sessionId,timestampMs,frames}));await app.tick();};
  try{await fn({app,root,logDirectory,statusFile,publish});}finally{await app.close();await fs.rm(root,{recursive:true,force:true});}
}
const sample=(sequence,timestampMs)=>({sequence,timestampMs,frameMs:10,cpuMs:null,gpuMs:null});

test('raw CSV defaults off and does not create a log directory',async()=>realFixture(async({app,logDirectory,publish})=>{
  await app.start({game:'iracing'});await publish(10250,[sample(1,10010)]);await app.stop();
  assert.equal(app.status().frametimeLog.enabled,false);
  assert.equal(app.report().frames.length,1);
  await assert.rejects(fs.access(logDirectory));
}));

for(const ending of ['stop','session-ended','close'])test(`enabled raw CSV deduplicates snapshots and flushes on ${ending}`,async()=>realFixture(async({app,statusFile,publish})=>{
  await app.start({game:'iracing'},{logFrametimes:true});
  await publish(10250,[sample(1,10010),sample(2,10020)]);
  await publish(10500,[sample(1,10010),sample(2,10020),sample(4,10040)]);
  if(ending==='session-ended'){await fs.unlink(statusFile);await publish(10750,[]);}
  else await app[ending]();
  const status=app.status().frametimeLog;
  assert.equal(status.enabled,true);assert.equal(status.state,'complete');assert.equal(status.error,null);
  assert.equal(status.queuedRows,0);assert.equal(status.writtenRows,3);assert.equal(status.droppedRows,0);
  assert.equal(await fs.readFile(status.filePath,'utf8'),'sequence,timestamp_ms,elapsed_ms,frame_interval_ms,cpu_ms,gpu_ms,missing_frames_before,timestamp_gap_ms\n1,10010,10,10,,,,\n2,10020,20,10,,,0,10\n4,10040,40,10,,,1,20\n');
  assert.equal(app.report().frames.length,3);
}));

test('CSV failure remains visible while the independent benchmark report finishes',async()=>realFixture(async({app,publish})=>{
  await app.start({game:'iracing'},{logFrametimes:true});await publish(10250,[sample(1,10010)]);await app.stop();
  const status=app.status();assert.equal(status.running,false);assert.equal(status.frametimeLog.state,'error');
  assert.ok(status.frametimeLog.error);assert.ok(status.frametimeLog.filePath);assert.equal(app.report().frames.length,1);
},{unwritable:true}));

test('start rejects malformed logging options instead of truthy coercion',async()=>realFixture(async({app})=>{
  await assert.rejects(app.start({game:'iracing'},{logFrametimes:'false'}),/logFrametimes|options/);
  assert.equal(app.status().running,false);
}));
