import { useEffect, useState } from 'react';
import { Download, FolderOpen, Gamepad2, RefreshCw } from 'lucide-react';
import { api, isBrowserPreview } from '../api.mjs';
import { Button, Note, Panel } from './Controls.jsx';

export default function SimHubBridgePanel({ disabled = false }) {
  const [status, setStatus] = useState(null);
  const [pending, setPending] = useState('loading');
  const [error, setError] = useState('');
  const message = reason => String(reason?.message || reason).replace(/^Error invoking remote method '[^']+': (?:Error: )?/, '');
  useEffect(() => {
    let disposed = false;
    api.simHubBridgeStatus().then(value => { if (!disposed) setStatus(value); })
      .catch(reason => { if (!disposed) setError(message(reason)); })
      .finally(() => { if (!disposed) setPending(''); });
    return () => { disposed = true; };
  }, []);
  async function run(name, callback) {
    if (pending) return;
    setPending(name); setError('');
    try {
      const next = await callback();
      if (next) setStatus(next);
    } catch (reason) {
      setError(message(reason));
      try { setStatus(await api.simHubBridgeStatus()); } catch { /* Keep the operation error visible. */ }
    } finally { setPending(''); }
  }
  const locked = disabled || !!pending || isBrowserPreview;
  const ready = status?.available && status?.directory && status.running === false && !status.error;
  return <Panel title="SimHub bridge" icon={Gamepad2}>
    <p className="panel-description">
      Mirror SimHub LED and button colors on GSI and Ascher Artura Ultimate 3D wheels.
      Dashboard displays use their SimHub URL separately.
    </p>
    <div aria-live="polite" aria-busy={!!pending}>
      <p className="panel-description" style={{overflowWrap:'anywhere'}}>
        {status?.directory ? <>SimHub folder: <strong>{status.directory}</strong></> : 'Choose your SimHub installation if it is not detected automatically.'}
      </p>
      <p className="panel-description">{pending === 'install' ? 'Installing the SimHub bridge…' : status?.message || (error ? '' : 'Checking SimHub…')}</p>
    </div>
    {(error || status?.error) && <div role="alert"><Note tone="amber">{error || status.error}</Note></div>}
    <div className="runtime-actions" style={{flexWrap:'wrap',gap:8}}>
      <Button variant="primary" icon={Download} disabled={locked || !ready || status?.current}
        onClick={() => run('install', () => api.installSimHubBridge())}>
        {pending === 'install' ? 'Installing…' : status?.current ? 'Bridge up to date' : status?.installed ? 'Update SimHub bridge' : 'Install SimHub bridge'}
      </Button>
      <Button icon={FolderOpen} disabled={locked} onClick={() => run('choose', () => api.chooseSimHubFolder())}>Choose SimHub folder</Button>
      <Button icon={RefreshCw} disabled={locked} onClick={() => run('refresh', () => api.simHubBridgeStatus())}>Refresh bridge status</Button>
    </div>
    <Note>
      Close SimHub before installing. Afterwards, start SimHub, enable <strong>OpenGrid XR Lights</strong> in Plugins,
      then enable <strong>Mirror my wheel lights in OpenGrid XR</strong>. Choose Connected wheel for hardware or Virtual wheel for effects without a physical device.
    </Note>
    <details>
      <summary>Supported wheels and manual installation</summary>
      <p className="panel-description">GSI Hyper P1, Hyper SL, GXL V2, GT-MAX 32, FPE2 and Ascher Artura Ultimate.</p>
      <p className="panel-description">For a restricted SimHub folder, copy oVRlay.SimHubLeds.dll into it manually and approve Windows administrator access if requested.</p>
      <Button icon={FolderOpen} disabled={locked} onClick={() => run('folder', () => api.openSimHubPluginFolder())}>Open bridge files</Button>
    </details>
  </Panel>;
}
