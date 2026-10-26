import React, { useEffect, useId, useRef, useState } from 'react';
import { Aperture, ImagePlus, SlidersHorizontal, Trash2 } from 'lucide-react';
import { Button, Note, Panel, Range, Segmented, Toggle } from './Controls.jsx';
import ContourEditor, { TrackArt } from './ContourEditor.jsx';
import { helmetPreviewGeometry, readHelmetGraphic, visorSettings, fitHelmetOpening, composeHelmetRaster } from '../helmet.mjs';
import { isSimplePolygon, polygonArea } from '../model.mjs';
import './HelmetEditor.css';

export function HelmetPreview({ profile, eye = 'left' }) {
  const id = useId().replace(/:/g, '');
  const helmet = profile.helmet;
  const visor = visorSettings(helmet);
  const { crop, contour } = helmetPreviewGeometry(profile, eye);
  const [raster, setRaster] = useState('');
  const [rendering, setRendering] = useState(false);
  const [renderError, setRenderError] = useState('');
  useEffect(() => {
    let live = true;
    if (!helmet.enabled) { setRaster(''); setRendering(false); return; }
    setRendering(true); setRenderError('');
    const timer = setTimeout(async () => {
      try {
        const root = `./helmets/${helmet.style}-${eye}`;
        const result = await composeHelmetRaster({ helmet: { ...helmet, visor: visorSettings(helmet) }, shellUrl: `${root}.png`, uvUrl: `${root}-visor-uv.png`, uvVUrl: `${root}-visor-v.png` });
        if (live) { setRaster(result.png); setRendering(false); }
      } catch (error) { if (live) { setRaster(''); setRenderError(error.message); setRendering(false); } }
    }, 120);
    return () => { live = false; clearTimeout(timer); };
  }, [helmet, eye]);
  const grading = helmet.enabled ? `brightness(${(helmet.visorBrightness ?? 100) / 100}) contrast(${(helmet.visorContrast ?? 100) / 100}) saturate(${(helmet.visorSaturation ?? 100) / 100})` : undefined;
  return <div className="helmet-art helmet-render helmet-editor-render" data-helmet-enabled={helmet.enabled} data-rendering={rendering} data-eye={eye} data-visor-enabled={visor.enabled} data-mirrored={visor.mirror}>
    <svg viewBox="0 0 800 450" role="img" aria-label={`${eye} eye helmet appearance preview`}>
      <defs>
        <clipPath id={`${id}-crop`}><rect {...crop} /></clipPath>
        <clipPath id={`${id}-contour`}>{contour ? <polygon points={contour} /> : <rect width="800" height="450" />}</clipPath>
      </defs>
      <rect width="800" height="450" fill="#030407" />
      <g clipPath={`url(#${id}-crop)`}><g clipPath={`url(#${id}-contour)`}>
        <g className="helmet-scene-grade" style={{ filter: grading }}><TrackArt id={`${id}-track`} /></g>
      </g></g>
    </svg>
    {helmet.enabled && raster && <img className="helmet-composite" src={raster} data-source={`${helmet.style}-${eye}.png`} alt={`Curved ${helmet.style} ${eye} eye helmet and visor composite`} />}
    {renderError && <span className="helmet-preview-error" role="alert">{renderError}</span>}
  </div>;
}

export default function HelmetEditor({ profile, onHelmetChange, onMaskChange, onViewChange }) {
  const [section, setSection] = useState('visor');
  const [eye, setEye] = useState('left');
  const [uploading, setUploading] = useState(false);
  const [notice, setNotice] = useState('');
  const [fitting, setFitting] = useState(false);
  const [fitNotice, setFitNotice] = useState('');
  const uploadSequence = useRef(0);
  const fitSequence = useRef(0);
  const latestProfile = useRef(profile);
  latestProfile.current = profile;
  const upload = useRef(null);
  useEffect(() => {
    uploadSequence.current++;
    fitSequence.current++;
    setNotice('');
    setUploading(false);
    setFitting(false);
    setFitNotice('');
    return () => { uploadSequence.current++; fitSequence.current++; };
  }, [profile.id]);
  const helmet = profile.helmet;
  const visor = visorSettings(helmet);
  const changeVisor = (change) => onHelmetChange({ visor: { ...visor, ...change } });
  const latestChangeVisor = useRef(changeVisor);
  latestChangeVisor.current = changeVisor;
  const fitOpening = async () => {
    const sequence = ++fitSequence.current;
    setFitting(true); setFitNotice('');
    try {
      const result = await fitHelmetOpening(helmet, profile.mask.padding);
      if (sequence !== fitSequence.current) return;
      if (latestProfile.current.helmet !== helmet || latestProfile.current.mask.padding !== profile.mask.padding) {
        setFitNotice('The helmet changed during fitting. Fit the opening again to use the new settings.'); return;
      }
      if (!Object.values(result.contours).every(points => isSimplePolygon(points) && polygonArea(points) >= .04))
        throw new Error('This fit leaves too little room. Increase the opening before fitting the crop.');
      onMaskChange({ enabled: true, linked: false, left: result.contours.left, right: result.contours.right, name: 'Stereo helmet aperture' });
      onViewChange({ crop: result.crop });
      setFitNotice('Both eyes fitted with a conservative margin. Refit after changing the interior fit, shell opacity or motion settings.');
    } catch (error) { if (sequence === fitSequence.current) setFitNotice(error.message); }
    finally { if (sequence === fitSequence.current) setFitting(false); }
  };
  const setGraphic = async (file) => {
    if (!file) return;
    const sequence = ++uploadSequence.current;
    setUploading(true);
    setNotice('');
    try {
      const graphic = await readHelmetGraphic(file);
      if (sequence === uploadSequence.current) {
        latestChangeVisor.current({ graphic });
        setNotice('Graphic added. It is embedded in this profile and included in exports.');
      }
    } catch (error) {
      if (sequence === uploadSequence.current) setNotice(error.message);
    } finally { if (sequence === uploadSequence.current) setUploading(false); }
  };
  const crop = profile.view.crop;
  const changeCrop = (side, value) => {
    const opposite = { left: 'right', right: 'left', top: 'bottom', bottom: 'top' }[side];
    onViewChange({ crop: { ...crop, [side]: Math.min(value, 70 - crop[opposite]) } });
  };
  return <div className="helmet-editor-layout">
    <div className="helmet-editor-main">
      <Panel title="Inside your helmet" icon={Aperture} action={<span className="badge">DESKTOP PREVIEW</span>}>
        <HelmetPreview profile={profile} eye={eye} />
        <div className="helmet-editor-preview-footer">
          <Segmented label="Helmet preview eye" value={eye} onChange={setEye} options={[{ value: 'left', label: 'Left eye' }, { value: 'right', label: 'Right eye' }]} />
          <span>{helmet.enabled ? 'Helmet on' : 'Helmet off'} · {profile.mode === 'driving' ? 'Driving view' : 'Full streaming view'}</span>
        </div>
        <p className="panel-description helmet-editor-caption">Print follows the actual curved Blender visor, projected separately for each eye. Saving prepares the finished PNG pair; no helmet mesh is rendered during a race. This is a physically inspired fit guide, not a measured optical model of your headset.</p>
      </Panel>
      <Panel title="View opening & crop" icon={SlidersHorizontal}>
        <p className="panel-description">These are the same crop and contour settings as View & crop. They reduce the visible image; they do not change the headset’s optical field of view.</p>
        {profile.mode === 'streaming' && <Note>Driving crop and contour are saved but inactive in streaming mode.</Note>}
        <Button disabled={!helmet.enabled || fitting} onClick={fitOpening}>{fitting ? 'Fitting both eyes…' : 'Fit crop to visor opening'}</Button>
        <p className="helmet-editor-muted">Fits a conservative contour and shared crop to the stereo interior, including padding and motion allowance. Translucent areas stay visible. Existing settings change only when you fit.</p>
        {fitNotice && <p className="helmet-editor-muted" role="status">{fitNotice}</p>}
        <div className="helmet-editor-grid">
          {['left', 'right', 'top', 'bottom'].map(side => <Range key={side} label={`${side[0].toUpperCase() + side.slice(1)} crop`} value={crop[side]} max={Math.min(40, 70 - crop[{ left: 'right', right: 'left', top: 'bottom', bottom: 'top' }[side]])} onChange={value => changeCrop(side, value)} />)}
        </div>
        <Toggle label="Use contour in driving" checked={profile.mask.enabled} onChange={enabled => onMaskChange({ enabled })} bindingAction="mask.enabled" />
        <details className="helmet-editor-details">
          <summary>Shape the opening point by point</summary>
          <ContourEditor profile={profile} onMaskChange={onMaskChange} compact />
        </details>
      </Panel>
    </div>
    <div className="settings-stack">
      <Panel title="Helmet studio">
        <Toggle label="Show helmet" description="Save and preview your helmet appearance" checked={helmet.enabled} onChange={enabled => onHelmetChange({ enabled })} bindingAction="helmet.enabled" />
        <div className="panel-divider" />
        <Segmented label="Helmet editor section" value={section} onChange={setSection} options={[{ value: 'fit', label: 'Interior' }, { value: 'visor', label: 'Sunstrip' }, { value: 'graphic', label: 'Graphic' }, { value: 'tint', label: 'Visor' }]} />
        {!helmet.enabled && <p className="helmet-editor-muted">Switch on Show helmet to preview your changes.</p>}
        {section === 'fit' && <div className="helmet-editor-controls">
          <Segmented label="Helmet style" value={helmet.style} onChange={style => onHelmetChange({ style })} options={[{ value: 'formula', label: 'Formula' }, { value: 'gt', label: 'GT' }, { value: 'open', label: 'Open' }]} />
          <Range label="Shell opacity" value={helmet.opacity} onChange={opacity => onHelmetChange({ opacity })} />
          <Range label="Guide scale" value={helmet.scale} min={50} max={150} onChange={scale => onHelmetChange({ scale })} />
          <Range label="Horizontal position" value={helmet.offsetX} min={-100} max={100} onChange={offsetX => onHelmetChange({ offsetX })} />
          <Range label="Vertical position" value={helmet.offsetY} min={-100} max={100} onChange={offsetY => onHelmetChange({ offsetY })} />
          <div className="panel-divider" />
          <Range label="Head movement response" value={Math.round((helmet.headMotion ?? 0) * 100)} onChange={value => onHelmetChange({ headMotion: value / 100 })} hint="Optional subtle helmet sway from head movement. Off at zero." />
          <Range label="Vehicle movement response" value={Math.round((helmet.vehicleMotion ?? 0) * 100)} onChange={value => onHelmetChange({ vehicleMotion: value / 100 })} hint="Optional subtle sway from live vehicle telemetry. Off at zero." />
        </div>}
        {section === 'visor' && <div className="helmet-editor-controls">
          <Toggle label="Upper sunstrip" checked={visor.enabled} onChange={enabled => changeVisor({ enabled })} />
          <div className="helmet-editor-presets"><Button onClick={() => changeVisor({ enabled: true, color: '#f4f0e5', textColor: '#c41b28', opacity: 92, text: visor.text || 'OPEN GRID', textSize: 44, textWidth: 52, height: 30, curve: 12, textX: 0, textY: 0, textOpacity: 100, softness: .6, mirror: true })}>White / red</Button><Button onClick={() => changeVisor({ enabled: true, color: '#101219', textColor: '#f8f7f2', opacity: 86, text: visor.text || 'OPEN GRID', textSize: 44, textWidth: 52, height: 30, curve: 12, textX: 0, textY: 0, textOpacity: 100, softness: .6, mirror: true })}>Dark / white</Button></div>
          <Range label="Sunstrip depth" value={visor.height} min={5} max={55} onChange={height => changeVisor({ height })} />
          <Range label="Sunstrip curve" value={visor.curve} min={-25} max={25} onChange={curve => changeVisor({ curve })} />
          <div className="helmet-editor-color-row"><label>Strip color<input type="color" aria-label="Sunstrip color" value={visor.color} onChange={event => changeVisor({ color: event.target.value })} /></label><label>Letter color<input type="color" aria-label="Sunstrip text color" value={visor.textColor} onChange={event => changeVisor({ textColor: event.target.value })} /></label></div>
          <Range label="Sunstrip opacity" value={visor.opacity} onChange={opacity => changeVisor({ opacity })} hint="Lower values let the track show through." />
          <label className="helmet-editor-text">Sponsor or personal text<input aria-label="Sunstrip text" value={visor.text} maxLength={80} placeholder="YOUR TEAM" onChange={event => changeVisor({ text: event.target.value.replace(/[\u0000-\u001f\u007f]/g, '') })} /></label>
          <Range label="Letter size" value={visor.textSize} min={10} max={60} unit="px" onChange={textSize => changeVisor({ textSize })} />
          <Range label="Letter width" value={visor.textWidth} min={10} max={100} onChange={textWidth => changeVisor({ textWidth })} />
          <Toggle label="Mirror lettering & graphic" description="Read the outside print from inside the helmet" checked={visor.mirror} onChange={mirror => changeVisor({ mirror })} />
          <details className="helmet-editor-details"><summary>Print position & softness</summary><div className="helmet-editor-controls">
            <Range label="Letter horizontal position" value={visor.textX} min={-50} max={50} onChange={textX => changeVisor({ textX })} />
            <Range label="Letter vertical position" value={visor.textY} min={-25} max={25} onChange={textY => changeVisor({ textY })} />
            <Range label="Print opacity" value={visor.textOpacity} onChange={textOpacity => changeVisor({ textOpacity })} />
            <Range label="Print softness" value={visor.softness} min={0} max={8} step={.1} unit="px" onChange={softness => changeVisor({ softness })} />
          </div></details>
        </div>}
        {section === 'tint' && <div className="helmet-editor-controls">
          <div className="helmet-editor-color-row"><label>Visor tint color<input type="color" aria-label="Visor tint color" value={helmet.visorTintColor ?? '#142035'} onChange={event => onHelmetChange({ visorTintColor: event.target.value })} /></label></div>
          <Range label="Whole visor tint" value={helmet.visorTint} onChange={visorTint => onHelmetChange({ visorTint })} hint="Colored transparency across the curved visor; independent of the upper strip." />
          <Range label="Scene brightness" value={helmet.visorBrightness ?? 100} min={50} max={150} onChange={visorBrightness => onHelmetChange({ visorBrightness })} />
          <Range label="Scene contrast" value={helmet.visorContrast ?? 100} min={50} max={150} onChange={visorContrast => onHelmetChange({ visorContrast })} />
          <Range label="Scene saturation" value={helmet.visorSaturation ?? 100} min={0} max={150} onChange={visorSaturation => onHelmetChange({ visorSaturation })} />
          <p className="helmet-editor-muted">Scene adjustments use the D3D11 image-processing path, including the frame-generation bridge. Native D3D12 games are not supported for these three adjustments. Save and restart the XR session. Clean stream output stays ungraded.</p>
        </div>}
        {section === 'graphic' && <div className="helmet-editor-controls">
          <p className="panel-description">Add your own logo or sponsor strip. PNG, JPEG or WebP, up to 4 MB. Images are resized and embedded in the profile.</p>
          {!visor.enabled && <p className="helmet-editor-muted">Enable Upper sunstrip in the Sunstrip tab to show the graphic.</p>}
          <input ref={upload} type="file" accept="image/png,image/jpeg,image/webp" className="helmet-editor-file" aria-label="Upload visor graphic" onChange={event => { setGraphic(event.target.files?.[0]); event.target.value = ''; }} />
          <Button icon={ImagePlus} disabled={uploading} onClick={() => upload.current?.click()}>{uploading ? 'Preparing image…' : visor.graphic ? 'Replace graphic' : 'Upload graphic'}</Button>
          {visor.graphic && <>
            <div className="helmet-editor-graphic-thumb"><img src={visor.graphic} alt="Your visor graphic" /></div>
            <Range label="Graphic width" value={visor.graphicWidth} min={5} max={100} onChange={graphicWidth => changeVisor({ graphicWidth })} />
            <Range label="Graphic horizontal position" value={visor.graphicX} min={-50} max={50} onChange={graphicX => changeVisor({ graphicX })} />
            <Range label="Graphic vertical position" value={visor.graphicY} min={-25} max={25} onChange={graphicY => changeVisor({ graphicY })} />
            <Button icon={Trash2} onClick={() => { uploadSequence.current++; setUploading(false); changeVisor({ graphic: '' }); setNotice('Graphic removed.'); }}>Remove graphic</Button>
          </>}
          {notice && <p className="helmet-editor-muted" role="status">{notice}</p>}
        </div>}
      </Panel>
    </div>
  </div>;
}
