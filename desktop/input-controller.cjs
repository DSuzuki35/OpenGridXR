"use strict";

const DEVICE = /^dinput:\{[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\}$/;
function validInput(value) {
  return value && DEVICE.test(value.deviceId) && Number.isInteger(value.index) && value.index >= 0 &&
    (value.kind === "button" ? value.index < 128 && value.direction === undefined : value.kind === "pov" && value.index < 4 && Number.isInteger(value.direction) && value.direction >= 0 && value.direction < 8);
}
const matches = (a,b) => a.deviceId === b.deviceId && a.kind === b.kind && a.index === b.index && (a.kind !== "pov" || a.direction === b.direction);
// Spatial hosts can outlive a bindings service during module/profile changes.
let nextPressId=0;

function createInputController({createHost,listActions,applyAction,applyHoldAction=()=>{},testMode=false,testFixture=false,onError=()=>{}}) {
  let document, closed=false, applying=false, applyingClick=null, error="", timer=null;
  let editor={dirty:true,busy:true,profileId:""};
  let capture={state:"idle"};
  const holds=new Map(), blocked=new Set();
  const key=input=>`${input.deviceId}:${input.kind}:${input.index}:${input.direction??''}`;
  const host=createHost({onPress:press,onRelease:release,onHeld:held,onStatus:status=>{
    for(const entry of holds.values()) if(!status.available || !status.devices.some(d=>d.id===entry.input.deviceId)) cancel(entry);
  }});
  const selected=()=>document?.profiles.find(p=>p.id===document.selectedProfileId);
  const bindings=()=>selected()?.inputBindings;
  function allowedAction(action) {
    if(action.disabled) return false;
    const precision=/^spatialOverlays\.([0-9a-f-]+)\.precision$/i.exec(action.id);
    if(!precision) return true;
    const profile=selected(),item=profile?.spatialOverlays?.items?.find(item=>item.id===precision[1]);
    return profile?.spatialOverlays?.enabled===true && item?.enabled===true && item.kind==='desktop' && item.interaction==='head';
  }
  function synchronize() {
    const settings=bindings();
    const allowed=new Set(selected() ? listActions(selected()).filter(allowedAction).map(action=>action.id) : []);
    const hasBindings=settings?.enabled !== false && settings?.bindings?.some(binding=>allowed.has(binding.actionId));
    for(const entry of holds.values()) if(paused(entry) || selected()?.id!==entry.profileId || settings?.enabled===false || !allowed.has(entry.actionId) || !settings?.bindings?.some(b=>b.actionId===entry.actionId && matches(b,entry.input))) cancel(entry);
    host.setActive(!closed && (!testMode || testFixture) && (capture.state === "listening" || Boolean(hasBindings)));
  }
  function paused(hold) {
    const clickingHeldDesktop=hold && applyingClick?.profileId===hold.profileId && applyingClick?.precisionActionId===hold.actionId;
    return closed || (applying && !clickingHeldDesktop) || editor.dirty || editor.busy || editor.profileId !== document?.selectedProfileId || !["idle","cancelled"].includes(capture.state);
  }
  function dispatchHold(entry,value) {
    try {
      const result=applyHoldAction(entry.profileId,entry.actionId,value,entry.pressId);
      if(value && result && typeof result.then==='function') {
        entry.pending=true;
        Promise.resolve(result).catch(e=>{if(entry.active){error=e.message;onError(error);cancel(entry);}}).finally(()=>{
          entry.pending=false;
          // A delayed adapter must never leave a cancelled press held.
          if(!entry.active) dispatchHold(entry,false);
        });
      } else if(!value && result && typeof result.then==='function') Promise.resolve(result).catch(()=>{});
    } catch(e) {if(value){error=e.message;onError(error);cancel(entry);}}
  }
  function cancel(entry) {
    if(!entry.active) return;
    entry.active=false;clearTimeout(entry.timer);holds.delete(key(entry.input));dispatchHold(entry,false);
  }
  function cancelAll(){for(const entry of holds.values()) cancel(entry);}
  function renew(entry) {
    const now=Date.now();
    // Timers may not have run yet after sleep or a blocked event loop.
    if(entry.lastReportAt!==undefined && (now-entry.lastReportAt>=300 || now<entry.lastReportAt)) {cancel(entry);return;}
    entry.lastReportAt=now;
    clearTimeout(entry.timer);
    entry.timer=setTimeout(()=>cancel(entry),300);entry.timer.unref?.();
    if(!entry.pending) dispatchHold(entry,true);
  }
  function release(input) {
    if(!validInput(input)) return;
    const entry=holds.get(key(input));if(entry) cancel(entry);blocked.delete(key(input));
  }
  function held(report) {
    if(closed || !report || !Array.isArray(report.inputs)) return;
    const inputs=report.inputs.map(input=>({...input,deviceId:report.deviceId}));
    for(const entry of holds.values()) if(entry.input.deviceId===report.deviceId) {
      if(!inputs.some(input=>matches(entry.input,input))) release(entry.input);
      else if(paused(entry) || bindings()?.enabled===false || selected()?.id!==entry.profileId || !listActions(selected()).some(action=>action.id===entry.actionId && allowedAction(action))) cancel(entry);
      else renew(entry);
    }
    for(const physical of blocked) if(physical.startsWith(`${report.deviceId}:`) && !inputs.some(input=>key(input)===physical)) blocked.delete(physical);
  }
  function press(input,metadata={fresh:true}) {
    if (closed || !validInput(input)) return;
    const settings=bindings();
    const binding=settings?.bindings?.find(b=>matches(b,input));
    const action=binding && listActions(selected()).find(a=>a.id===binding.actionId);
    const physical=key(input),wasBlocked=blocked.has(physical);
    if(action?.hold) blocked.add(physical);
    if (capture.state === "listening") {
      clearTimeout(timer); timer=null;
      const deviceName=(host.state().devices.find(d=>d.id===input.deviceId)?.name || "Joystick").slice(0,160);
      capture={...capture,state:"captured",input:{deviceId:input.deviceId,deviceName,kind:input.kind,index:input.index,...(input.kind==="pov"?{direction:input.direction}:{})}};
      synchronize(); return;
    }
    if (paused()) return;
    if (!settings || settings.enabled === false) return;
    if (!binding || !action || !allowedAction(action)) return;
    const profileId=selected().id;
    if(action.hold) {
      if(wasBlocked || !metadata.fresh || nextPressId>=Number.MAX_SAFE_INTEGER) return;
      const entry={input:{...input},profileId,actionId:binding.actionId,pressId:++nextPressId,active:true,pending:false,timer:null};
      holds.set(physical,entry);renew(entry);return;
    }
    const click=/^spatialOverlays\.[0-9a-f-]+\.(leftClick|rightClick)$/i.test(binding.actionId);
    applyingClick=click?{profileId,precisionActionId:binding.actionId.replace(/\.(leftClick|rightClick)$/i,'.precision')}:null;
    for(const entry of holds.values()) if(entry.profileId!==applyingClick?.profileId || entry.actionId!==applyingClick?.precisionActionId) cancel(entry);
    applying=true; error="";
    Promise.resolve().then(()=>{
      if(closed || editor.dirty || editor.busy || editor.profileId!==profileId || document?.selectedProfileId!==profileId) return;
      return applyAction(profileId,binding.actionId);
    }).catch(e=>{
      if (!closed) { error=e.message; onError(error); }
    }).finally(()=>{ applying=false; applyingClick=null; });
  }
  return {
    update(next) { document=next; synchronize(); },
    editorState(value) {
      if (!value || typeof value.dirty!=="boolean" || typeof value.busy!=="boolean" || typeof value.profileId!=="string" || value.profileId.length>128) throw new Error("Invalid input editor state.");
      editor={dirty:value.dirty,busy:value.busy,profileId:value.profileId};
      if(capture.state==="listening" && capture.profileId!==editor.profileId) this.captureCancel();
      synchronize(); return this.status();
    },
    status() {
      return {...host.state(),paused:paused(),captureState:capture.state,message:error || (editor.dirty ? "Save or discard edits to resume joystick actions." : editor.busy || applying ? "Joystick actions pause while settings are being saved." : capture.state === "listening" ? "Press a joystick button or POV direction." : "Joystick actions use the saved selected profile.")};
    },
    captureStart(actionId,profileId) {
      if(closed || typeof actionId!=="string" || !/^[a-zA-Z0-9.-]{1,160}$/.test(actionId) || typeof profileId!=="string" || !profileId || profileId.length>128) throw new Error("Invalid binding target.");
      clearTimeout(timer);
      cancelAll();
      capture={state:"listening",actionId,profileId};
      timer=setTimeout(()=>{capture={...capture,state:"error",error:"No button received. Check the device and try again."};timer=null;synchronize();},15000);
      timer.unref?.(); synchronize(); return this.captureStatus();
    },
    captureStatus() { return structuredClone(capture); },
    captureCancel() { cancelAll();clearTimeout(timer);timer=null;capture={state:"cancelled"};synchronize();return this.captureStatus(); },
    close() { closed=true;cancelAll();clearTimeout(timer);timer=null;host.close(); },
  };
}
module.exports={createInputController};
