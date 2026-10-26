import React, { useEffect, useState } from "react";
import { Download, RefreshCw } from "lucide-react";
import { api } from "../api.mjs";
import { Button, Panel } from "./Controls.jsx";

export default function UpdatesPanel({ details, dirty, busy }) {
  const [state, setState] = useState(null);
  const [working, setWorking] = useState(false);
  const [error, setError] = useState("");
  useEffect(() => {
    let live = true;
    api.updateStatus().then(value => { if (live) setState(value); }).catch(e => { if (live) setError(e.message); });
    const unsubscribe = api.onUpdateStatus(value => { if (live) setState(value); });
    return () => { live = false; unsubscribe?.(); };
  }, []);
  async function run(operation) {
    setWorking(true); setError("");
    try { setState(await operation()); } catch (e) { setError(e.message); }
    finally { setWorking(false); }
  }
  if (!details && !state?.prompt && !error) return null;
  const disabled = working || !!busy;
  return <div className={`updates-area ${details ? "updates-details" : "updates-notification"}`} role="region" aria-label="Application updates">
    <Panel title="Application updates" icon={Download}>
      <p role="status" className="panel-description">{state?.message || "Reading update status…"}</p>
      {details && state && <p className="panel-description">Installed: {state.currentVersion}. Stable releases from GitHub. You can decline every update.</p>}
      {error && <p role="alert">{error}</p>}
      {state?.phase === "downloading" && <progress aria-label="Update download" value={state.percent} max="100" />}
      <div className="runtime-actions">
        {state?.phase === "available" && <>
          <Button variant="primary" icon={Download} disabled={disabled} onClick={() => run(() => api.updateChoice("download"))}>Download update</Button>
          <Button disabled={disabled} onClick={() => run(() => api.updateChoice("later"))}>Later</Button>
          <Button variant="ghost" disabled={disabled} onClick={() => run(() => api.updateChoice("skip"))}>Skip this version</Button>
        </>}
        {state?.phase === "downloaded" && <>
          <Button variant="primary" disabled={disabled || dirty} onClick={() => run(() => api.installUpdate())}>Install and restart</Button>
          <Button disabled={disabled} onClick={() => run(() => api.updateChoice("later"))}>Later</Button>
          {dirty && <span>Save your profile before restarting.</span>}
        </>}
        {details && state && !["unsupported", "downloading", "downloaded", "installing"].includes(state.phase) &&
          <Button icon={RefreshCw} disabled={disabled || state.phase === "checking"} onClick={() => run(() => api.checkUpdates())}>Check for updates</Button>}
      </div>
    </Panel>
  </div>;
}
