import test from "node:test";
import assert from "node:assert/strict";
import placementModule from "../desktop/dashboard-placement.cjs";
import { createSpatialOverlay, updateCarPlacement } from "../src/spatial-overlays.mjs";

function fixture() {
  const item = {...createSpatialOverlay(), enabled: true};
  const profile = { id: "profile", game: "lmu", spatialOverlays: {enabled:true, items:[item]} };
  const pose = {x:.25,y:-.1,z:-.45,yaw:12,pitch:-3,roll:0,widthMeters:.32};
  const state = {profileId:profile.id,car:{id:"lmu:car-a"},items:[{id:item.id,source:{displaying:true}}]};
  const commands = [];
  const spatial = {status:()=>state, async placeDashboard(_game,_id,mode) {commands.push(mode);return pose;}};
  const placement = placementModule.createDashboardPlacement({getSpatial:()=>spatial,updateCarPlacement});
  const press = () => placement.toggle(profile,item,"revision-1");
  return {item,profile,pose,state,commands,placement,press};
}

test("second placement press freezes the pose for the current car, preserving the base dashboard",async()=>{
  const f=fixture();
  assert.equal(await f.press(),null);
  const locked=await f.press();
  assert.equal(locked.valid(),true);
  assert.equal(locked.item.x,f.item.x);
  assert.equal(locked.item.carPlacements[0].x,.25);
  assert.equal(locked.item.carPlacements[0].carId,"lmu:car-a");
  f.state.car={id:"lmu:car-b"};
  assert.equal(locked.valid(),false);
  await locked.finish();
  assert.deepEqual(f.commands,["start","freeze","cancel"]);
});

test("switching cars mid-gesture cancels before saving, unknown car can place the default",async()=>{
  const f=fixture();
  await f.press();
  f.state.car=null;
  await assert.rejects(f.press(),/car or profile changed/);
  assert.deepEqual(f.commands,["start","cancel"]);
  await f.press();
  const locked=await f.press();
  assert.equal(locked.item.x,.25);
  assert.equal(locked.item.carPlacements,undefined);
  await locked.finish();
});

test("reset cancels the current grab and applies a fresh headset pose only to the active car",async()=>{
 const f=fixture(); await f.press(); const reset=await f.placement.reset(f.profile,f.item,"revision-1");
 assert.deepEqual(f.commands,["start","cancel","reset"]);
 assert.equal(reset.item.x,f.item.x); assert.equal(reset.item.carPlacements[0].x,.25);
 assert.equal(reset.valid(),true); f.state.items=[]; assert.equal(reset.valid(),false); await reset.finish();
});
