// One newest-value slot; IPC is fire-and-forget and never queues promises.
export function createFramingPreviewSender(send, timers = globalThis) {
  let current=null,timer=null,closed=false;
  const flush=()=>{timer=null;if(!closed&&current)send(current);};
  const heartbeat=timers.setInterval(()=>{if(!closed&&current)send(current);},500);
  return {
    update(request) {
      if(closed)return;
      current=request;
      if(!request) {
        if(timer!==null)timers.clearTimeout(timer);
        timer=null;send(null);return;
      }
      if(timer===null)timer=timers.setTimeout(flush,34);
    },
    close() {
      closed=true;current=null;
      if(timer!==null)timers.clearTimeout(timer);
      timers.clearInterval(heartbeat);send(null);
    },
  };
}
