import React, { useEffect, useRef, useState } from "react";
import { api } from "../api.mjs";
import { steeringAxes, steeringInputDefaults } from "../spatial-overlays.mjs";
import { Button, Note, Range, Segmented, Toggle } from "./Controls.jsx";

export default function SteeringInput({ value, onChange }) {
  const settings = { ...steeringInputDefaults, ...value };
  const [snapshot, setSnapshot] = useState({ devices: [], samples: {} });
  const [listening, setListening] = useState(false);
  const [message, setMessage] = useState("");
  const capture = useRef(null), latest = useRef({ settings, onChange });
  latest.current = { settings, onChange };
  useEffect(() => {
    if (settings.source !== "device") return;
    let disposed = false, inFlight = false;
    const poll = async () => {
      if (disposed || inFlight || document.hidden) return;
      inFlight = true;
      try {
        const next = await api.steeringInputStatus({ discover: true });
        if (disposed) return;
        setSnapshot(next);
        const active = capture.current;
        if (active) {
          if (Date.now() - active.started > 15000) {
            capture.current = null; setListening(false); setMessage("No axis movement detected. Try again or choose the device and axis below.");
          } else {
            let best = null;
            for (const device of next.devices || []) for (const axis of device.axes || []) {
              const sample = next.samples?.[device.id]?.[axis], key = `${device.id}:${axis}`;
              if (!Number.isFinite(sample)) continue;
              if (active.baseline[key] === undefined) active.baseline[key] = sample;
              const delta = Math.abs(sample - active.baseline[key]);
              if (delta > .15 && (!best || delta > best.delta)) best = { device, axis, delta };
            }
            if (best) {
              capture.current = null; setListening(false);
              latest.current.onChange({ ...latest.current.settings, deviceId: best.device.id, axis: best.axis });
              setMessage(`Assigned ${best.device.name} · ${best.axis.toUpperCase()}. Check the direction below, then save the profile.`);
            }
          }
        }
      } catch (error) { if (!disposed) setSnapshot({ devices: [], samples: {}, error: error.message }); }
      finally { inFlight = false; }
    };
    const visibility = () => {
      if (document.hidden) { capture.current = null; setListening(false); void api.steeringInputStopDiscovery().catch(() => {}); }
      else void poll();
    };
    void poll(); const timer = setInterval(poll, 100);
    document.addEventListener("visibilitychange", visibility);
    return () => {
      disposed = true; clearInterval(timer); capture.current = null;
      document.removeEventListener("visibilitychange", visibility);
      void api.steeringInputStopDiscovery().catch(() => {});
    };
  }, [settings.source]);
  const change = patch => onChange({ ...settings, ...patch });
  const device = snapshot.devices?.find(item => item.id === settings.deviceId);
  const axes = device?.axes || steeringAxes;
  const sample = snapshot.samples?.[settings.deviceId]?.[settings.axis];
  const fresh = Number.isFinite(sample);
  const degrees = fresh ? -sample * settings.lockDegrees / 2 * (settings.invert ? -1 : 1) : 0;
  return <section className="steering-input-settings" aria-label="Steering input">
    <h3>Steering input</h3>
    <Segmented label="Steering input source" value={settings.source} onChange={source => { setListening(false); setMessage(""); change({ source }); }}
      options={[{ value: "telemetry", label: "Game telemetry" }, { value: "device", label: "Assigned axis" }]} />
    <p>Shared by steering-following wheels and dashboards in this profile.</p>
    {settings.source === "device" && <>
      <div className="placement-actions">
        <Button onClick={() => {
          if (listening) { capture.current = null; setListening(false); return; }
          const baseline = {};
          for (const [id, values] of Object.entries(snapshot.samples || {})) for (const [axis, sample] of Object.entries(values))
            if (Number.isFinite(sample)) baseline[`${id}:${axis}`] = sample;
          capture.current = { baseline, started: Date.now() }; setListening(true); setMessage("");
        }}>{listening ? "Cancel axis assignment" : "Assign steering axis"}</Button>
      </div>
      <p role="status">{listening ? "Turn your wheel slowly. Leave pedals and other controls still." : message}</p>
      <div className="steering-input-selectors">
        <label>Wheel base<select aria-label="Steering wheel base" value={settings.deviceId} onChange={event => {
          const selected = snapshot.devices.find(item => item.id === event.target.value);
          change({ deviceId: event.target.value, axis: selected?.axes?.includes(settings.axis) ? settings.axis : selected?.axes?.[0] || "x" });
        }}>
          <option value="">Choose a wheel base</option>
          {settings.deviceId && !device && <option value={settings.deviceId}>Assigned device · disconnected</option>}
          {(snapshot.devices || []).filter(item => item.axes?.length).map(item => <option key={item.id} value={item.id}>{item.name}</option>)}
        </select></label>
        <label>Axis<select aria-label="Steering axis" value={settings.axis} onChange={event => change({ axis: event.target.value })}>
          {!axes.includes(settings.axis) && <option value={settings.axis}>{settings.axis.toUpperCase()} · unavailable</option>}
          {axes.map(axis => <option key={axis} value={axis}>{axis.toUpperCase()}</option>)}
        </select></label>
      </div>
      <Range label="Steering lock" value={settings.lockDegrees} onChange={lockDegrees => change({ lockDegrees: Math.round(lockDegrees) })} min={90} max={2520} step={1} unit="°"
        hint="Total travel from left stop to right stop. Match your wheelbase setting: 900° means ±450°. This does not change the base’s hardware lock." />
      <Toggle label="Invert steering axis" checked={settings.invert} onChange={invert => change({ invert })}/>
      <div className="steering-input-meter">
        <meter aria-label="Steering angle preview" min={-settings.lockDegrees / 2} max={settings.lockDegrees / 2} value={degrees}/>
        <output>{fresh ? `${Math.abs(degrees).toFixed(1)}° ${Math.abs(degrees) < .5 ? "· centered" : degrees > 0 ? "left" : "right"}` : "Waiting for assigned axis"}</output>
      </div>
      {snapshot.error && <Note>{snapshot.error}</Note>}
      <p>Save profile to apply. No game telemetry is required for steering. A disconnected or stale axis centers the virtual wheel. Per-car placement still uses game telemetry.</p>
    </>}
  </section>;
}
