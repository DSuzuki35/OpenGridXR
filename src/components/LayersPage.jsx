import React, { useEffect, useRef, useState } from 'react';
import { Layers, RefreshCw, RotateCcw, ShieldCheck } from 'lucide-react';
import { api, isBrowserPreview } from '../api.mjs';
import { Button, Note, Panel, Toggle } from './Controls.jsx';

const validDword = entry => entry.type === 'DWORD' && Number.isInteger(entry.value) && entry.value >= 0 && entry.value <= 0xffffffff;
const backupState = { pending: 'Pending verification', confirmed: 'Verified', failed: 'Failed', restored: 'Restored' };
const registryViewLabel = entry => entry.sharedView || entry.scope === 'HKCU' ? '32/64-bit shared' : `${entry.view}-bit`;

export default function LayersPage() {
  const [inventory, setInventory] = useState({ entries: [], errors: [], backups: [] });
  const [admin, setAdmin] = useState(false);
  const [busy, setBusy] = useState('Reading layer registrations…');
  const [error, setError] = useState('');
  const [message, setMessage] = useState('');
  const [fresh, setFresh] = useState(false);
  const inFlight = useRef(false);
  const mounted = useRef(false);

  async function run(label, operation, success = '') {
    if (inFlight.current) return;
    inFlight.current = true;
    setBusy(label); setError(''); setMessage('');
    try {
      const result = await operation();
      if (mounted.current) {
        setInventory(result); setFresh(true); setMessage(success);
      }
    } catch (failure) {
      if (mounted.current) {
        setError(failure?.message || 'The layer operation could not be completed. Refresh and try again.');
        // A rejected change can still have changed the OS state. Re-read before
        // offering another mutation; never present an optimistic success.
        setFresh(false);
        if (success) {
          try { const result = await api.layersInventory(); if (mounted.current) { setInventory(result); setFresh(true); } }
          catch { /* Keep the original action error and disable stale controls. */ }
        }
      }
    } finally {
      inFlight.current = false;
      if (mounted.current) setBusy('');
    }
  }

  useEffect(() => {
    mounted.current = true;
    run('Reading layer registrations…', () => api.layersInventory());
    return () => { mounted.current = false; };
  }, []);

  const locked = Boolean(busy) || !fresh || isBrowserPreview;
  const enabledCount = inventory.entries.filter(entry => validDword(entry) && entry.value === 0).length;
  return (
    <div className="layers-page" aria-busy={Boolean(busy)}>
      <div className="layers-overview">
        <div><span className="eyebrow">OPENXR REGISTRATIONS</span><h2>Know what is available.</h2><p>Review the layers available to OpenXR apps. Close your sim before making changes.</p></div>
        <div className="layers-count"><strong>{inventory.entries.length}</strong><span>registrations · {enabledCount} enabled</span></div>
      </div>
      <Note>Enabled registration does not mean a layer is loaded in your current session. Explicit layers also require an app request. Changes apply when you next start the app.</Note>
      <Panel title="Layer access" icon={ShieldCheck} action={<Button icon={RefreshCw} disabled={Boolean(busy)} onClick={() => run('Refreshing layer registrations…', () => api.layersInventory(), 'Layer registrations refreshed.')} aria-label="Refresh layers">Refresh</Button>}>
        <Toggle label="Allow administrator changes" checked={admin} onChange={setAdmin} disabled={Boolean(busy) || isBrowserPreview} description="Allow edits for all users (HKLM). Windows asks for approval for each change. OpenGrid XR stays open normally." />
        <p className="layers-help">Current user (HKCU) entries can be edited without administrator approval. HKCU changes apply to both 32-bit and 64-bit registry views. Each change is backed up before it is applied.</p>
      </Panel>
      <div className="layers-feedback" aria-live="polite">
        {busy && <p role="status">{busy}</p>}
        {message && !busy && <p role="status" className="layers-success">{message}</p>}
        {error && <p role="alert" className="layers-error">{error}</p>}
        {inventory.errors.map((item, index) => <p role="alert" className="layers-error" key={index}>{item}</p>)}
      </div>
      <Panel title="Registered layers" icon={Layers}>
        <p className="layers-help">DWORD 0 enables a registration; any nonzero value disables it. The list does not represent loading order.</p>
        {!inventory.entries.length && !busy && <div className="layers-empty"><Layers size={28} /><h3>No layer registrations to show</h3><p>{inventory.errors.length || error ? 'Review the message above, then refresh when ready.' : 'No registrations were found for the current user or all users in either architecture.'}</p></div>}
        <div className="layers-list">
          {inventory.entries.map(entry => {
            const dword = validDword(entry);
            const enabled = dword && entry.value === 0;
            const needsAdmin = entry.scope === 'HKLM';
            const editable = dword && entry.editable;
            return <article className="layers-entry" aria-label={entry.name || entry.manifest} key={entry.id}>
              <div className="layers-entry-heading"><div><h3>{entry.name || 'Unnamed layer'}</h3><div className="layers-tags"><span>{entry.scope === 'HKCU' ? 'Current user' : 'All users'} · {entry.scope}</span><span>{registryViewLabel(entry)}</span><span>{entry.kind === 'explicit' ? 'Explicit' : 'Implicit'}</span></div></div><span className={`layers-state ${enabled ? 'enabled' : ''}`}>{dword ? enabled ? 'Enabled' : 'Disabled' : entry.type === 'DWORD' ? 'Invalid DWORD' : 'Unsupported type'}</span></div>
              <dl className="layers-paths"><div><dt>Manifest</dt><dd>{entry.manifest}</dd></div><div><dt>Library</dt><dd>{entry.library || 'Unavailable'}</dd></div></dl>
              <div className="layers-entry-footer"><div><p>{entry.health || 'Health information unavailable'}</p><small>{entry.type} {String(entry.value ?? '')}</small>{needsAdmin && !admin && <small>Administrator changes are off</small>}</div><Button disabled={locked || !editable || (needsAdmin && !admin)} onClick={() => run(`${enabled ? 'Disabling' : 'Enabling'} ${entry.name || 'layer'}…`, () => api.layersSetEnabled({ id: entry.id, enabled: !enabled, admin: needsAdmin && admin }), `${entry.name || 'Layer'} ${enabled ? 'disabled' : 'enabled'}. Registration verified and previous value backed up.`)}>{editable ? enabled ? 'Disable' : 'Enable' : 'Read only'}</Button></div>
            </article>;
          })}
        </div>
      </Panel>
      <Panel title="Change history" icon={RotateCcw}>
        <p className="layers-help">Restore returns the exact saved value. It is available only while the registration still matches the change made here.</p>
        {!inventory.backups.length && <p className="layers-empty-history">Changes made here will appear with their restore options.</p>}
        <div className="layers-history">{inventory.backups.map(backup => <article key={backup.id} className="layers-backup"><div><h3>{backup.name || 'Layer registration'}</h3><p>{backup.scope} · {registryViewLabel(backup)} · {backup.kind} · {backupState[backup.state] || 'Unknown status'}</p><p className="layers-history-path">{backup.manifest}</p><small>DWORD {backup.before} → {backup.after} · {new Date(backup.createdAt).toLocaleString()}</small></div><Button icon={RotateCcw} aria-label={`Restore ${backup.name || 'layer'}`} disabled={locked || !backup.canRestore || (backup.scope === 'HKLM' && !admin)} onClick={() => run('Restoring saved registration…', () => api.layersRestore({ id: backup.id, admin: backup.scope === 'HKLM' && admin }), 'The exact previous registration value was restored and verified.')}>Restore</Button></article>)}</div>
      </Panel>
    </div>
  );
}
