import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import history from '../desktop/benchmark-comparison.cjs';
import benchmark from '../desktop/benchmark.cjs';

test('invalid recording selections are refused before worker data is cloned',async()=>{
  const id='benchmark-2026-11-10T10-00-00-100Z.json';
  for(const [first,second] of [[()=>{},id],[id,()=>{}],[id,'x'.repeat(10000)]]){
    await assert.rejects(history.compareInWorker('',first,second),/Invalid recording selection/);
  }
});

test('compares measured summaries from two saved recordings, including older reports without renderer stamps',async t=>{
  const root=await fs.mkdtemp(path.join(os.tmpdir(),'og-comparison-'));t.after(()=>fs.rm(root,{recursive:true,force:true}));
  const names=['benchmark-2026-11-10T10-00-00-100Z.json','benchmark-2026-11-10T10-01-00-100Z.json'];
  for(const [index,id] of names.entries()){
    let clock=1000;
    const recorder=benchmark.createBenchmark({now:()=>clock});
    recorder.start({game:'lmu',metadata:index?{lmuRenderingMode:'quad',lmuQuadProvider:'qvfr',nvidiaMvrRequested:true}:{}});
    clock+=100;
    recorder.accept({timestampMs:clock,sessionId:'example',frames:[{sequence:1,timestampMs:clock,frameMs:index?8:10,cpuMs:3}]});
    clock+=index?20000:2000;
    await fs.writeFile(path.join(root,id),JSON.stringify(recorder.stop()));
  }
  const list=await history.listRecordings(root);
  assert.deepEqual(list.map(item=>item.id),[names[1],names[0]]);
  assert.equal(list[0].startedAt,'2026-11-10T10:01:00.100Z');
  const result=await history.compareInWorker(root,...names);
  assert.equal(result.rows.find(row=>row.label==='Application FPS').delta,25);
  assert.equal(result.rows.find(row=>row.label==='P99 frame interval').delta,-20);
  assert.equal(result.rows.find(row=>row.label==='Mean GPU time').delta,null);
  assert.match(result.a.settings,/unrecorded/);assert.match(result.b.settings,/QVFR.*MVR requested/);
  assert.ok(result.warnings.some(text=>text.includes('1,000')));assert.ok(result.warnings.some(text=>text.includes('lengths')));
  await assert.rejects(history.compareInWorker(root,names[0],names[0]),/different/);
  await assert.rejects(history.compareInWorker(root,'../profiles.json',names[0]),/selection/);
});
