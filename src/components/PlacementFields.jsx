import { useId, useState } from "react";
import { Minus, Plus } from "lucide-react";
import { poseRanges, steeringRanges, driverFitRanges } from "../spatial-overlays.mjs";

const labels = {
  x: ["Horizontal", "Horizontal position"], y: ["Vertical", "Vertical position"], z: ["Distance forward"],
  widthMeters: ["Width", "Overlay width"], yaw: ["Yaw", "Overlay yaw"], pitch: ["Pitch", "Overlay pitch"],
  roll: ["Roll", "Overlay roll"], pivotX: ["Horizontal", "Pivot horizontal"], pivotY: ["Vertical", "Pivot vertical"],
  pivotZ: ["Depth", "Pivot depth"], steeringTilt: ["Axis tilt", "Steering axis tilt"], opacity: ["Opacity", "Overlay opacity"],
  driverSeatHeight: ["Seat height"], driverSeatDistance: ["Seat distance"], driverShoulderWidth: ["Shoulder width"],
  driverTorsoLean: ["Torso lean"], driverLegLength: ["Leg length"], driverKneeHeight: ["Knee height"],
};

function PlacementField({ axis, value, onChange }) {
  const id = useId();
  const [draft, setDraft] = useState(null);
  const [caption, label = caption] = labels[axis];
  const angle = ["yaw", "pitch", "roll", "steeringTilt", "driverTorsoLean"].includes(axis);
  const factor = ["driverShoulderWidth", "driverLegLength"].includes(axis) ? 100 : 1;
  const unit = angle ? "°" : axis === "opacity" || factor === 100 ? "%" : "m";
  const digits = unit === "m" ? 3 : 1;
  const step = unit === "m" ? 0.001 : 0.1;
  const bounds = (poseRanges[axis] ?? steeringRanges[axis] ?? driverFitRanges[axis] ?? [0, 100]).map(n => n * factor);
  const [min, max] = axis === "z" ? [-bounds[1], -bounds[0]] : bounds;
  const current = axis === "z" ? -value : value * factor;
  const text = draft ?? String(Number(current.toFixed(digits)));
  const parsed = /^[+-]?(?:\d+(?:[.,]\d*)?|[.,]\d+)$/.test(text.trim()) ? Number(text.trim().replace(",", ".")) : NaN;
  const invalid = !Number.isFinite(parsed);
  function apply(number) {
    const next = Number(Math.max(min, Math.min(max, number)).toFixed(digits));
    if (next !== current) onChange({ [axis]: axis === "z" ? -next : next / factor });
    setDraft(null);
  }
  function increment(direction) {
    apply((invalid ? current : parsed) + direction * step);
  }
  function key(event) {
    if (event.altKey || event.ctrlKey || event.metaKey) return;
    switch (event.key) {
      case "ArrowUp": increment(1); break;
      case "ArrowDown": increment(-1); break;
      case "Enter": if (!invalid && draft !== null) apply(parsed); break;
      case "Escape": setDraft(null); break;
      default: return;
    }
    event.preventDefault();
  }
  return <div className="placement-field">
    <label htmlFor={id}>{caption}</label>
    <div className="placement-number">
      <button type="button" tabIndex={-1} aria-label={`Decrease ${label} by ${step} ${unit}`} onMouseDown={event => event.preventDefault()} onClick={() => increment(-1)}><Minus size={12}/></button>
      <input id={id} type="text" inputMode="decimal" autoComplete="off" spellCheck={false} role="spinbutton"
        aria-label={`${label} value`} aria-valuemin={min} aria-valuemax={max} aria-valuenow={current}
        aria-valuetext={`${current} ${unit}`} aria-invalid={invalid || undefined} aria-describedby={invalid ? `${id}-error` : undefined}
        value={text} onChange={event => setDraft(event.target.value)} onKeyDown={key}
        onBlur={() => { if (draft !== null) invalid ? setDraft(null) : apply(parsed); }} />
      <span aria-hidden="true">{unit}</span>
      <button type="button" tabIndex={-1} aria-label={`Increase ${label} by ${step} ${unit}`} onMouseDown={event => event.preventDefault()} onClick={() => increment(1)}><Plus size={12}/></button>
    </div>
    {invalid && <small id={`${id}-error`} className="placement-error">Enter a decimal number.</small>}
    <input className="placement-slider" type="range" aria-label={label} min={min} max={max} step={step}
      style={{ "--placement-progress": `${(current - min) * 100 / (max - min)}%` }}
      aria-valuetext={`${current} ${unit}`} value={current} onChange={event => apply(Number(event.target.value))}/>
  </div>;
}

export function PlacementFields({ axes, pose, onChange }) {
  return <div className="placement-fields">{axes.map(axis => <PlacementField key={axis} axis={axis}
    value={pose[axis] ?? 0} onChange={onChange} />)}</div>;
}
