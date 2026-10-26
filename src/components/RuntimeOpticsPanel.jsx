import React, { useEffect, useRef, useState } from "react";
import { Eye, FileCheck2, FileSearch, RefreshCw, RotateCcw, ShieldCheck } from "lucide-react";
import { Button, Note, Panel } from "./Controls.jsx";

const NUMBERS = [
  { key: "eyeRotation", label: "Eye rotation", min: -5, max: 15, step: 0.1, unit: "degrees per eye outward" },
  { key: "fovZoom", label: "FOV Zoom", min: 0.8, max: 1.1, step: 0.005, unit: "factor" },
  { key: "flatFovZoom", label: "Flat FOV Zoom", min: 0.7, max: 1.15, step: 0.005, unit: "factor" },
];
const show = value => value === null || value === undefined ? "Not set" : typeof value === "object" ? JSON.stringify(value) : String(value);
const reviewedValue = (value, present) => present === false ? "Not set (field absent)" : value === null ? "null" : show(value);

export default function RuntimeOpticsPanel({ api, busy = false }) {
  const [inspection, setInspection] = useState(null);
  const [variant, setVariant] = useState("");
  const [headset, setHeadset] = useState("");
  const [draft, setDraft] = useState({});
  const [review, setReview] = useState(null);
  const [backupId, setBackupId] = useState("");
  const [confirmed, setConfirmed] = useState(false);
  const [working, setWorking] = useState(false);
  const [error, setError] = useState("");
  const [message, setMessage] = useState("");
  const active = useRef(true), inFlight = useRef(false);
  const supported = typeof api?.runtimeOpticsInspect === "function";
  const disabled = !!busy || working || !supported;

  function discardReview() { setReview(null); setConfirmed(false); }
  function accept(result) {
    setInspection(result); setDraft({}); setBackupId(""); discardReview();
    setVariant(result.selected?.variant || ""); setHeadset(result.selected?.headset || "");
    setMessage(result.message || "");
  }
  async function perform(action) {
    if (inFlight.current || busy) return;
    inFlight.current = true; setWorking(true); setError(""); setMessage("");
    try { await action(); }
    catch (failure) { if (active.current) { setError(failure.message || String(failure)); discardReview(); } }
    finally { inFlight.current = false; if (active.current) setWorking(false); }
  }
  useEffect(() => {
    active.current = true;
    let current = true;
    if (supported) {
      setWorking(true); inFlight.current = true;
      api.runtimeOpticsInspect().then(result => { if (current) accept(result); })
        .catch(failure => { if (current) setError(failure.message); })
        .finally(() => { if (current) { setWorking(false); inFlight.current = false; } });
    }
    return () => { current = false; active.current = false; };
  }, [api, supported]);

  function changeTarget(key, value) {
    if (key === "variant") setVariant(value); else setHeadset(value);
    setInspection(previous => previous && { ...previous, available: false, selected: null, values: null, backups: [] });
    setDraft({}); setBackupId(""); setMessage(""); discardReview();
  }
  function edit(key, value) {
    setDraft(previous => ({ ...previous, [key]: value })); discardReview(); setMessage("");
  }
  const read = () => perform(async () => {
    discardReview();
    setInspection(previous => previous && { ...previous, available: false });
    const result = variant && headset ? await api.runtimeOpticsChoose({ variant, headset }) : await api.runtimeOpticsInspect();
    if (active.current) accept(result);
  });
  const preview = () => perform(async () => {
    discardReview();
    const changes = {};
    for (const [key, raw] of Object.entries(draft)) {
      if (raw === "") continue;
      changes[key] = key === "autoEyeRotation" ? raw === "true" : Number(raw);
    }
    const result = await api.runtimeOpticsPreview(changes, inspection.revision);
    if (active.current) setReview(result);
  });
  const previewRestore = () => perform(async () => {
    discardReview();
    const result = await api.runtimeOpticsPreviewRestore(backupId, inspection.revision);
    if (active.current) setReview(result);
  });
  const apply = () => perform(async () => {
    if (!confirmed || !review) throw new Error("Confirm the offline checklist before applying.");
    const token = review.token; discardReview();
    const result = await api.runtimeOpticsApply(token, true);
    if (active.current) accept(result);
  });
  const candidate = inspection?.candidates?.find(item => item.variant === variant);
  const proposed = Object.values(draft).some(value => value !== "");
  const autoValue = draft.autoEyeRotation ?? (inspection?.values?.autoEyeRotation == null ? "" : String(inspection.values.autoEyeRotation));

  return (
    <div className="enhancement-layout runtime-optics-layout">
      <div className="enhancement-main">
        <Panel title="Sboys / SteamVR optics" icon={Eye} action={<span className="badge subtle">OFFLINE CONFIGURATION</span>}>
          <p className="panel-description">Edit the existing Sboys driver configuration for the selected headset. These controls change compositor optics calibration. Saved values do not confirm which driver is active or what the headset displays.</p>
          <Note tone="amber">FOV Zoom and Flat FOV Zoom change distortion mapping; lower values expand its angular mapping. Check stereo alignment and distortion after restarting. These are upstream UI ranges, not verified comfort or optical safety limits.</Note>
          <p className="panel-description">Wide FOV / per-eye maximum FOV is unavailable here until matching device diagnostics establish its bounds. Physical IPD and distortion profiles are not adjusted by these controls.</p>
        </Panel>
        <Panel title="Select the saved configuration" icon={FileSearch} action={<Button icon={RefreshCw} onClick={read} disabled={disabled}>Refresh files</Button>}>
          {!supported && <Note tone="amber">Runtime optics requires the Windows desktop app.</Note>}
          <div className="select-field">
            <label htmlFor="optics-variant">Configuration build</label>
            <select id="optics-variant" value={variant} disabled={disabled} onChange={event => changeTarget("variant", event.target.value)}>
              <option value="">Select a build explicitly</option>
              <option value="neutral">Sboys neutral build</option>
              <option value="pimax">Sboys VENDOR_PIMAX build</option>
            </select>
          </div>
          <div className="select-field">
            <label htmlFor="optics-headset">Headset configuration</label>
            <select id="optics-headset" value={headset} disabled={disabled} onChange={event => changeTarget("headset", event.target.value)}>
              <option value="">Select a headset explicitly</option>
              <option value="dreamAir">Dream Air</option>
              <option value="dreamAirSE">Dream Air SE</option>
            </select>
          </div>
          {candidate && <div className="native-file-path"><FileCheck2 size={19} /><div><strong>{candidate.exists ? "Existing configuration" : "Configuration not found"}</strong><code>{candidate.path}</code></div></div>}
          <p className="selection-hint">The Pimax build above is the Sboys SteamVR driver variant. File presence does not identify the active runtime. Choose the installation you intend to edit.</p>
          <Button onClick={read} disabled={disabled || !variant || !headset}>Read selected configuration</Button>
          {working && <p role="status">Reading or updating optics configuration...</p>}
          {error && <div role="alert"><Note tone="amber">{error}</Note></div>}
          {message && <p className="native-file-message" role="status">{message}</p>}
        </Panel>
        {inspection?.available && <Panel title="Saved values and proposed edits" icon={Eye}>
          <p className="panel-description">Only fields you edit are included in the review. Existing unusual values remain visible and unchanged. Clearing a proposed value leaves that saved field unchanged.</p>
          {NUMBERS.map(field => {
            const saved = inspection.values[field.key];
            const value = draft[field.key] ?? (typeof saved === "number" ? String(saved) : "");
            const unusual = saved != null && (typeof saved !== "number" || saved < field.min || saved > field.max);
            return <div className="text-field" key={field.key}>
              <label htmlFor={`optics-${field.key}`}>{field.label}</label>
              <input id={`optics-${field.key}`} aria-label={`${field.label} draft`} type="number" min={field.min} max={field.max} step={field.step} value={value} placeholder="Not set" disabled={disabled} onChange={event => edit(field.key, event.target.value)} />
              <small className="field-hint">Saved: {show(saved)}. Edit range: {field.min} to {field.max} {field.unit}.{unusual ? " Saved value is outside this editor's range; it will not be normalized." : ""}</small>
            </div>;
          })}
          <div className="select-field">
            <label htmlFor="optics-auto">Automatic eye rotation</label>
            <select id="optics-auto" value={autoValue} disabled={disabled} onChange={event => edit("autoEyeRotation", event.target.value)}>
              <option value="">Leave saved value unchanged</option>
              <option value="false">Off - use fixed eye rotation</option>
              <option value="true">On - use platform canting angle</option>
            </select>
            <small className="field-hint">Saved: {show(inspection.values.autoEyeRotation)}. Fixed eye rotation may not determine the effective angle while automatic rotation is on; the distortion profile also affects it.</small>
          </div>
          <div className="native-review-action"><Button variant="primary" icon={FileCheck2} onClick={preview} disabled={disabled || !proposed}>Review optics changes</Button><Button variant="ghost" onClick={() => { setDraft({}); discardReview(); }} disabled={disabled || !proposed}>Discard edits</Button></div>
        </Panel>}
        {review && <Panel title={review.kind === "restore" ? "Review full-file restore" : "Review optics changes"} icon={FileCheck2} className="native-review-panel" action={<span className="badge subtle">NOT APPLIED</span>}>
          <code className="native-review-path">{review.path}</code>
          <p>{review.selected?.headset === "dreamAirSE" ? "Dream Air SE" : "Dream Air"} - {review.selected?.variant === "pimax" ? "Sboys VENDOR_PIMAX build" : "Sboys neutral build"}</p>
          {!!review.changes.length && <table className="native-change-table"><thead><tr><th>Field</th><th>Saved</th><th>Proposed</th></tr></thead><tbody>{review.changes.map((change, index) => <tr key={`${change.key}-${index}`}><td><code>{change.key}</code></td><td>{change.beforeJson ?? reviewedValue(change.before, change.beforePresent)}</td><td>{change.afterJson ?? reviewedValue(change.after, change.afterPresent)}</td></tr>)}</tbody></table>}
          {!review.changes.length && <p>{review.byteChanges ? "Only the file's original formatting or encoding bytes differ." : "The saved file already matches this review."}</p>}
          <Note tone={review.wholeFile ? "amber" : "muted"}>{review.message}</Note>
          <label className="optics-confirmation"><input type="checkbox" checked={confirmed} disabled={disabled} onChange={event => setConfirmed(event.target.checked)} /> I closed the game, SteamVR and the Sboys driver GUI. I intend to change this configuration and will restart and verify the headset.</label>
          <div className="native-review-action"><Button variant="primary" onClick={apply} disabled={disabled || !confirmed || !review.byteChanges}>{review.kind === "restore" ? "Restore reviewed file" : "Apply reviewed optics"}</Button><Button variant="ghost" onClick={discardReview} disabled={disabled}>Cancel review</Button></div>
        </Panel>}
      </div>
      <div className="settings-stack">
        <Panel title="Pimax native OpenXR" icon={Eye} action={<span className="badge subtle">UNSUPPORTED API</span>}>
          <p className="panel-description">Pimax Play has a Dream Air Wide FOV mode, but this build has no verified native API for it or for these numeric optics controls. Use Pimax Play for its supported mode.</p>
        </Panel>
        <Panel title="Observed active optics" icon={Eye}>
          <strong>Not verified</strong>
          <p className="panel-description">This editor reads and writes a selected settings file. Restarting and observing the actual headset/runtime is still required. A successful save does not prove the selected driver is loaded.</p>
        </Panel>
        <Panel title="Exact file backups" icon={ShieldCheck}>
          <p className="panel-description">Every applied edit keeps the original bytes. Restore reviews the complete file, including the other headset and unrelated settings, and backs up the displaced file first.</p>
          <div className="select-field"><label htmlFor="optics-backup">Backup to restore</label><select id="optics-backup" value={backupId} disabled={disabled || !inspection?.available || !inspection.backups?.length} onChange={event => { setBackupId(event.target.value); discardReview(); }}><option value="">Select a backup</option>{inspection?.backups?.map(backup => <option key={backup.id} value={backup.id}>{new Date(backup.createdAt).toLocaleString()} - {backup.id.slice(-13, -5)}</option>)}</select></div>
          <Button icon={RotateCcw} onClick={previewRestore} disabled={disabled || !inspection?.available || !backupId}>Review full-file restore</Button>
        </Panel>
      </div>
    </div>
  );
}
