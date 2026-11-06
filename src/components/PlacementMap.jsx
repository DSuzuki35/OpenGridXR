import { useEffect, useId, useRef, useState } from "react";
import { Move } from "lucide-react";
import { Panel, Segmented } from "./Controls.jsx";
import { mapBounds, mapPatch, mapPosition, nudgePosition } from "../placement-map.mjs";
import "./PlacementMap.css";

export default function PlacementMap({ items, selected, onSelect, onChange, disabled = false, profileKey }) {
  const [view, setView] = useState("top");
  const svgRef = useRef(null);
  const drag = useRef(null);
  const gridId = useId();
  const helpId = useId();
  const axis = view === "front" ? "y" : "z";
  const endDrag = () => {
    const current = drag.current;
    drag.current = null;
    if (current && svgRef.current?.hasPointerCapture(current.pointerId)) svgRef.current.releasePointerCapture(current.pointerId);
  };
  useEffect(() => { endDrag(); }, [disabled, profileKey, view]);
  useEffect(() => {
    if (drag.current && !items.some(item => item.id === drag.current.id)) endDrag();
  }, [items]);

  const pointAt = (event) => {
    const matrix = svgRef.current?.getScreenCTM();
    return matrix ? new DOMPoint(event.clientX, event.clientY).matrixTransform(matrix.inverse()) : null;
  };
  const move = (event) => {
    const current = drag.current;
    if (!current || event.pointerId !== current.pointerId) return;
    if (disabled || current.profileKey !== profileKey || selected?.id !== current.id) { endDrag(); return; }
    if (!current.moved && Math.hypot(event.clientX - current.start.x, event.clientY - current.start.y) < 2) return;
    current.moved = true;
    const point = pointAt(event);
    if (point) onChange(current.id, mapPatch({ x: point.x - current.offset.x, y: point.y - current.offset.y }, view));
  };
  const cancel = () => {
    const current = drag.current;
    endDrag();
    if (current && !disabled && current.profileKey === profileKey && selected?.id === current.id) onChange(current.id, current.original);
  };
  return (
    <Panel title="Placement map" icon={Move} className="spatial-map-panel spatial-map-interactive"
      action={<Segmented label="Placement view" value={view} onChange={setView} options={[{ value: "top", label: "Top · X/Z" }, { value: "front", label: "Front · X/Y" }]} />}>
      <svg ref={svgRef} viewBox="0 0 660 310" role="group" aria-label={`${view === "top" ? "Top" : "Front"} view overlay placement`} aria-describedby={helpId}
        onPointerMove={move} onPointerUp={(event) => { if (drag.current?.pointerId === event.pointerId) { move(event); endDrag(); } }}
        onPointerCancel={cancel} onLostPointerCapture={endDrag} onKeyDown={(event) => { if (event.key === "Escape" && drag.current) { event.preventDefault(); cancel(); } }}>
        <defs><pattern id={gridId} x={mapBounds.left} y={mapBounds.top} width="54" height="23" patternUnits="userSpaceOnUse"><path d="M54 0H0V23" fill="none" stroke="#ad82c1" strokeOpacity=".13" /></pattern></defs>
        <rect x="60" y="30" width="540" height="230" fill={`url(#${gridId})`} stroke="#604773" />
        <path d={view === "front" ? "M330 30V260M60 145H600" : "M330 30V260"} stroke="#a17abe" strokeOpacity=".45" strokeDasharray="5 6" />
        <g className="spatial-map-labels" fill="#bda1cf" fontSize="10">
          <text x="60" y="282">−5 m</text><text x="600" y="282" textAnchor="end">+5 m</text>
          <text x="330" y="300" textAnchor="middle">X · LEFT / RIGHT</text>
          <text x="50" y="34" textAnchor="end">{view === "top" ? "−10 m" : "+5 m"}</text>
          <text x="50" y="263" textAnchor="end">{view === "top" ? "−0.1 m" : "−5 m"}</text>
          <text x="330" y="17" textAnchor="middle">{view === "top" ? "Z · FORWARD" : "Y · UP / DOWN"}</text>
        </g>
        <g transform={`translate(330 ${view === "top" ? 260 : 145})`} aria-hidden="true"><circle r="6" fill="#30203f" stroke="#a17abe" /><path d="M-3 2 0-3 3 2" fill="none" stroke="#cda9e7" /></g>
        {[...items.filter(item => item.id !== selected?.id), ...items.filter(item => item.id === selected?.id)].map(item => {
          const point = mapPosition(item, view);
          const active = selected?.id === item.id;
          const width = Math.max(24, item.widthMeters * mapBounds.width / 10);
          const height = view === "front" ? Math.max(18, width * item.height / item.width) : 14;
          return <g key={item.id} className="spatial-map-item" transform={`translate(${point.x} ${point.y})`} role="button" tabIndex={disabled ? -1 : 0}
            aria-label={`Move overlay ${item.name}`} aria-pressed={active} aria-disabled={disabled} aria-describedby={helpId}
            onClick={() => { if (!disabled) onSelect(item.id); }}
            onPointerDown={(event) => {
              if (disabled || event.button !== 0 || drag.current) return;
              const pointer = pointAt(event);
              if (!pointer) return;
              event.preventDefault(); event.currentTarget.focus(); onSelect(item.id);
              drag.current = { id: item.id, pointerId: event.pointerId, profileKey, start: { x: event.clientX, y: event.clientY }, moved: false, original: { x: item.x, [axis]: item[axis] }, offset: { x: pointer.x - point.x, y: pointer.y - point.y } };
              svgRef.current.setPointerCapture(event.pointerId);
            }}
            onKeyDown={(event) => {
              if (disabled) return;
              if (event.key === "Enter" || event.key === " ") { event.preventDefault(); onSelect(item.id); return; }
              const patch = nudgePosition(item, view, event.key, event.shiftKey);
              if (patch) { event.preventDefault(); onSelect(item.id); onChange(item.id, patch); }
            }}>
            <title>{`${item.name}: X ${item.x.toFixed(2)} m, Y ${item.y.toFixed(2)} m, Z ${item.z.toFixed(2)} m`}</title>
            <rect x={-width / 2 - 5} y={-height / 2 - 5} width={width + 10} height={height + 10} fill="transparent" />
            <rect className="spatial-map-item-shape" x={-width / 2} y={-height / 2} width={width} height={height} rx="3" fill={active ? "#7050a8" : "#35233f"} stroke={active ? "#e1c7ff" : "#8a659e"} strokeWidth={active ? 2 : 1} opacity={active || item.enabled ? 1 : 0.65} />
            <text x="0" y={-height / 2 - 9} textAnchor="middle" fontSize="10" fill={active ? "#e1c7ff" : "#bda1cf"}>{item.name.slice(0, 22)}</text>
          </g>;
        })}
      </svg>
      <p id={helpId}>Drag an overlay to move it. Arrow keys: 0.01 m; Shift + arrows: 0.1 m. Escape cancels a drag. {view === "top" ? "Height (Y) stays unchanged." : "Depth (Z) stays unchanged."} Positions are relative to each overlay’s anchor; source content and rotation are not shown.</p>
    </Panel>
  );
}
