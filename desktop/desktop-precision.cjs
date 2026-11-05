'use strict';
function createDesktopPrecision({getDocument, getEditor, isEnabled, getSpatial, testMode=false}) {
  const presses = new Map();
  let closed = false;
  function release(record) {
    if (!record?.active) return;
    record.active = false;
    try { record.controller.desktopAction(record.game, record.overlayId, 'precision', {held:false, pressId:record.id}); } catch {}
  }
  return {
    apply(profileId, actionId, held, pressId) {
      const match = /^spatialOverlays\.([0-9a-f-]{36})\.precision$/i.exec(actionId);
      if (!match || typeof held !== 'boolean' || !Number.isSafeInteger(pressId) || pressId <= 0) return false;
      const key = `${profileId}:${actionId}`, prior = presses.get(key);
      if (!held) { if (prior?.id === pressId) release(prior); return true; }
      if (closed || testMode || prior && (pressId < prior.id || pressId === prior.id && !prior.active)) return false;
      const doc = getDocument(), editor = getEditor();
      const profile = doc?.profiles.find(p => p.id === profileId);
      const item = profile?.spatialOverlays?.items.find(i => i.id.toLowerCase() === match[1].toLowerCase());
      const allowed = doc?.selectedProfileId === profileId && editor.profileId === profileId && !editor.dirty && !editor.busy &&
        isEnabled('bindings') && isEnabled('spatial') && profile?.inputBindings?.enabled !== false &&
        profile?.spatialOverlays?.enabled && item?.enabled && item.kind === 'desktop' && item.interaction === 'head';
      if (prior && (prior.id !== pressId || !allowed)) release(prior);
      const record = prior?.id === pressId ? prior : {id:pressId, active:false};
      presses.set(key, record);
      if (!allowed) return false;
      const controller = getSpatial();
      if (!controller) { release(record); return false; }
      try {
        controller.desktopAction(profile.game, item.id, 'precision', {held:true, pressId});
        Object.assign(record, {active:true, controller, game:profile.game, overlayId:item.id});
        return true;
      } catch { release(record); return false; }
    },
    close() { closed = true; for (const record of presses.values()) release(record); presses.clear(); },
  };
}
module.exports = { createDesktopPrecision };
