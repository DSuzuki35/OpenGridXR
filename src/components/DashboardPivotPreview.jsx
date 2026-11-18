import { useRef, useState } from "react";
import "./dashboard-pivot.css";

export default function DashboardPivotPreview({ placement, aspect = 2, onChange, disabled = false }) {
  const { widthMeters = 0.3, pivotX = 0, pivotY = 0, pivotZ = 0, steeringTilt = 0 } = placement;
  const [angle, setAngle] = useState(0);
  const drag = useRef(null);
  const panelHeight = widthMeters / Math.max(0.1, aspect);
  const extent = Math.max(widthMeters * 1.5, panelHeight * 2.8,
    Math.abs(pivotX) * 2.4, Math.abs(pivotY) * 5, 0.3);
  const span = drag.current?.span ?? extent;
  const clamp = value => Math.max(-5, Math.min(5, Math.round(value * 1000) / 1000));
  const place = (x, y) => onChange({ pivotX: clamp(x), pivotY: clamp(y) });
  function pointer(event) {
    const rect = event.currentTarget.getBoundingClientRect();
    const scale = drag.current.span;
    place(((event.clientX - rect.left) / rect.width - 0.5) * scale,
      (0.5 - (event.clientY - rect.top) / rect.height) * scale / 2);
  }
  function key(event) {
    const move = { ArrowLeft: [-1,0], ArrowRight: [1,0], ArrowUp: [0,1], ArrowDown: [0,-1] }[event.key];
    if (!move || disabled) return;
    event.preventDefault();
    event.stopPropagation();
    const step = event.shiftKey ? 0.001 : 0.01;
    place(pivotX + move[0] * step, pivotY + move[1] * step);
  }
  const xPercent = 50 + 100 * pivotX / span;
  const yPercent = 50 - 200 * pivotY / span;
  // Front projection of the same tilted-axis rotation used in the headset.
  const tilt = steeringTilt * Math.PI / 180, turn = angle * Math.PI / 180;
  const sy = -Math.sin(tilt), sz = Math.cos(tilt), c = Math.cos(turn), s = Math.sin(turn);
  const translateX = -100 * pivotZ * sy * s / span;
  const translateY = 200 * pivotZ * (1-c) * sy * sz / span;
  const transform = `translate(${translateX}%, ${translateY}%) matrix(${c},${-sz*s},${sz*s},${c+(1-c)*sy*sy},0,0)`;
  function endDrag(e) {
    if (drag.current?.id === e.pointerId) drag.current = null;
  }
  return <div className="dashboard-pivot">
    <div className="dashboard-pivot-caption"><strong>Wheel hub</strong><span>Front view</span></div>
    <button type="button" className="dashboard-pivot-map" disabled={disabled}
      aria-label="Set wheel hub on the dashboard diagram" onKeyDown={key}
      onPointerDown={e => {
        if (disabled || e.button !== 0 || !e.isPrimary) return;
        drag.current = { id: e.pointerId, span };
        e.currentTarget.setPointerCapture(e.pointerId);
        pointer(e);
      }}
      onPointerMove={e => { if (drag.current?.id === e.pointerId) pointer(e); }}
      onPointerUp={endDrag} onPointerCancel={endDrag} onLostPointerCapture={endDrag}>
      <span className="dashboard-pivot-rig" style={{
        transformOrigin: `${xPercent}% ${yPercent}%`, transform,
      }}>
        <span className="dashboard-pivot-panel" style={{ width: `${100*widthMeters/span}%`, height: `${200*panelHeight/span}%` }}>Dash</span>
      </span>
      <span className="dashboard-pivot-hub" style={{left: `${xPercent}%`, top: `${yPercent}%`}} />
    </button>
    <p>Drag the crosshair to the wheel hub. Arrow keys move it 1 cm. Hold Shift for 1 mm.</p>
    <label className="dashboard-pivot-angle">Preview turn
      <input type="range" min="-90" max="90" step="1" value={angle} disabled={disabled}
        onChange={e => setAngle(Number(e.target.value))} /><output>{angle}°</output>
    </label>
  </div>;
}
