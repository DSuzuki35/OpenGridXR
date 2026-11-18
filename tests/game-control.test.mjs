import test from 'node:test';
import assert from 'node:assert/strict';
import {EventEmitter} from 'node:events';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url);
const {createGameControl}=require('../desktop/game-control.cjs');

function fixture() {
  const calls=[];
  const control=createGameControl({executable:'control-fixture.exe',spawnProcess:(...args)=>{
    const child=new EventEmitter();child.stdout=new EventEmitter();child.stderr=new EventEmitter();
    child.kill=()=>{child.killed=true;};calls.push({args,child});return child;
  }});
  return {control,calls};
}
const settings={gameShortcut:'Ctrl+Shift+F4',delaySeconds:2};

test('game control forwards configured key and delay and rejects concurrent requests',async()=>{
  const f=fixture();
  const pending=f.control.center('lmu',settings);
  assert.deepEqual(f.calls[0]?.args[1],['--game','lmu','--key','Ctrl+Shift+F4','--delay-ms','2000']);
  await assert.rejects(f.control.center('lmu',settings),/pending/i);
  f.calls[0].child.emit('exit',0);await pending;
});

test('editing during a controller centering delay cancels the native command',async()=>{
  const f=fixture();let clean=true;
  const pending=f.control.center('lmu',settings,{canApply:()=>clean});
  clean=false;f.control.cancelIfInvalid();
  await assert.rejects(pending,/save|discard|cancel/i);
  assert.equal(f.calls[0].child.killed,true);
  await assert.rejects(f.control.center('lmu',settings,{canApply:()=>false}),/save|discard|cancel/i);
  assert.equal(f.calls.length,1);
});

test('cancelled child exit cannot clear or complete a newer centering command',async()=>{
  const f=fixture();let clean=true;
  const first=f.control.center('lmu',settings,{canApply:()=>clean});
  clean=false;f.control.cancelIfInvalid();await assert.rejects(first);
  const second=f.control.center('lmu',settings);
  f.calls[0].child.emit('exit',1);
  await assert.rejects(f.control.center('lmu',settings),/pending/i);
  f.calls[1].child.emit('exit',0);await second;
});
test('hiding or losing focus of the menu cancels a delayed centering command without editor IPC',async()=>{
  const f=fixture();let sourceActive=true;
  const pending=f.control.center('lmu',settings,{canApply:()=>sourceActive});
  sourceActive=false;
  await assert.rejects(pending,/cancel|source/i);assert.equal(f.calls[0].child.killed,true);
});
