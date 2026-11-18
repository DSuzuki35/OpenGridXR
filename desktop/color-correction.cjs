'use strict';
const {execFile}=require('node:child_process');
const {promisify}=require('node:util');
const PRESETS=new Set(['neutral','sun','night','custom']);
const unavailable=error=>({available:false,supported:false,enabled:false,activePreset:null,effectActive:false,error});
function parse(text) {
  if(typeof text!=='string'||Buffer.byteLength(text)>16384)throw Error('Invalid native color status.');
  const state=JSON.parse(text);
  if(!state||['available','supported','enabled','effectActive'].some(key=>typeof state[key]!=='boolean')||
      !(state.activePreset===null||PRESETS.has(state.activePreset))||typeof state.error!=='string')throw Error('Invalid native color status.');
  return {available:state.available,supported:state.supported,enabled:state.enabled,activePreset:state.activePreset,
    effectActive:state.available&&state.supported&&state.enabled&&state.effectActive,
    error:state.error.replace(/[\x00-\x1f\x7f]/g,' ').slice(0,500)};
}
function createColorCorrection({executable,run,testMode=false}={}) {
  const invoke=run||(args=>promisify(execFile)(executable,args,{windowsHide:true,timeout:3000,maxBuffer:16384}));
  let closed=false,pending=false;
  const validateGame=game=>{if(!['lmu','iracing'].includes(game))throw Error('Unsupported color-correction game.');};
  async function request(game,cycle) {
    try{return parse((await invoke([cycle?'--color-cycle':'--color-status','--game',game])).stdout);}
    catch(error){
      let reply;try{reply=parse(error.stdout);}catch{}
      if(cycle)throw Error(reply?.error||'Native color controls are unavailable. Update the runtime connection and start a supported VR session.');
      return reply||unavailable('Native color controls are unavailable. Update the runtime connection and start a supported VR session.');
    }
  }
  return {
    async status(game) {
      validateGame(game);
      if(closed||testMode)return unavailable(testMode?'Color correction requires a supported VR session.':'Color controls are closed.');
      const result=await request(game,false);
      return closed?unavailable('Color controls are closed.'):result;
    },
    async cycle(game,{canApply=()=>true}={}) {
      validateGame(game);
      if(closed||testMode)throw Error('Color preset switching is unavailable.');
      if(pending)throw Error('A color preset change is already pending.');
      if(!canApply())throw Error('Save or discard editor changes before switching presets.');
      pending=true;
      try {
        const result=await request(game,true);
        if(!result.available||!result.supported||!result.enabled||result.error)throw Error(result.error||'Color correction is not active in this VR session.');
        return result;
      }finally{pending=false;}
    },
    close(){closed=true;},
  };
}
module.exports={createColorCorrection};
