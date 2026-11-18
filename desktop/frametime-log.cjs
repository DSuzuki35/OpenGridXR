'use strict';
const defaultFs=require('node:fs/promises');
const path=require('node:path');
const {randomUUID}=require('node:crypto');

const HEADER='sequence,timestamp_ms,elapsed_ms,frame_interval_ms,cpu_ms,gpu_ms,missing_frames_before,timestamp_gap_ms\n';
const positive=(value,fallback)=>Number.isSafeInteger(value)&&value>0?value:fallback;

// The benchmark collector owns validation and deduplication. append() only
// queues its accepted numeric samples; disk operations run asynchronously.
function createFrametimeLog({directory,game,startedAtMs,fs=defaultFs,batchSize=256,maxQueuedRows=8192,flushIntervalMs=250,setTimer=setInterval,clearTimer=clearInterval}){
  if(typeof directory!=='string'||!directory)throw new Error('A frametime log directory is required.');
  batchSize=positive(batchSize,256);maxQueuedRows=positive(maxQueuedRows,8192);
  batchSize=Math.min(batchSize,maxQueuedRows);
  const stamp=new Date(startedAtMs).toISOString().replace(/[:.]/g,'-');
  const filePath=path.join(directory,`OpenGridXR-${game==='lmu'?'lmu':'iracing'}-${stamp}-${randomUUID()}-frametimes.csv`);
  let handle=null,queue=[],pending=0,writtenRows=0,droppedRows=0,error=null,writeFailed=false;
  let lastSequence=null,lastTimestamp=null,draining=null,closing=false,closed=false,closePromise=null;
  function fail(cause){
    error=error??`Frametime CSV is incomplete: ${cause.message??cause}`;
    writeFailed=true;droppedRows+=pending;pending=0;queue=[];
  }
  const ready=(async()=>{
    try{await fs.mkdir(directory,{recursive:true});handle=await fs.open(filePath,'wx');await handle.writeFile(HEADER);}
    catch(cause){fail(cause);}
  })();
  function flush(){
    if(draining)return draining;
    draining=(async()=>{
      await ready;
      while(queue.length&&!writeFailed){
        const batch=queue.splice(0,batchSize);
        try{await handle.writeFile(batch.join(''));writtenRows+=batch.length;pending-=batch.length;}
        catch(cause){fail(cause);}
      }
    })().finally(()=>{draining=null;});
    return draining;
  }
  const timer=setTimer(()=>flush(),positive(flushIntervalMs,250));timer?.unref?.();
  return {
    append(frame){
      if(error||closing){droppedRows++;return false;}
      if(pending>=maxQueuedRows){
        error=`Frametime CSV is incomplete: write queue reached ${maxQueuedRows} rows (disk backpressure).`;
        droppedRows++;return false;
      }
      const missing=lastSequence===null?'':Math.max(0,frame.sequence-lastSequence-1);
      const gap=lastTimestamp===null?'':frame.timestampMs-lastTimestamp;
      queue.push([frame.sequence,frame.timestampMs,frame.timestampMs-startedAtMs,frame.frameMs,frame.cpuMs??'',frame.gpuMs??'',missing,gap].join(',')+'\n');
      lastSequence=frame.sequence;lastTimestamp=frame.timestampMs;pending++;
      if(queue.length>=batchSize)void flush();
      return true;
    },
    flush,
    close(){
      if(closePromise)return closePromise;
      closing=true;clearTimer(timer);
      closePromise=(async()=>{
        // A previous drain can be settling when close begins. Recheck after
        // awaiting it so a tail queued before closing cannot be left behind.
        do{await flush();}while(queue.length&&!writeFailed);
        try{await handle?.close();}catch(cause){fail(cause);}
        closed=true;
      })();
      return closePromise;
    },
    status(){return {enabled:true,state:error?'error':closed?'complete':closing?'flushing':handle?'recording':'opening',filePath,error,queuedRows:pending,writtenRows,droppedRows};},
  };
}
module.exports={createFrametimeLog};
