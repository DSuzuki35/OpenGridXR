import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import controller from '../desktop/benchmark-controller.cjs';

async function fixture(fn) {
  const root=await fs.mkdtemp(path.join(os.tmpdir(),'opengrid-benchmark-'));
  const sessionId='123-100-fixture';let clock=10000,recording=false;const accepted=[];
  const recorder={start(){recording=true;return this.status();},accept(p,t){accepted.push([p,t]);},stop(reason){recording=false;return {reason};},status(){return {running:recording};},report(){return {ok:true};}};
  await fs.mkdir(path.join(root,'status'),{recursive:true});
  await fs.writeFile(path.join(root,'status',`${sessionId}.json`),JSON.stringify({protocolVersion:1,sessionId,processId:123,application:'iRacingSim64DX11.exe',updatedAtUnixMilliseconds:clock,capabilities:{benchmark:true}}));
  const app=controller.createBenchmarkController({root,now:()=>clock,isAlive:()=>true,createRecorder:()=>recorder,telemetry:async()=>({record:{game:'iracing',timestampMs:clock}}),setTimer:()=>0,clearTimer:()=>{}});
  try{await fn({app,root,sessionId,accepted,setClock:v=>clock=v});}finally{await app.close();await fs.rm(root,{recursive:true,force:true});}
}
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
