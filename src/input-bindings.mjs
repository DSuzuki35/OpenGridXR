import { validateProfile } from "./model.mjs";
import { framegenDefaults, overlayDefaults } from "./enhancements.mjs";
import { spatialDefaults } from "./spatial-overlays.mjs";

export const bindingDefaults = () => ({ enabled: true, bindings: [] });
const SPECS = [
  ["overlay.enabled", "Toggle performance HUD", "Performance HUD"],
  ["overlay.layout", "Switch HUD mode: compact / expanded", "Performance HUD"],
  ["framegen.enabled", "Toggle frame generation", "Frame generation"],
  ["spatialOverlays.enabled", "Toggle all spatial overlays", "Spatial overlays"],
];
// Older profiles remain readable, but setup actions are never offered or executed.
const LEGACY_ACTIONS = new Set([
  "mask.enabled",
  "mask.linked",
  "helmet.enabled",
  "stream.mirror",
  "stream.fullscreen",
  "stream.extraSymmetric",
  "telemetry.enabled",
  "overlay.showPerformance",
  "overlay.showGraph",
  "overlay.showRuntime",
  "overlay.showApp",
  "overlay.showClock",
  "overlay.showFramegen",
  "overlay.showToolkit",
]);
const GUID = "[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}";
const DEVICE = new RegExp(`^dinput:\\{${GUID}\\}$`);
const DYNAMIC = new RegExp(
  `^spatialOverlays\\.(${GUID})\\.(enabled|showInStream)$`,
  "i",
);
function defaults(group) {
  if (group === "framegen") return framegenDefaults();
  if (group === "overlay") return overlayDefaults();
  if (group === "spatialOverlays") return spatialDefaults();
  return {};
}
export function listToggleActions(profile) {
  const actions = SPECS.map(([id, label, group, restartRequired = false]) => {
    const [section, key] = id.split(".");
    return {
      id,
      label,
      group,
      restartRequired,
      disabled: false,
      checked:
        key === "layout"
          ? profile?.overlay?.layout === "compact"
          : !!{ ...defaults(section), ...profile?.[section] }[key],
    };
  });
  for (const item of profile?.spatialOverlays?.items || []) {
    actions.push({
      id: `spatialOverlays.${item.id}.enabled`,
      label: `${item.name} - Toggle overlay`,
      group: "Spatial overlays",
      checked: item.enabled === true,
      disabled: false,
      restartRequired: false,
    });
  }
  return actions;
}
export function validateBindings(value, profile) {
  if (value === undefined) return value;
  if (
    !value ||
    typeof value !== "object" ||
    Array.isArray(value) ||
    typeof value.enabled !== "boolean" ||
    !Array.isArray(value.bindings) ||
    value.bindings.length > 128
  )
    throw new Error("Invalid input bindings.");
  const allowed = profile
    ? new Set([
        ...listToggleActions(profile).map((action) => action.id),
        ...LEGACY_ACTIONS,
        ...(profile.spatialOverlays?.items || []).map(
          (item) => `spatialOverlays.${item.id}.showInStream`,
        ),
      ])
    : null;
  const used = new Set();
  for (const binding of value.bindings) {
    if (
      !binding ||
      typeof binding !== "object" ||
      Array.isArray(binding) ||
      typeof binding.actionId !== "string" ||
      (allowed
        ? !allowed.has(binding.actionId)
        : !SPECS.some(([id]) => id === binding.actionId) &&
          !LEGACY_ACTIONS.has(binding.actionId) &&
          !DYNAMIC.test(binding.actionId)) ||
      typeof binding.deviceId !== "string" ||
      !DEVICE.test(binding.deviceId) ||
      typeof binding.deviceName !== "string" ||
      !binding.deviceName.trim() ||
      binding.deviceName.length > 160 ||
      /[\x00-\x1f]/.test(binding.deviceName) ||
      !["button", "pov"].includes(binding.kind) ||
      !Number.isInteger(binding.index) ||
      binding.index < 0 ||
      binding.index > (binding.kind === "button" ? 127 : 3) ||
      (binding.kind === "pov"
        ? !Number.isInteger(binding.direction) ||
          binding.direction < 0 ||
          binding.direction > 7
        : binding.direction !== undefined)
    )
      throw new Error("Invalid input binding or unavailable toggle action.");
    const key = `${binding.deviceId}:${binding.kind}:${binding.index}:${binding.direction ?? ""}`;
    if (used.has(key))
      throw new Error("This physical input is already bound in this profile.");
    used.add(key);
  }
  return value;
}
export function toggleProfileAction(profile, actionId) {
  validateProfile(profile);
  const action = listToggleActions(profile).find(
    (item) => item.id === actionId,
  );
  if (!action || action.disabled)
    throw new Error("This toggle action is unavailable.");
  const next = structuredClone(profile);
  const dynamic = DYNAMIC.exec(actionId);
  if (dynamic) {
    const item = next.spatialOverlays.items.find(
      (item) => item.id === dynamic[1],
    );
    if (!item) throw new Error("This overlay no longer exists.");
    item[dynamic[2]] = !action.checked;
  } else {
    const [group, key] = actionId.split(".");
    next[group] = {
      ...defaults(group),
      ...next[group],
      [key]:
        actionId === "overlay.layout"
          ? action.checked ? "expanded" : "compact"
          : !action.checked,
    };
  }
  return validateProfile(next);
}
