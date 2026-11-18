import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import {runInNewContext} from 'node:vm';

// Exercise the component's asynchronous polling effect with controllable IPC.
const source=await fs.readFile(new URL('../src/components/BenchmarkPanel.jsx',import.meta.url),'utf8');
const effect=source.slice(source.indexOf('  useEffect(()=>{')+'  useEffect(()=>{'.length,source.indexOf('  },[]);'));
test('finished report polling retries failures, then stops fetching the same report',async()=>{
  let scheduled,attempts=0;const reports=[];
  const cleanup=runInNewContext(`(()=>{${effect}})()`,{
    api:{benchmarkStatus:async()=>({state:'complete',startedAtMs:123,frameCount:5}),benchmarkReport:async()=>{if(++attempts===1)throw new Error('temporary');return {id:123};}},
    setStatus(){},setError(){},setReport:value=>reports.push(value),
    setTimeout:callback=>{scheduled=callback;return 1;},clearTimeout(){}
  });
  const settle=()=>new Promise(resolve=>setImmediate(resolve));
  await settle();await scheduled();await settle();await scheduled();await settle();cleanup();
  assert.equal(attempts,2);assert.equal(reports.length,1);
});
test('an unmounted panel ignores a report that finishes after disposal',async()=>{
  let finish,updates=0;
  const cleanup=runInNewContext(`(()=>{${effect}})()`,{
    api:{benchmarkStatus:async()=>({state:'complete',startedAtMs:123}),benchmarkReport:()=>new Promise(resolve=>{finish=resolve;})},
    setStatus(){},setError(){},setReport(){updates++;},setTimeout(){},clearTimeout(){}
  });
  await new Promise(resolve=>setImmediate(resolve));cleanup();finish({id:123});
  await new Promise(resolve=>setImmediate(resolve));assert.equal(updates,0);
});
