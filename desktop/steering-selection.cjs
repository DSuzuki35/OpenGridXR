'use strict';
const { selectedForGame } = require('./runtime-config.cjs');
function steeringProfile(document, sessions) {
  const candidates = ['iracing', 'lmu'].map(game => selectedForGame(document, game)).filter(profile =>
    profile?.spatialOverlays?.enabled && profile.spatialOverlays.steeringInput?.source === 'device' &&
    sessions.some(session => session.game === profile.game && session.active && session.engineLoaded) &&
    profile.spatialOverlays.items.some(item => item.enabled && item.kind === 'url' &&
      (item.steeringFollow || item.carPlacements?.some(pose => pose.steeringFollow))));
  return candidates.find(profile => profile.id === document.selectedProfileId) || candidates[0];
}
module.exports = { steeringProfile };
