import {test} from 'node:test';
import assert from 'node:assert/strict';
import {EventEmitter} from 'node:events';
import {createRequire} from 'node:module';
import {createFramingPreviewSender} from '../src/output-framing.mjs';
const {createOutputFraming}=createRequire(import.meta.url)('../desktop/output-framing.cjs');
function fixture() {
  const stream=new EventEmitter(), writes=[], tasks=new Map();let clock=0,id=0;
  stream.destroyed=false;stream.writable=true;
  stream.write=line=>{writes.push(line);return !stream.blocked;};
  const controller=createOutputFraming(stream,{id:'p1',game:'lmu'}, {
    setTimeout:(fn,ms)=>{tasks.set(++id,{fn,at:clock+ms});return id;},
    clearTimeout:id=>tasks.delete(id),now:()=>clock,
  });
  const tick=ms=>{clock+=ms;for(const [id,t] of [...tasks])if(t.at<=clock){tasks.delete(id);t.fn();}};
  const request=(value=13)=>({profileId:'p1',game:'lmu',reserve:{left:value,right:value,top:value,bottom:value},offsetX:31,offsetY:6});
  return {stream,writes,controller,tick,request};
}
test('unsaved framing reaches the owned output pipe; rapid drags coalesce to latest value',()=>{
  const f=fixture();for(let i=0;i<=30;i++)assert.equal(f.controller.preview(f.request(i),'p1'),true);
  assert.equal(f.writes.length,0);f.tick(34);
  assert.deepEqual(f.writes,['F 30 30 30 30 31 6\n']);
  f.controller.close();
});
test('pipe backpressure retains one latest update, reset supersedes queued preview',()=>{
  const f=fixture();f.stream.blocked=true;f.controller.preview(f.request(),'p1');f.tick(34);
  for(let i=0;i<30;i++)f.controller.preview(f.request(i),'p1');f.tick(100);
  assert.equal(f.writes.length,1);
  f.controller.reset();f.stream.blocked=false;f.stream.emit('drain');f.tick(34);
  assert.deepEqual(f.writes,['F 13 13 13 13 31 6\n','R\n']);f.controller.close();
});
test('wrong profile/game, invalid numbers and editor selection cannot preview another output',()=>{
  const f=fixture();
  for(const request of [{...f.request(),profileId:'other'},{...f.request(),game:'iracing'},
    {...f.request(),offsetX:NaN},f.request(31),{...f.request(),offsetY:'6'}]) {
    assert.equal(f.controller.preview(request,'p1'),false);
  }
  assert.equal(f.controller.preview(f.request(),'other'),false);f.tick(34);
  assert.equal(f.writes.some(x=>x.startsWith('F ')),false);f.controller.close();
});
test('pipe error stops further sends without throwing into the desktop app',()=>{
  const f=fixture();f.controller.preview(f.request(),'p1');f.stream.emit('error',new Error('closed'));
  f.tick(34);assert.equal(f.writes.length,0);assert.equal(f.controller.preview(f.request(),'p1'),false);
});
test('renderer coalesces drag events, renews idle preview and cancels queued work on discard',()=>{
  let scheduled,heartbeat;const sent=[];
  const sender=createFramingPreviewSender(value=>sent.push(value),{
    setTimeout:fn=>{scheduled=fn;return 1;},clearTimeout:()=>{scheduled=null;},
    setInterval:fn=>{heartbeat=fn;return 2;},clearInterval:()=>{heartbeat=null;},
  });
  for(let offsetX=0;offsetX<100;offsetX++)sender.update({offsetX});
  assert.equal(sent.length,0);scheduled();assert.deepEqual(sent,[{offsetX:99}]);
  heartbeat();assert.deepEqual(sent.at(-1),{offsetX:99});
  sender.update({offsetX:50});sender.update(null);
  assert.equal(scheduled,null);assert.equal(sent.at(-1),null);
  const count=sent.length;heartbeat();assert.equal(sent.length,count);
  sender.close();assert.equal(heartbeat,null);assert.equal(sent.at(-1),null);
});
