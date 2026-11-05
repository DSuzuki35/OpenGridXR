'use strict';
// This gateway accepts actions only after one fresh, source-bound native click.
// It is never installed in remote overlays or the editor's privileged preload.
const IDENTITY=['session','focusEpoch','configRevision','sourceRevision','sourceGeneration','recenter'];
const decimal=value=>typeof value==='string'&&/^\d{1,20}$/.test(value);
function validUiPointer(event){
  return event?.op==='ui-pointer'&&Number.isInteger(event.slot)&&event.slot>=0&&event.slot<15&&
    ['move','leave','click'].includes(event.event)&&typeof event.uiToken==='string'&&/^[0-9a-f-]{36}$/i.test(event.uiToken)&&
    [...IDENTITY,'tickMs','sequence'].every(key=>decimal(event[key]))&&
    typeof event.u==='number'&&Number.isFinite(event.u)&&event.u>=0&&event.u<=1&&
    typeof event.v==='number'&&Number.isFinite(event.v)&&event.v>=0&&event.v<=1;
}
// Ping-pong buffer generation changes on ordinary paints. Native already validates
// it at click emission; accepted actions follow the source lifecycle/focus instead.
const contextOf=event=>IDENTITY.filter(key=>key!=='sourceGeneration').map(key=>event[key]).join(':');
function createQuickMenuGateway({now=Date.now,nativeNow=()=>require('node:os').uptime()*1000,setTimer=setInterval,clearTimer=clearInterval}={}){
  const sources=new Map();let timer=null,closed=false;
  function active(source){return !closed&&!source.closed&&!source.contents.isDestroyed()&&source.isCurrent();}
  function state(source){const value=source.getState();return {...value,busy:source.busy===true||value.busy===true,message:source.message||value.message||'',pointer:source.pointerState||{valid:false}};}
  function publish(source){if(active(source))source.contents.send('opengrid:quickMenuState',state(source));}
  function remove(source){
    source.closed=true;source.permit=null;source.context=null;sources.delete(source.contents);
    if(!sources.size&&timer!==null){clearTimer(timer);timer=null;}
  }
  return {
    register(options){
      if(closed||sources.has(options.contents))throw Error('Menu source already registered or closed.');
      const source={...options,closed:false,lastSequence:0n,context:null,permit:null,busy:false,message:'',pointerState:{valid:false}};
      sources.set(source.contents,source);
      source.contents.once('did-finish-load',()=>publish(source));
      if(timer===null){timer=setTimer(()=>{for(const record of sources.values())publish(record);},1000);timer?.unref?.();}
      return {
        pointer(event){
          if(!active(source)||!validUiPointer(event)||event.uiToken!==source.token||BigInt(event.sequence)<=source.lastSequence)return false;
          source.lastSequence=BigInt(event.sequence);
          const tick=Number(event.tickMs),clock=nativeNow();
          if(event.event!=='leave'&&(!Number.isSafeInteger(tick)||tick>clock+5||clock-tick>200))return false;
          const context=contextOf(event);
          if(source.context!==context){source.permit=null;source.context=context;}
          if(event.event==='leave'){
            source.permit=null;source.context=null;source.target=null;source.pointerState={valid:false};
            source.contents.sendInputEvent({type:'mouseLeave',x:0,y:0});publish(source);return true;
          }
          const x=Math.round(event.u*(source.width-1)),y=Math.round(event.v*(source.height-1));
          source.target={session:event.session,focusEpoch:event.focusEpoch,configRevision:event.configRevision,sourceRevision:event.sourceRevision,recenter:event.recenter};
          source.pointerState={valid:true,u:event.u,v:event.v};
          source.contents.sendInputEvent({type:'mouseMove',x,y});
          if(event.event==='click'&&!source.busy){
            source.permit={context,at:now()};
            source.contents.sendInputEvent({type:'mouseDown',x,y,button:'left',clickCount:1});
            source.contents.sendInputEvent({type:'mouseUp',x,y,button:'left',clickCount:1});
          }
          publish(source);return true;
        },
        selectionContext:()=>active(source)&&source.context?source.target:null,
        refresh:()=>publish(source),close:()=>remove(source),
      };
    },
    async run(event,actionId){
      const source=sources.get(event.sender);
      if(!source||event.senderFrame!==source.contents.mainFrame||!active(source))throw Error('Untrusted menu frame.');
      const permit=source.permit;
      const current=()=>active(source)&&source.context===permit?.context;
      if(!permit||!current()||now()<permit.at||now()-permit.at>200)throw Error('Select the button with a fresh headset menu click.');
      source.permit=null;
      const menuState=state(source),action=menuState.actions?.find(item=>item.id===actionId);
      if(typeof actionId!=='string'||actionId.length>160||!action||action.disabled||menuState.busy)throw Error('This menu action is unavailable.');
      source.busy=true;source.message='Applying…';publish(source);
      try{
        const result=await source.runAction(actionId,{canApply:current});
        source.message=result?.message||'Action applied.';return {message:source.message};
      }catch(error){source.message=String(error.message||error).slice(0,500);throw error;}
      finally{source.busy=false;publish(source);}
    },
    refresh(){for(const source of sources.values())publish(source);},
    close(){closed=true;for(const source of [...sources.values()])remove(source);},
  };
}
function menuActions(profile,modules={},runtime){
  const actions=[];
  const add=(id,label,available,reason)=>actions.push({id,label,disabled:!available,reason:available?'':reason});
  const live=runtime===undefined||runtime.live?.available===true;
  const colorLive=runtime===undefined||(runtime.color?.available===true&&runtime.color.supported===true&&runtime.color.enabled===true);
  add('colorCorrection.cycle','Next color preset',modules.colorCorrection!==false&&profile.colorCorrection?.enabled&&colorLive,'Unavailable in this XR session. Enable VR color correction, save and restart.');
  add('overlay.enabled','Toggle performance HUD',modules.overlay!==false&&profile.overlay?.enabled&&live&&(runtime===undefined||runtime.live.capabilities?.osd===true),'HUD unavailable in this session. Enable it, save and restart.');
  add('overlay.layout','Change HUD layout',modules.overlay!==false&&profile.overlay?.enabled&&live&&(runtime===undefined||runtime.live.capabilities?.osd===true),'HUD unavailable in this session. Enable it, save and restart.');
  add('framegen.enabled','Toggle frame generation',modules.framegen!==false&&profile.framegen?.enabled&&live&&(runtime===undefined||runtime.live.capabilities?.framegenToggle===true),'Frame generation unavailable in this session. Enable it, save and restart.');
  for(const item of profile.spatialOverlays?.items||[])if(item.kind!=='menu')add(`spatialOverlays.${item.id}.enabled`,`${item.enabled?'Hide':'Show'} ${item.name}`,modules.spatial!==false&&profile.spatialOverlays.enabled,'Spatial overlays are disabled.');
  add('centering.game','Center VR in game',modules.bindings!==false&&!!profile.centering?.gameShortcut,'Set the game centering shortcut first.');
  add('recenter','Recenter overlays',modules.spatial!==false,'Spatial overlays are disabled.');
  add('close','Close menu',true,'');
  return actions;
}
module.exports={createQuickMenuGateway,validUiPointer,menuActions};
