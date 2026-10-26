'use strict';
const fs=require('node:fs/promises');
const path=require('node:path');
const {atomicWrite}=require('./store.cjs');
const ACTIONS=Object.freeze({'framegen.enabled':'framegen.toggle','overlay.enabled':'osd.toggle','overlay.layout':'osd.cycle'});
const GAME_EXES={iracing:/^iracingsim64(?:dx11)?(?:\.exe)?$/i,lmu:/^(?:le mans ultimate|lemansultimate|lmu)(?:\.exe)?$/i};
const SESSION=/^[1-9]\d{0,9}-\d{1,16}-[a-zA-Z0-9-]{1,100}$/;
function alive(pid){try{process.kill(pid,0);return true;}catch{return false;}}
function createLiveControls({root,now=Date.now,isAlive=alive,delay=ms=>new Promise(resolve=>setTimeout(resolve,ms)),timeoutMs=1800,canApply=()=>true,getEditorGeneration=()=>0}) {
  let pending=false,lastRevision=0;
  async function read(file){try{if((await fs.stat(file)).size>65536)return null;return JSON.parse(await fs.readFile(file,'utf8'));}catch{return null;}}
  const valid=(s,filename,game)=>s?.protocolVersion===1 && SESSION.test(s.sessionId) && filename===`${s.sessionId}.json` &&
    Number.isSafeInteger(s.processId) && s.processId>0 && s.sessionId.startsWith(`${s.processId}-`) && isAlive(s.processId) &&
    Number.isFinite(s.updatedAtUnixMilliseconds) && now()-s.updatedAtUnixMilliseconds>=-2000 && now()-s.updatedAtUnixMilliseconds<=3000 &&
    GAME_EXES[game]?.test(s.application) && s.capabilities?.drivingControls===true;
  return {async execute(profile,actionId,editorGeneration=getEditorGeneration()) {
    const action=ACTIONS[actionId];if(!action)throw new Error('Unsupported live action.');
    if(pending)throw new Error('A live action is already pending.');
    const fg=action==='framegen.toggle';
    if(!(fg?profile.framegen?.enabled:profile.overlay?.enabled))throw new Error(`Enable ${fg?'frame generation':'the Performance HUD'} and save before starting the game.`);
    pending=true;let commandFile,revision;
    try {
      const names=await fs.readdir(path.join(root,'status')).catch(()=>[]);
      const candidates=[];
      for(const name of names.slice(0,256)) {
        if(!name.endsWith('.json')||!SESSION.test(name.slice(0,-5)))continue;
        const state=await read(path.join(root,'status',name));
        if(valid(state,name,profile.game))candidates.push({state,name});
      }
      if(candidates.length!==1)throw new Error(candidates.length?'Keep only one matching game session running.':'No supported running engine session. Start the game with the updated runtime layer.');
      const {state,name}=candidates[0];
      if(!(fg?state.capabilities.framegenToggle:state.capabilities.osd))throw new Error('This action is unavailable in the running session. Enable the feature and restart the game.');
      const ack=Number.isSafeInteger(state.acknowledgedRevision)&&state.acknowledgedRevision>=0?state.acknowledgedRevision:0;
      revision=Math.max(now(),lastRevision+1,ack+1);lastRevision=revision;
      const expiry=now()+timeoutMs;
      commandFile=path.join(root,'control',name);await fs.mkdir(path.dirname(commandFile),{recursive:true});
      const checkEditor=()=>{if(!canApply(profile.id,editorGeneration))throw new Error('Save or discard edits before using live controls.');};
      checkEditor();
      await atomicWrite(commandFile,JSON.stringify({protocolVersion:1,targetSessionId:state.sessionId,revision,expiresAtUnixMilliseconds:expiry,drivingAction:action}),checkEditor);
      while(now()<=expiry){
        await delay(30);
        const updated=await read(path.join(root,'status',name));
        if(valid(updated,name,profile.game)&&updated.acknowledgedRevision===revision)return {applied:true,action,state:updated.state};
      }
      throw new Error('The running engine did not acknowledge this action. Check the session and runtime version.');
    } finally {
      // Remove our own expired/applied request; never remove a newer request.
      if(commandFile){const request=await read(commandFile);if(request?.revision===revision)await fs.unlink(commandFile).catch(()=>{});}
      pending=false;
    }
  }};
}
module.exports={createLiveControls,isLiveAction:action=>Object.hasOwn(ACTIONS,action)};
