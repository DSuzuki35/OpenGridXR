"use strict";

const DEVICE = /^dinput:\{[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\}$/;
function validInput(value) {
  return value && DEVICE.test(value.deviceId) && Number.isInteger(value.index) && value.index >= 0 &&
    (value.kind === "button" ? value.index < 128 : value.kind === "pov" && value.index < 4 && Number.isInteger(value.direction) && value.direction >= 0 && value.direction < 8);
}
const matches = (a,b) => a.deviceId === b.deviceId && a.kind === b.kind && a.index === b.index && (a.kind !== "pov" || a.direction === b.direction);

function createInputController({createHost,listActions,applyAction,testMode=false,testFixture=false,onError=()=>{}}) {
  let document, closed=false, applying=false, error="", timer=null;
  let editor={dirty:true,busy:true,profileId:""};
  let capture={state:"idle"};
  const host=createHost({onPress:press,onStatus:()=>{}});
  const selected=()=>document?.profiles.find(p=>p.id===document.selectedProfileId);
  const bindings=()=>selected()?.inputBindings;
  function synchronize() {
    const settings=bindings();
    const allowed=new Set(selected() ? listActions(selected()).filter(action=>!action.disabled).map(action=>action.id) : []);
    const hasBindings=settings?.enabled !== false && settings?.bindings?.some(binding=>allowed.has(binding.actionId));
    host.setActive(!closed && (!testMode || testFixture) && (capture.state === "listening" || Boolean(hasBindings)));
  }
  function paused() {
    return closed || applying || editor.dirty || editor.busy || editor.profileId !== document?.selectedProfileId || !["idle","cancelled"].includes(capture.state);
  }
  function press(input) {
    if (closed || !validInput(input)) return;
    if (capture.state === "listening") {
      clearTimeout(timer); timer=null;
      const deviceName=(host.state().devices.find(d=>d.id===input.deviceId)?.name || "Joystick").slice(0,160);
      capture={...capture,state:"captured",input:{deviceId:input.deviceId,deviceName,kind:input.kind,index:input.index,...(input.kind==="pov"?{direction:input.direction}:{})}};
      synchronize(); return;
    }
    if (paused()) return;
    const settings=bindings();
    if (!settings || settings.enabled === false) return;
    const binding=settings.bindings.find(b=>matches(b,input));
    if (!binding || !listActions(selected()).some(a=>a.id===binding.actionId && !a.disabled)) return;
    const profileId=selected().id;
    applying=true; error="";
    Promise.resolve().then(()=>{
      if(closed || editor.dirty || editor.busy || editor.profileId!==profileId || document?.selectedProfileId!==profileId) return;
      return applyAction(profileId,binding.actionId);
    }).catch(e=>{
      if (!closed) { error=e.message; onError(error); }
    }).finally(()=>{ applying=false; });
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
      capture={state:"listening",actionId,profileId};
      timer=setTimeout(()=>{capture={...capture,state:"error",error:"No button received. Check the device and try again."};timer=null;synchronize();},15000);
      timer.unref?.(); synchronize(); return this.captureStatus();
    },
    captureStatus() { return structuredClone(capture); },
    captureCancel() { clearTimeout(timer);timer=null;capture={state:"cancelled"};synchronize();return this.captureStatus(); },
    close() { closed=true;clearTimeout(timer);timer=null;host.close(); },
  };
}
module.exports={createInputController};
