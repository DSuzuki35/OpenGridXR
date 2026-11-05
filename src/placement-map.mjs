import { spatialPositionLimits } from "./spatial-overlays.mjs";

export const mapBounds = Object.freeze({ left: 60, top: 30, width: 540, height: 230 });
const clamp = (value, low, high) => Math.max(low, Math.min(high, value));
const position = (axis, value) => Number(clamp(value, ...spatialPositionLimits[axis]).toFixed(2));
const verticalAxis = (view) => view === "front" ? "y" : "z";

export function mapPosition(item, view) {
  const axis = verticalAxis(view);
  const [low, high] = spatialPositionLimits[axis];
  const normalized = (item[axis] - low) / (high - low);
  return {
    x: mapBounds.left + (item.x - spatialPositionLimits.x[0]) / (spatialPositionLimits.x[1] - spatialPositionLimits.x[0]) * mapBounds.width,
    y: mapBounds.top + (view === "front" ? 1 - normalized : normalized) * mapBounds.height,
  };
}

export function mapPatch(point, view) {
  const axis = verticalAxis(view);
  const [low, high] = spatialPositionLimits[axis];
  const normalized = (point.y - mapBounds.top) / mapBounds.height;
  return {
    x: position("x", spatialPositionLimits.x[0] + (point.x - mapBounds.left) / mapBounds.width * (spatialPositionLimits.x[1] - spatialPositionLimits.x[0])),
    [axis]: position(axis, low + (view === "front" ? 1 - normalized : normalized) * (high - low)),
  };
}

export function nudgePosition(item, view, key, coarse = false) {
  if (!["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown"].includes(key)) return null;
  const axis = key === "ArrowLeft" || key === "ArrowRight" ? "x" : verticalAxis(view);
  const sign = key === "ArrowRight" || (key === "ArrowUp" && view === "front") || (key === "ArrowDown" && view === "top") ? 1 : -1;
  return { [axis]: position(axis, item[axis] + sign * (coarse ? 0.1 : 0.01)) };
}
