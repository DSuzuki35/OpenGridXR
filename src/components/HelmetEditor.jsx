import {usesFullRenderArea} from "../model.mjs";
import React, { useEffect, useId, useRef, useState } from 'react';
import { Aperture, ImagePlus, SlidersHorizontal, Trash2 } from 'lucide-react';
import { Button, Note, Panel, Range, Segmented, Toggle } from './Controls.jsx';
import ContourEditor, { TrackArt } from './ContourEditor.jsx';
import { helmetPreviewGeometry, readHelmetGraphic, visorSettings, visorFonts, fitHelmetOpening, composeHelmetRaster } from '../helmet.mjs';
import { isSimplePolygon, polygonArea } from '../model.mjs';
import {api} from '../api.mjs';
import './HelmetEditor.css';
import {colorCorrectionDefaults,resolvedColorPreset,colorPresetIsIdentity} from '../color-correction.mjs';
import SceneColorFilter from './SceneColorFilter.jsx';

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
        const root = `./helmets/${helmet.style}`;
        const result = await composeHelmetRaster({ helmet: { ...helmet, visor: visorSettings(helmet) }, shellUrl: `${root}.png`, uvUrl: `${root}-mono-visor-uv.png`, uvVUrl: `${root}-mono-visor-v.png` });
        if (live) { setRaster(result.png); setRendering(false); }
      } catch (error) { if (live) { setRaster(''); setRenderError(error.message); setRendering(false); } }
    }, 120);
    return () => { live = false; clearTimeout(timer); };
  }, [helmet]);
  const color = profile.colorCorrection ?? colorCorrectionDefaults();
  const preset = resolvedColorPreset(color);
  const grading = color.enabled && !colorPresetIsIdentity(preset);
  return <div className="helmet-art helmet-render helmet-editor-render" data-helmet-enabled={helmet.enabled} data-rendering={rendering} data-eye={eye} data-visor-enabled={visor.enabled} data-mirrored={visor.mirror}>
    <svg viewBox="0 0 800 450" role="img" aria-label="2D helmet appearance preview">
      <defs>
        <SceneColorFilter id={`${id}-color`} preset={preset}/>
        <clipPath id={`${id}-crop`}><rect {...crop} /></clipPath>
        <clipPath id={`${id}-contour`}>{contour ? <polygon points={contour} /> : <rect width="800" height="450" />}</clipPath>
      </defs>
      <rect width="800" height="450" fill="#030407" />
      <g clipPath={`url(#${id}-crop)`}><g clipPath={`url(#${id}-contour)`}>
        <g className="helmet-scene-grade" filter={grading ? `url(#${id}-color)` : undefined}><TrackArt id={`${id}-track`} /></g>
      </g></g>
    </svg>
    {helmet.enabled && raster && <img className="helmet-composite" src={raster} data-source={`${helmet.style}.png`} alt={`Flat 2D ${helmet.style} helmet and visor composite`} />}
    {renderError && <span className="helmet-preview-error" role="alert">{renderError}</span>}
  </div>;
}

export default function HelmetEditor({ profile, onHelmetChange, onMaskChange, onViewChange, onOpeningCommit, onPreparingChange }) {
  const [section, setSection] = useState('visor');
  const [uploading, setUploading] = useState(false);
  const [notice, setNotice] = useState('');
  const [fitting, setFitting] = useState(false);
  const [fitNotice, setFitNotice] = useState('');
  const [pendingOpening, setPendingOpening] = useState(null);
  const commitOpening = useRef(onOpeningCommit);
  commitOpening.current = onOpeningCommit;
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
    setPendingOpening(null);
    return () => { uploadSequence.current++; fitSequence.current++; };
  }, [profile.id]);
  const helmet = profile.helmet;
  const openingHelmet = pendingOpening ? { ...helmet, ...pendingOpening } : helmet;
  const visor = visorSettings(openingHelmet);
  const changeVisor = change => changeOpening({ visor: { ...visor, ...change } });
  const changeOpening = patch => {
    onPreparingChange(true);
    setPendingOpening(current => ({ ...current, ...patch }));
  };
  useEffect(() => {
    if (!pendingOpening) return;
    let live = true;
    onPreparingChange(true); setFitNotice('Adjusting opening and crop…');
    const timer = setTimeout(async () => {
      try {
        const result = await fitHelmetOpening({ ...helmet, ...pendingOpening }, profile.mask.padding);
        if (!live) return;
        if (!Object.values(result.contours).every(points => isSimplePolygon(points) && polygonArea(points) >= .04))
          throw new Error('This opening is too small. Increase its height or width.');
        commitOpening.current(profile.id, pendingOpening, result);
        setPendingOpening(null);
        setFitNotice('Opening and crop updated together. Save to apply in the headset.');
      } catch (error) { if (live) { setPendingOpening(null); setFitNotice(error.message); } }
      finally { if (live) onPreparingChange(false); }
    }, 250);
    return () => { live = false; clearTimeout(timer); onPreparingChange(false); };
  }, [pendingOpening, helmet, profile.id, profile.mask.padding, onPreparingChange]);
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
      onMaskChange({ enabled: true, linked: true, left: result.contours.left, right: result.contours.right, name: '2D helmet aperture' });
      onViewChange({ crop: result.crop });
      setFitNotice('Shared 2D opening fitted with a conservative margin. Geometry controls keep this fit up to date.');
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
        <HelmetPreview profile={pendingOpening ? { ...profile, helmet: openingHelmet, mask: { ...profile.mask, enabled: false }, view: { ...profile.view, crop: { left: 0, right: 0, top: 0, bottom: 0 } } } : profile} />
        <div className="helmet-editor-preview-footer">
          <span className="badge">FLAT 2D OVERLAY</span>
          <span>{helmet.enabled ? 'Helmet on' : 'Helmet off'} · {usesFullRenderArea(profile) ? 'Full render area for stream' : 'Game view crop'}</span>
        </div>
        <Range label="Vertical visor opening" value={openingHelmet.openingAngle ?? 60} min={25} max={100} step={.5} unit="° ≈" onChange={openingAngle => changeOpening({ openingAngle, openingWidth: openingHelmet.openingWidth ?? 100 })} hint={openingHelmet.openingAngle === undefined ? 'Original vertical shape. Move from a narrow 25° slit to Open at 100°. Approximate angles at a 100° reference vertical FOV.' : '25°: narrow slit · 100°: Open. Approximate at a 100° reference vertical FOV; actual headset angles also depend on zoom and position. Crop follows automatically.'} />
        <Range label="Visor opening width" value={openingHelmet.openingWidth ?? 100} min={50} max={150} step={.5} onChange={openingWidth => changeOpening({ openingWidth })} hint="100% keeps the center opening full width with 0% left/right crop. Increase to push curved side edges farther out; reduce to narrow the opening. Rounded corners can still hide part of the scene." />
        <Button onClick={() => changeOpening({ openingAngle: undefined, openingWidth: openingHelmet.openingWidth ?? 100 })}>Original vertical opening</Button>
        <Range label="Helmet zoom" value={openingHelmet.scale} min={50} max={150} onChange={scale => changeOpening({ scale })} hint="100% is the original size. Increase zoom for a larger visor opening; decrease it for a smaller opening. The crop follows automatically." />
        <Range label="Helmet stereo alignment" value={openingHelmet.stereoOffset ?? 0} min={-5} max={5} step={.1} onChange={stereoOffset => changeOpening({ stereoOffset })} hint="Negative: inward. Positive: outward. Moves only the helmet and visor, in opposite directions for each eye. Start at 0 and save small changes to compare in the headset; this single-eye preview stays unchanged. The crop margin follows automatically." />
        <Button disabled={(openingHelmet.stereoOffset ?? 0) === 0} onClick={() => changeOpening({ stereoOffset: 0 })}>Reset stereo alignment</Button>
        <p className="panel-description helmet-editor-caption">The helmet is a flat 2D overlay with the same artwork in both eyes. Saving prepares static images for the selected visor states, composited after frame generation and behind your other overlays during a race. This preview is an appearance and crop guide.</p>
        <p className="helmet-editor-muted">Visor positions and texture-detail controls inspired by Williem3’s Sim-QVFR / ace-quadview. Independently implemented in OpenGrid XR.</p>
        <Button onClick={() => api.openSimQVFR().catch(error => setNotice(error.message))}>Sim-QVFR · original project</Button>
      </Panel>
      <Panel title="View opening & crop" icon={SlidersHorizontal}>
        <p className="panel-description">These are the same crop and contour settings as View & crop. They reduce the visible image; they do not change the headset’s optical field of view.</p>
        {usesFullRenderArea(profile) && <Note>Full render area for stream keeps crop and contour saved; only the stream output is framed. Extra area applies after save and restart.</Note>}
        <Button disabled={!helmet.enabled || fitting || !!pendingOpening} onClick={fitOpening}>{fitting ? 'Fitting opening…' : 'Fit crop to visor opening'}</Button>
        <p className="helmet-editor-muted">Fits the finished helmet and visor, including opaque sunstrips. Live switching reserves the fully open visor instead. Translucent areas, blurred edges, padding and motion allowance stay visible. Appearance changes update the fit automatically; horizontal crop stays off at 100% width or above.</p>
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
        <Segmented label="Visor position" value={openingHelmet.visorPosition ?? 'closed'} onChange={visorPosition => changeOpening({visorPosition})} options={[{value:'closed',label:'Closed'},{value:'half',label:'Half open'},{value:'open',label:'Fully open'}]} />
        <Toggle label="Switch visor while driving" checked={openingHelmet.visorHotkey ?? false} onChange={visorHotkey => changeOpening({visorHotkey})} description="Ctrl+Alt+V cycles Closed / Half / Open while the sim has focus. Save before starting the session." />
        {openingHelmet.visorHotkey && <Note>Crop reserves the fully open view for every visor position. Opening the visor during a race cannot reveal missing scenery. The shortcut changes the running session only; the saved position is restored on reload.</Note>}
        <Segmented label="Helmet editor section" value={section} onChange={setSection} options={[{ value: 'fit', label: 'Interior' }, { value: 'visor', label: 'Sunstrip' }, { value: 'graphic', label: 'Graphic' }, { value: 'tint', label: 'Visor' }]} />
        {!helmet.enabled && <p className="helmet-editor-muted">Switch on Show helmet to preview your changes.</p>}
        {section === 'fit' && <div className="helmet-editor-controls">
          <Segmented label="Helmet style" value={openingHelmet.style} onChange={style => changeOpening(style === 'level3' ? { style, openingAngle: 25 } : { style })} options={[{ value: 'formula', label: 'Formula' }, { value: 'gt', label: 'GT' }, { value: 'open', label: 'Open' }, { value: 'level3', label: 'Level 3' }]} />
          {openingHelmet.style === 'level3' && <p className="panel-description">Winner winner, podium dinner. Level 3 protection against bad apexes. Narrow opening selected; width stays yours.</p>}
          <Segmented label="Helmet texture detail" value={helmet.detail ?? 'performance'} onChange={detail => onHelmetChange({detail})} options={[{value:'performance',label:'Performance · 2048'},{value:'detail',label:'Detail · 4096'}]} />
          <p className="helmet-editor-muted">2048 × 1152 or 4096 × 2304 pixels. Detail gives sharper composed lettering and edges at four times the texture area; it does not add detail to the original interior artwork. Desktop preview uses a fixed resolution.</p>
          <Range label="Shell opacity" value={openingHelmet.opacity} onChange={opacity => changeOpening({ opacity })} />
          <Range label="Interior brightness" value={helmet.shellBrightness ?? 100} min={0} max={150} onChange={shellBrightness => onHelmetChange({ shellBrightness })} hint="Darken or brighten only the helmet interior. 0% is black; visor print and the game image keep their own settings." />
          <Range label="Interior contrast" value={helmet.shellContrast ?? 100} min={0} max={200} onChange={shellContrast => onHelmetChange({ shellContrast })} />
          <Range label="Interior saturation" value={helmet.shellSaturation ?? 100} min={0} max={200} onChange={shellSaturation => onHelmetChange({ shellSaturation })} />
          <Range label="Helmet blur" value={openingHelmet.shellBlur ?? 0} min={0} max={8} step={.1} unit="px" onChange={shellBlur => changeOpening({ shellBlur })} hint="Soften the complete helmet overlay, including visor text and logos. Keeps print size and position; the game image stays sharp. Zero is off." />
          <Button onClick={() => onHelmetChange({ shellBrightness: 100, shellContrast: 100, shellSaturation: 100 })}>Reset interior colors</Button>
          <Range label="Horizontal position" value={openingHelmet.offsetX} min={-100} max={100} onChange={offsetX => changeOpening({ offsetX })} />
          <Range label="Vertical position" value={openingHelmet.offsetY} min={-100} max={100} onChange={offsetY => changeOpening({ offsetY })} hint="Move the whole helmet and visor up or down together in both eyes. Outer borders stay covered and the crop follows automatically." />
          <div className="panel-divider" />
          <Range label="Head movement response" value={Math.round((openingHelmet.headMotion ?? 0) * 100)} onChange={value => changeOpening({ headMotion: value / 100 })} hint="Optional subtle helmet sway from head movement. Off at zero." />
          <Range label="Vehicle movement response" value={Math.round((openingHelmet.vehicleMotion ?? 0) * 100)} onChange={value => changeOpening({ vehicleMotion: value / 100 })} hint="Optional subtle sway from live vehicle telemetry. Off at zero." />
        </div>}
        {section === 'visor' && <div className="helmet-editor-controls">
          <Toggle label="Upper sunstrip" checked={visor.enabled} onChange={enabled => changeVisor({ enabled })} />
          <div className="helmet-editor-presets"><Button onClick={() => changeVisor({ enabled: true, color: '#f4f0e5', textColor: '#c41b28', opacity: 92, text: visor.text || 'OPEN GRID', textSize: 44, textWidth: 52, height: 30, curve: 12, textCurve: 12, textX: 0, textY: 0, textOpacity: 100, softness: .6, mirror: true })}>White / red</Button><Button onClick={() => changeVisor({ enabled: true, color: '#101219', textColor: '#f8f7f2', opacity: 86, text: visor.text || 'OPEN GRID', textSize: 44, textWidth: 52, height: 30, curve: 12, textCurve: 12, textX: 0, textY: 0, textOpacity: 100, softness: .6, mirror: true })}>Dark / white</Button></div>
          <Range label="Sunstrip depth" value={visor.height} min={5} max={55} onChange={height => changeVisor({ height })} />
          <Range label="Sunstrip curve" value={visor.curve} min={-25} max={25} onChange={curve => changeVisor({ curve })} />
          <div className="helmet-editor-color-row"><label>Strip color<input type="color" aria-label="Sunstrip color" value={visor.color} onChange={event => changeVisor({ color: event.target.value })} /></label><label>Letter color<input type="color" aria-label="Sunstrip text color" value={visor.textColor} onChange={event => changeVisor({ textColor: event.target.value })} /></label></div>
          <Range label="Sunstrip opacity" value={visor.opacity} onChange={opacity => changeVisor({ opacity })} hint="Lower values let the track show through." />
          <label className="helmet-editor-text">Sponsor or personal text<input aria-label="Sunstrip text" value={visor.text} maxLength={80} placeholder="YOUR TEAM" onChange={event => changeVisor({ text: event.target.value.replace(/[\u0000-\u001f\u007f]/g, '') })} /></label>
          <label className="helmet-editor-text">Letter font<select aria-label="Letter font" value={visor.font} onChange={event => changeVisor({ font: event.target.value })}>{visorFonts.map(({value, label}) => <option key={value} value={value}>{label}</option>)}</select></label>
          <Range label="Letter size" value={visor.textSize} min={10} max={60} unit="px" onChange={textSize => changeVisor({ textSize })} />
          <Range label="Letter width" value={visor.textWidth} min={10} max={100} onChange={textWidth => changeVisor({ textWidth })} />
          <Range label="Text curvature" value={visor.textCurve} min={-25} max={25} onChange={textCurve => changeVisor({ textCurve })} hint="Zero keeps text straight. Positive values raise the ends; negative values lower them. Match Sunstrip curve to follow the strip." />
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
          <Range label="Whole visor tint" value={openingHelmet.visorTint} onChange={visorTint => changeOpening({ visorTint })} hint="Colored transparency across the visor; independent of the upper strip." />
          <p className="helmet-editor-muted">Set independent headset brightness, contrast, saturation and gamma in Color correction. These settings work with every visor position and while helmet artwork is off. Clean Output stays unchanged.</p>
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
