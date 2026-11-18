"use strict";

function createDashboardPlacement({ getSpatial, updateCarPlacement }) {
  let active = null;
  async function cancel(gesture = active) {
    if (!gesture || gesture !== active) return;
    active = null;
    await gesture.spatial.placeDashboard(gesture.game, gesture.id, "cancel").catch(() => {});
  }
  async function place(profile, item, revision, reset) {
    const spatial = getSpatial();
    if (!spatial || item?.kind !== "url" || !item.enabled || !profile.spatialOverlays?.enabled)
      throw new Error("Show the web dashboard in the headset before placing it.");
    const context = () => {
      const status = spatial.status(profile.game);
      const source = status.items.find(s => s.id === item.id);
      return status.profileId === profile.id && source?.source?.displaying
        ? { carId: status.car?.id || null } : null;
    };
    const initial = context();
    if (!initial) { await cancel(); throw new Error("The dashboard is no longer visible."); }
    if (active && (reset || active.profile !== profile.id || active.id !== item.id)) await cancel();
    if (!active) {
      const gesture = { spatial, profile: profile.id, game: profile.game, id: item.id, revision, ...initial };
      active = gesture;
      if (!reset) {
        try { await spatial.placeDashboard(profile.game, item.id, "start"); }
        catch (error) { await cancel(gesture); throw error; }
        return null;
      }
    }
    const gesture = active;
    const valid = () => active === gesture && revision === gesture.revision &&
      context()?.carId === gesture.carId;
    try {
      if (!valid()) throw new Error("The active car or profile changed. Start placement again.");
      const pose = await spatial.placeDashboard(profile.game, item.id, reset ? "reset" : "freeze");
      if (!valid()) throw new Error("The active car or profile changed during placement.");
      const patch = { anchor: "local" };
      for (const key of ["x","y","z","yaw","pitch","roll","widthMeters"]) {
        if (!Number.isFinite(pose?.[key])) throw new Error("The headset has no fresh dashboard position.");
        patch[key] = pose[key];
      }
      const placed = gesture.carId ? updateCarPlacement(item, gesture.carId, patch) : { ...item, ...patch };
      return { item: placed, valid, finish: () => cancel(gesture) };
    } catch (error) { await cancel(gesture); throw error; }
  }
  return { toggle: (profile,item,revision) => place(profile,item,revision,false),
    reset: (profile,item,revision) => place(profile,item,revision,true), cancel, editorChanged(state) {
    if (active && (state.dirty || state.busy || state.profileId !== active.profile)) void cancel();
  } };
}
module.exports = { createDashboardPlacement };
