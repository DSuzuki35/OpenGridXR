import {useEffect, useMemo, useState} from 'react';
import {Aperture} from 'lucide-react';
import {Button, Panel, Range, Toggle} from './Controls.jsx';
import {composeHelmetRaster, streamHelmetAppearance, visorSettings} from '../helmet.mjs';

export function StreamHelmetPreview({profile}) {
  const appearance = useMemo(() => streamHelmetAppearance(profile), [profile.helmet, profile.stream.helmet]);
  const [image, setImage] = useState('');
  const [error, setError] = useState('');
  const [pending, setPending] = useState(false);
  useEffect(() => {
    let active = true;
    setError('');
    if (!appearance.enabled) { setImage(''); setPending(false); return; }
    setPending(true);
    const timer = setTimeout(async () => {
      try {
        const root = `./helmets/${appearance.style}`;
        const result = await composeHelmetRaster({helmet:{...appearance, visor:visorSettings(appearance)},
          shellUrl:`${root}.png`, uvUrl:`${root}-mono-visor-uv.png`, uvVUrl:`${root}-mono-visor-v.png`});
        if (active) { setImage(result.png); setPending(false); }
      } catch (reason) { if (active) { setImage(''); setPending(false); setError(reason.message); } }
    }, 120);
    return () => { active = false; clearTimeout(timer); };
  }, [appearance]);
  if (!appearance.enabled) return null;
  return <>
    {image && <img className="stream-helmet-image" src={image} alt="Stream helmet preview" style={{opacity:(profile.stream.helmet?.opacity ?? 100) / 100}} />}
    {(pending || error) && <span className="stream-helmet-status" role={error ? 'alert' : 'status'}>{error || 'Updating helmet preview…'}</span>}
  </>;
}

export function StreamHelmetControls({profile, changeGroup, available = true}) {
  const helmet = {enabled:false,scale:100,offsetX:0,offsetY:0,opacity:100,...profile.stream.helmet};
  const update = patch => changeGroup('stream', {helmet:{...helmet,...patch}});
  return <Panel title="Stream helmet" icon={Aperture}>
    <Toggle label="Show helmet in stream" checked={helmet.enabled} disabled={!available} onChange={enabled => update({enabled})}
      description="Add the helmet to the preview and saved stream output." />
    <p className="panel-description">{available ? 'Uses the artwork and visor from Helmet. These position, size and opacity controls apply only to the stream. Save to update the output.' : 'Enable the Helmet & visor module in Settings to show the stream helmet.'}</p>
    <div className="stream-helmet-controls">
      <Range label="Stream helmet size" min={50} max={200} value={helmet.scale} disabled={!available || !helmet.enabled} onChange={scale => update({scale})} />
      <Range label="Stream helmet opacity" value={helmet.opacity} disabled={!available || !helmet.enabled} onChange={opacity => update({opacity})} />
      <Range label="Stream helmet horizontal position" min={-100} max={100} value={helmet.offsetX} disabled={!available || !helmet.enabled} onChange={offsetX => update({offsetX})} />
      <Range label="Stream helmet vertical position" min={-100} max={100} value={helmet.offsetY} disabled={!available || !helmet.enabled} onChange={offsetY => update({offsetY})} />
    </div>
    <Button disabled={!available || !helmet.enabled} onClick={() => update({scale:100,offsetX:0,offsetY:0,opacity:100})}>Reset stream helmet placement</Button>
  </Panel>;
}
