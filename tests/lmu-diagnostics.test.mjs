import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import runtime from '../desktop/runtime.cjs';

const fixture = (patch={}) => ({version:1,pid:process.pid,timestampMs:Date.now(),state:'unavailable',reason:'Engine hook data reference mismatch',cpuAffinity:'skipped: unsupported topology',reentryCount:2,outerTimeoutCount:1,...patch});

test('failed LMU startup remains visible without an active session',async()=>{
  const directory=await fs.mkdtemp(path.join(os.tmpdir(),'opengrid-diagnostics-'));
  try {
    const native=runtime.createRuntime({sourceDir:directory,runtimeDir:directory,testMode:true});
    await fs.writeFile(path.join(directory,'OpenGridXR-lmu-diagnostics.json'),JSON.stringify(fixture()));
    assert.equal((await native.sessionsStatus()).some(s=>s.active),false);
    const diagnostic=await native.lmuDiagnosticsStatus();
    assert.equal(diagnostic.state,'unavailable');
    assert.equal(diagnostic.reason,'Engine hook data reference mismatch');
    assert.equal(diagnostic.cpuAffinity,'skipped: unsupported topology');
    assert.equal(diagnostic.reentryCount,2);
    await fs.writeFile(path.join(directory,'OpenGridXR-lmu-diagnostics.json'),'x'.repeat(8193));
    assert.equal(await native.lmuDiagnosticsStatus(),null);
    await fs.writeFile(path.join(directory,'OpenGridXR-lmu-diagnostics.json'),'{broken');
    assert.equal(await native.lmuDiagnosticsStatus(),null);
    const invalidUtf8=Buffer.from(JSON.stringify(fixture({reason:'invalid-slot'})));
    invalidUtf8[invalidUtf8.indexOf('invalid-slot')]=0xff;
    await fs.writeFile(path.join(directory,'OpenGridXR-lmu-diagnostics.json'),invalidUtf8);
    assert.equal(await native.lmuDiagnosticsStatus(),null);
  } finally {await fs.rm(directory,{recursive:true,force:true});}
});

test('diagnostics reject stale, future, malformed and unbounded payloads; only failures survive process exit',()=>{
  const now=Date.now(),valid=fixture({timestampMs:now});
  const parse=payload=>runtime.lmuDiagnosticState(payload,now,()=>true);
  assert.equal(parse(valid).state,'unavailable');
  for(const patch of [{version:2},{pid:0},{pid:1.2},{timestampMs:now-86400001},{timestampMs:now+2001},{state:'ready-ish'},
    {reason:'a'.repeat(513)},{reason:'bad\nreason'},{cpuAffinity:[]},{reentryCount:-1},{outerTimeoutCount:Infinity}])
    assert.equal(parse({...valid,...patch}),null,JSON.stringify(patch));
  const previous=runtime.lmuDiagnosticState(valid,now,()=>false);
  assert.equal(previous.state,'unavailable');
  assert.equal(previous.activeProcess,false);
  assert.equal(runtime.lmuDiagnosticState({...valid,state:'ready'},now,()=>false),null);
  assert.equal(runtime.lmuDiagnosticState({...valid,state:'starting'},now,()=>false),null);
  assert.equal(parse(null),null);
});

test('native outer fallback remains a bounded nonfatal warning on ready hooks, with older snapshots supported',()=>{
  const now=Date.now(),parse=payload=>runtime.lmuDiagnosticState(payload,now,()=>true);
  const legacy=parse(fixture({state:'ready',reason:'Engine hooks verified'}));
  assert.equal(legacy.warning,'');
  assert.equal(legacy.nativeOuterRefusalCount,0);
  const fallback=parse(fixture({state:'ready',reason:'Engine hooks verified',warning:'Temporary full-resolution fallback: native outer data unavailable',nativeOuterRefusalCount:4}));
  assert.equal(fallback.state,'ready');
  assert.equal(fallback.reason,'Engine hooks verified');
  assert.equal(fallback.warning,'Temporary full-resolution fallback: native outer data unavailable');
  assert.equal(fallback.nativeOuterRefusalCount,4);
  for(const patch of [{warning:null},{warning:'a'.repeat(513)},{warning:'bad\nwarning'},{nativeOuterRefusalCount:-1},{nativeOuterRefusalCount:1.1},{nativeOuterRefusalCount:Infinity}])
    assert.equal(parse(fixture(patch)),null,JSON.stringify(patch));
});
