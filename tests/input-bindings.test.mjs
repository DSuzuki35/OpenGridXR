import test from "node:test";
import assert from "node:assert/strict";
import { createProfile, validateProfile } from "../src/model.mjs";
import { createSpatialOverlay } from "../src/spatial-overlays.mjs";
import { bindingDefaults, validateBindings, listToggleActions, toggleProfileAction } from "../src/input-bindings.mjs";
import * as bindingsModule from "../src/input-bindings.mjs";

const deviceId = "dinput:{01234567-89ab-cdef-0123-456789abcdef}";
const button = (actionId = "overlay.enabled", index = 3) => ({ actionId, deviceId, deviceName: "Isolated test wheel", kind: "button", index });
const removed = ["mask.enabled", "mask.linked", "helmet.enabled", "stream.mirror", "stream.fullscreen", "stream.extraSymmetric", "telemetry.enabled", "overlay.showPerformance", "overlay.showGraph", "overlay.showRuntime", "overlay.showApp", "overlay.showClock", "overlay.showFramegen", "overlay.showToolkit"];

test("bindings are optional and reject malformed or duplicate physical controls", () => {
  const p = createProfile();
  delete p.inputBindings;
  assert.doesNotThrow(() => validateProfile(p));
  p.inputBindings = bindingDefaults();
  p.inputBindings.bindings.push(button());
  assert.doesNotThrow(() => validateProfile(p));
  for (const patch of [
    { deviceId: "device-0" }, { kind: "axis" }, { index: 128 }, { index: -1 },
    { kind: "pov", index: 4, direction: 0 }, { kind: "pov", index: 0, direction: 8 },
    { actionId: "__proto__.enabled" }, { deviceName: "x\n" },
  ]) assert.throws(() => validateBindings({ enabled: true, bindings: [{ ...button(), ...patch }] }, p));
  assert.throws(() => validateBindings({ enabled: true, bindings: [button(), button("framegen.enabled")] }, p), /already|duplicate/i);
  assert.doesNotThrow(() => validateBindings({ enabled: true, bindings: [button(), { ...button("framegen.enabled"), kind: "pov", index: 0, direction: 7 }] }, p));
  assert.throws(() => validateBindings({ enabled: true, bindings: Array.from({ length: 129 }, (_, index) => button("overlay.enabled", index % 128)) }, p));
});

test("controller menu offers only quick driving actions and existing overlay visibility", () => {
  const p = createProfile();
  const source = createSpatialOverlay();
  p.spatialOverlays.items.push(source);
  assert.deepEqual(new Set(listToggleActions(p).map(action => action.id)), new Set([
    "framegen.enabled", "overlay.enabled", "overlay.layout", "spatialOverlays.enabled", "centering.game", "colorCorrection.cycle", "helmet.visorCycle", `spatialOverlays.${source.id}.enabled`,
  ]));
  assert.throws(() => toggleProfileAction(p, `spatialOverlays.${crypto.randomUUID()}.enabled`));
  assert.throws(() => validateBindings({ enabled: true, bindings: [button(`spatialOverlays.${crypto.randomUUID()}.enabled`)] }, p));
});

test('visor cycling is a transient button or POV action requiring helmet and live visor switching',()=>{
  const p=createProfile();
  const action=()=>listToggleActions(p).find(a=>a.id==='helmet.visorCycle');
  assert.equal(action()?.disabled,true);
  p.helmet.enabled=true;p.helmet.visorHotkey=true;
  assert.equal(action().disabled,false);assert.equal(action().transient,true);assert.equal(action().checked,false);
  for(const input of [button('helmet.visorCycle'),{...button('helmet.visorCycle'),kind:'pov',index:0,direction:2}]){
    p.inputBindings.bindings=[input];assert.doesNotThrow(()=>validateProfile(p));
    assert.doesNotThrow(()=>validateBindings(p.inputBindings));
  }
  const before=structuredClone(p);assert.throws(()=>toggleProfileAction(p,'helmet.visorCycle'),/unavailable/);assert.deepEqual(p,before);
  p.helmet.visorHotkey=false;assert.equal(action().disabled,true);
  p.helmet.visorHotkey=true;p.helmet.enabled=false;assert.equal(action().disabled,true);
});

test("game centering accepts buttons and POV bindings but never toggles profile settings", () => {
  const p = createProfile('lmu');
  assert.equal(listToggleActions(p).find(action => action.id === 'centering.game')?.disabled, true);
  p.centering.gameShortcut = 'Ctrl+F1';
  assert.equal(listToggleActions(p).find(action => action.id === 'centering.game')?.disabled, false);
  for (const descriptor of [button('centering.game'), {...button('centering.game'), kind:'pov', index:0, direction:6}]) {
    p.inputBindings.bindings = [descriptor];
    assert.doesNotThrow(() => validateProfile(p));
    assert.doesNotThrow(() => validateBindings(p.inputBindings));
  }
  const before = structuredClone(p);
  assert.throws(() => toggleProfileAction(p, 'centering.game'), /not.*toggle|unavailable/i);
  assert.deepEqual(p, before);
});

test("old setup bindings load compatibly but cannot execute or mutate a profile", () => {
  const p = createProfile();
  const source = createSpatialOverlay();
  p.spatialOverlays.items.push(source);
  const obsolete = [...removed, `spatialOverlays.${source.id}.showInStream`];
  p.inputBindings.bindings = obsolete.map((action, index) => button(action, index));
  const before = structuredClone(p);
  assert.doesNotThrow(() => validateProfile(p));
  for (const action of obsolete) assert.throws(() => toggleProfileAction(p, action), /unavailable/i, action);
  assert.deepEqual(p, before);
  assert.equal(toggleProfileAction(p, "overlay.enabled").overlay.enabled, true);
});

test("driving toggles preserve other settings and initialize optional defaults", () => {
  const p = createProfile();
  const source = createSpatialOverlay();
  p.spatialOverlays.items.push(source);
  const before = structuredClone(p);
  assert.equal(toggleProfileAction(p, "framegen.enabled").framegen.enabled, true);
  assert.equal(toggleProfileAction(p, "overlay.enabled").overlay.enabled, true);
  assert.equal(toggleProfileAction(p, "spatialOverlays.enabled").spatialOverlays.enabled, true);
  const shown = toggleProfileAction(p, `spatialOverlays.${source.id}.enabled`);
  assert.equal(shown.spatialOverlays.items[0].enabled, !source.enabled);
  assert.equal(shown.spatialOverlays.items[0].showInStream, source.showInStream);
  assert.deepEqual(p, before);
  delete p.framegen;
  delete p.overlay;
  assert.equal(toggleProfileAction(p, "framegen.enabled").framegen.enabled, true);
  assert.equal(toggleProfileAction(p, "overlay.enabled").overlay.enabled, true);
  assert.throws(() => toggleProfileAction({ ...p, stream: { ...p.stream, extraLeft: 99 } }, "overlay.enabled"));
});

test("overlay mode binding cycles expanded and compact while preserving chosen content", () => {
  const p = createProfile();
  p.overlay.showClock = true;
  p.overlay.showGraph = false;
  const compact = toggleProfileAction(p, "overlay.layout");
  assert.deepEqual(compact.overlay, { ...p.overlay, layout: "compact" });
  const expanded = toggleProfileAction(compact, "overlay.layout");
  assert.deepEqual(expanded.overlay, p.overlay);
  delete p.overlay;
  assert.equal(toggleProfileAction(p, "overlay.layout").overlay.layout, "compact");
});
test("desktop click bindings are transient and unavailable on web or window sources", () => {
  const p = createProfile(), desktop = { ...createSpatialOverlay(), kind: "desktop", interaction: "head" };
  p.spatialOverlays.items.push(desktop, createSpatialOverlay());
  for (const action of ["leftClick", "rightClick", "resume"]) {
    const id = `spatialOverlays.${desktop.id}.${action}`;
    assert.equal(listToggleActions(p).find(a => a.id === id)?.transient, true);
    assert.doesNotThrow(() => validateBindings({ enabled: true, bindings: [button(id)] }, p));
    assert.doesNotThrow(() => validateBindings({ enabled: true, bindings: [button(id)] }));
    const before = structuredClone(p);
    assert.throws(() => toggleProfileAction(p, id), /unavailable/i);
    assert.deepEqual(p, before);
    assert.throws(() => validateBindings({ enabled: true, bindings: [button(`spatialOverlays.${p.spatialOverlays.items[1].id}.${action}`)] }, p));
  }
});

test('precision binding is a transient hold for head desktop only and is pruned after mode changes',()=>{
  const p=createProfile(),source={...createSpatialOverlay(),kind:'desktop',interaction:'head'};p.spatialOverlays.items=[source];
  const id=`spatialOverlays.${source.id}.precision`,action=listToggleActions(p).find(a=>a.id===id);
  assert.equal(action?.hold,true);assert.equal(action.transient,true);assert.equal(action.checked,false);assert.equal(action.disabled,false);
  assert.doesNotThrow(()=>validateBindings({enabled:true,bindings:[button(id)]},p));assert.throws(()=>toggleProfileAction(p,id));
  p.inputBindings.bindings=[button(id)];source.interaction='mouse';
  assert.equal(listToggleActions(p).find(a=>a.id===id)?.disabled,true);assert.deepEqual(bindingsModule.pruneSpatialBindings(p).inputBindings.bindings,[]);
  source.kind='url';assert.equal(listToggleActions(p).find(a=>a.id===id),undefined);
});
test("changing desktop kind prunes transient bindings while preserving surviving visibility and other controls", () => {
  assert.equal(typeof bindingsModule.pruneSpatialBindings, "function");
  for (const kind of ["url", "window"]) {
    const p = createProfile(), source = { ...createSpatialOverlay(), kind: "desktop" }, removed = createSpatialOverlay();
    p.spatialOverlays.items = [source, removed];
    p.inputBindings.bindings = ["leftClick", "rightClick", "resume", "enabled", "showInStream"].map((action, index) => button(`spatialOverlays.${source.id}.${action}`, index));
    p.inputBindings.bindings.push(button(`spatialOverlays.${removed.id}.enabled`, 5), button("overlay.enabled", 6));
    source.kind = kind; p.spatialOverlays.items.pop();
    const before = structuredClone(p), next = bindingsModule.pruneSpatialBindings(p);
    assert.deepEqual(next.inputBindings.bindings.map(binding => binding.actionId), [`spatialOverlays.${source.id}.enabled`, `spatialOverlays.${source.id}.showInStream`, "overlay.enabled"]);
    assert.deepEqual(p, before, "pruning is immutable");
    assert.doesNotThrow(() => validateProfile(next));
  }
  const p = createProfile(), desktop = { ...createSpatialOverlay(), kind: "desktop" };
  p.spatialOverlays.items = [desktop]; p.inputBindings.bindings = [button(`spatialOverlays.${desktop.id}.resume`)];
  assert.deepEqual(bindingsModule.pruneSpatialBindings(p), p, "live desktop actions survive");
});
