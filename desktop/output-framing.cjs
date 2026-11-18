// Only the child's inherited stdin carries preview commands. No filesystem,
// public pipe, process spawning or unbounded async work is involved in a drag.
function createOutputFraming(stream, profile, timers = globalThis) {
  let pending=null,timer=null,blocked=false,closed=false;
  const schedule=()=>{
    if(closed||blocked||timer!==null||pending===null)return;
    timer=timers.setTimeout(flush,34);timer?.unref?.();
  };
  const close=()=>{
    closed=true;pending=null;
    if(timer!==null)timers.clearTimeout(timer);
    timer=null;stream?.removeListener('drain',drain);
  };
  function flush() {
    timer=null;
    if(closed||blocked||pending===null)return;
    if(!stream||stream.destroyed||!stream.writable){close();return;}
    const line=pending;pending=null;
    try {blocked=!stream.write(line);} catch {close();}
  }
  const drain=()=>{blocked=false;schedule();};
  stream?.on('drain',drain);
  stream?.on('error',close);
  stream?.on('close',close);
  const reset=()=>{if(!closed){pending='R\n';schedule();}};
  return {
    preview(request,selectedProfileId) {
      if(closed)return false;
      const values=[request?.reserve?.left,request?.reserve?.right,request?.reserve?.top,
        request?.reserve?.bottom,request?.offsetX,request?.offsetY];
      if(request?.profileId!==profile.id||request?.profileId!==selectedProfileId||request?.game!==profile.game||
        values.some((v,i)=>typeof v!=='number'||!Number.isFinite(v)||v<(i<4?0:-100)||v>(i<4?30:100))) {
        reset();return false;
      }
      pending=`F ${values.join(' ')}\n`;schedule();return true;
    },reset,close,
  };
}
module.exports = {createOutputFraming};
