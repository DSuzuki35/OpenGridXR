import { validateProfile } from "./model.mjs";
import { framegenDefaults, overlayDefaults } from "./enhancements.mjs";
import { spatialDefaults } from "./spatial-overlays.mjs";

export const bindingDefaults = () => ({ enabled: true, bindings: [] });
const SPECS = [
  ["mask.enabled", "Use contour in driving", "View & crop"],
  ["mask.linked", "Link eyes", "View & crop"],
  ["helmet.enabled", "Save helmet preference", "Helmet"],
  ["stream.mirror", "Mirror output", "Stream"],
  ["stream.fullscreen", "Start fullscreen", "Stream"],
  ["stream.extraSymmetric", "Symmetric extra area", "Stream"],
  ["framegen.enabled", "Configure frame generation", "Frame generation", true],
  ["overlay.enabled", "Enable performance HUD", "Performance HUD", true],
  [
    "overlay.showPerformance",
    "Show FPS & frame times",
    "Performance HUD",
    true,
  ],
  ["overlay.showGraph", "Show frame-time graph", "Performance HUD", true],
  ["overlay.showRuntime", "Show runtime", "Performance HUD", true],
  ["overlay.showApp", "Show application", "Performance HUD", true],
  ["overlay.showClock", "Show clock", "Performance HUD", true],
  ["overlay.showFramegen", "Show frame generation", "Performance HUD", true],
  ["overlay.showToolkit", "Show toolkit settings", "Performance HUD", true],
  ["telemetry.enabled", "Enable telemetry", "Telemetry"],
  ["spatialOverlays.enabled", "Enable spatial overlays", "Spatial overlays"],
];
const GUID = "[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}";
const DEVICE = new RegExp(`^dinput:\\{${GUID}\\}$`);
const DYNAMIC = new RegExp(
  `^spatialOverlays\\.(${GUID})\\.(enabled|showInStream)$`,
  "i",
);
function defaults(group) {
  if (group === "framegen") return framegenDefaults();
  if (group === "overlay") return overlayDefaults();
  if (group === "telemetry") return { enabled: false, updateHz: 20 };
  if (group === "spatialOverlays") return spatialDefaults();
  if (group === "stream") return { fullscreen: true, extraSymmetric: false };
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
      checked: !!{ ...defaults(section), ...profile?.[section] }[key],
    };
  });
  for (const item of profile?.spatialOverlays?.items || []) {
    for (const [key, label] of [
      ["enabled", "Enable overlay"],
      ["showInStream", "Show in stream"],
    ])
      actions.push({
        id: `spatialOverlays.${item.id}.${key}`,
        label: `${item.name} - ${label}`,
        group: "Spatial overlays",
        checked: item[key] === true,
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
    ? new Set(listToggleActions(profile).map((action) => action.id))
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
      [key]: !action.checked,
    };
    if (actionId === "stream.extraSymmetric" && next.stream.extraSymmetric)
      next.stream.extraLeft = next.stream.extraRight = Math.max(
        next.stream.extraLeft,
        next.stream.extraRight,
      );
    if (actionId === "mask.linked" && next.mask.linked)
      next.mask.right = next.mask.left.map((point) => ({ ...point }));
  }
  return validateProfile(next);
}
