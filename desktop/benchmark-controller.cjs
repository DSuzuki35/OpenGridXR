'use strict';
const fs=require('node:fs/promises');
const path=require('node:path');
const {atomicWrite}=require('./store.cjs');
const SESSION=/^[1-9]\d{0,9}-\d{1,16}-[a-zA-Z0-9-]{1,100}$/;
const EXES={iracing:/^iracingsim64(?:dx11)?(?:\.exe)?$/i,lmu:/^(?:le mans ultimate|lemansultimate|lmu)(?:\.exe)?$/i};
function alive(pid){try{process.kill(pid,0);return true;}catch{return false;}}
function createBenchmarkController({root,telemetry,now=Date.now,isAlive=alive,createRecorder=()=>require('./benchmark.cjs').createBenchmark(),setTimer=setInterval,clearTimer=clearInterval,onReport=async()=>{}}) {
  let recorder=createRecorder(),active=null,starting=false,startCompletion=null,timer=null,polling=null,finishing=null;
  let lastRead=-Infinity,lastLease=-Infinity,message='',closed=false;
  async function read(file,limit=65536){try{if((await fs.stat(file)).size>limit)return null;const bytes=await fs.readFile(file);if(bytes.length>limit)return null;return JSON.parse(bytes.toString('utf8'));}catch{return null;}}
  function valid(s,name,game){return s?.protocolVersion===1&&SESSION.test(s.sessionId)&&name===`${s.sessionId}.json`&&Number.isSafeInteger(s.processId)&&s.processId>0&&s.sessionId.startsWith(`${s.processId}-`)&&isAlive(s.processId)&&Number.isFinite(s.updatedAtUnixMilliseconds)&&now()-s.updatedAtUnixMilliseconds>=-2000&&now()-s.updatedAtUnixMilliseconds<=3000&&EXES[game]?.test(s.application)&&s.capabilities?.benchmark===true;}
  async function lease(){
    if(!active)return;
    const time=now();await atomicWrite(active.requestFile,JSON.stringify({protocolVersion:1,sessionId:active.sessionId,requestedAtMs:time,expiresAtMs:time+4000}));lastLease=time;
  }
  async function finish(reason){
    if(finishing)return finishing;
    const selected=active;if(!selected)return recorder.report();
    active=null;if(timer!==null){clearTimer(timer);timer=null;}
    finishing=(async()=>{
      const result=recorder.stop(reason);message=reason==='manual'?'Recording finished.':`Recording stopped: ${reason}.`;
      const request=await read(selected.requestFile);
      if(request?.sessionId===selected.sessionId)await fs.unlink(selected.requestFile).catch(()=>{});
      try{await onReport(result);}catch(error){message=`Recording finished, but automatic report save failed: ${error.message}`;}
      return result;
    })();
    try{return await finishing;}finally{finishing=null;}
  }
  async function collect(){
    if(!active)return;
    const selected=active;
    const t=await telemetry(selected.game);
    if(active!==selected)return;
    let perf=null;
    if(now()-lastRead>=250){
      lastRead=now();
      const state=await read(selected.statusFile);
      if(!valid(state,`${selected.sessionId}.json`,selected.game))return finish('session-ended');
      const candidate=await read(selected.feedFile,1024*1024);
      if(candidate?.sessionId===selected.sessionId&&Number.isFinite(candidate.timestampMs)&&now()-candidate.timestampMs>=-2000&&now()-candidate.timestampMs<=3000&&Array.isArray(candidate.frames)&&candidate.frames.length<=2048)perf=candidate;
    }
    if(active!==selected)return;
    recorder.accept(perf,t?.record??null);
    if(!recorder.status().running)return finish('recording-limit');
    if(now()-lastLease>=1000)await lease();
  }
  const api={
    async start(profile){
      if(closed)throw new Error('Benchmark controller is closed.');
      if(active||starting||finishing)throw new Error('A benchmark is already recording or finishing.');
      if(!EXES[profile?.game])throw new Error('Unsupported benchmark game.');
      starting=true;
      let completeStart;startCompletion=new Promise(resolve=>{completeStart=resolve;});
      try{
        const names=await fs.readdir(path.join(root,'status')).catch(()=>[]),matches=[];
        for(const name of names.slice(0,256)){
          if(!name.endsWith('.json')||!SESSION.test(name.slice(0,-5)))continue;
          const state=await read(path.join(root,'status',name));if(valid(state,name,profile.game))matches.push(state);
        }
        if(matches.length!==1)throw new Error(matches.length?'Keep only one matching game session running.':'No supported running engine session. Enable the Performance HUD, save, update the runtime layer and restart the sim.');
        if(closed)throw new Error('Benchmark controller is closed.');
        const state=matches[0],id=state.sessionId;
        recorder=createRecorder();recorder.start({game:profile.game,profileName:profile.name??'',metadata:{profileId:profile.id??'',sessionId:id,mode:profile.mode??'',renderScale:profile.view?.renderScale??100,crop:JSON.stringify(profile.view?.crop??{}),framegen:JSON.stringify(profile.framegen??{}),helmetStyle:profile.helmet?.enabled?profile.helmet.style:'off'}});
        active={game:profile.game,profileId:profile.id,sessionId:id,statusFile:path.join(root,'status',`${id}.json`),feedFile:path.join(root,'status',`${id}_benchmark.json`),requestFile:path.join(root,'control',`${id}_benchmark-request.json`)};
        lastRead=lastLease=-Infinity;message='Recording individual frame timings and lap telemetry.';
        await lease();await api.tick();
        if(active){timer=setTimer(()=>{api.tick().catch(()=>{});},50);timer?.unref?.();}
        return api.status();
      }catch(error){if(active)await finish('start-failed');throw error;}finally{starting=false;completeStart();startCompletion=null;}
    },
    tick(){if(polling)return polling;if(!active)return Promise.resolve();polling=collect().catch(async error=>{await finish('source-error');message=`Recording stopped: ${error.message}`;}).finally(()=>{polling=null;});return polling;},
    async stop(){if(polling)await polling;return finish('manual');},
    status(){return {...recorder.status(),running:Boolean(active),game:active?.game??null,sessionId:active?.sessionId??null,message};},
    report(options){return recorder.report(options);},
    async close(){closed=true;if(startCompletion)await startCompletion;if(polling)await polling;return finish('app-closed');},
  };
  return api;
}
module.exports={createBenchmarkController};
