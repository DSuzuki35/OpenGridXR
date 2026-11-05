import React, {useEffect, useState} from "react";
import {Boxes} from "lucide-react";
import {Button, Panel, Toggle, Note} from "./Controls.jsx";
import {MODULES, moduleDefaults} from "../modules.mjs";

export default function ModulesPanel({enabled, error, saving, blocked, onApply}) {
  const [draft, setDraft] = useState(enabled);
  useEffect(() => setDraft(enabled), [enabled]);
  const changed = MODULES.some(({id}) => draft[id] !== enabled[id]);
  function toggle(id, checked) {
    setDraft(current => {
      const next = {...current, [id]:checked};
      if (id === "benchmark" && checked) next.telemetry = true;
      if (id === "telemetry" && !checked) next.benchmark = false;
      return next;
    });
  }
  function preset(ids) {
    setDraft(Object.fromEntries(MODULES.map(({id}) => [id,ids.includes(id)])));
  }
  return <Panel title="Modules" icon={Boxes} className="modules-panel">
    <p className="panel-description">Choose what this app loads and shows. Applies to all profiles; your saved feature settings stay intact.</p>
    <p className="module-core">Always available: Session, View & crop, Profiles and Setup. One registered OpenXR layer.</p>
    <div className="module-presets">
      <Button disabled={saving} onClick={() => preset([])}>Core only</Button>
      <Button disabled={saving} onClick={() => preset(["helmet","overlay","bindings","sharpening"])}>Driving essentials</Button>
      <Button disabled={saving} onClick={() => setDraft(moduleDefaults())}>All modules</Button>
    </div>
    <div className="module-grid">
      {MODULES.map(({id,label,description}) => <div className="module-card" key={id}>
        <Toggle label={label} description={description} checked={draft[id]} disabled={saving} onChange={checked => toggle(id,checked)} />
      </div>)}
    </div>
    {error && <Note tone="amber">{error}</Note>}
    {blocked && <Note>Save or discard your profile edits before applying modules.</Note>}
    <div className="module-presets">
      <Button variant="primary" disabled={!changed || blocked || saving} onClick={() => onApply(draft)}>{saving ? "Applying…" : "Apply modules"}</Button>
      {changed && <Button disabled={saving} onClick={() => setDraft(enabled)}>Discard module changes</Button>}
    </div>
    <p className="field-hint">Disabled modules stop their background services and are excluded from the next game session. Restart the game after changing modules. Pages load when opened; restart this app to release previously loaded page code.</p>
  </Panel>;
}
