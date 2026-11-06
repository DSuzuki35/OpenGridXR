import {useEffect,useId,useRef,useState} from 'react';
import {Palette} from 'lucide-react';
import {api} from '../api.mjs';
import {COLOR_PRESET_IDS,COLOR_RANGES,colorCorrectionDefaults,resolvedColorPreset,colorPresetIsIdentity} from '../color-correction.mjs';
import {Button,Note,Panel,Range,Segmented,Toggle} from './Controls.jsx';
import {BindingButton} from './BindingContext.jsx';
import {TrackArt} from './ContourEditor.jsx';
import SceneColorFilter from './SceneColorFilter.jsx';
import './ColorCorrectionPanel.css';

const name=id=>id ? id[0].toUpperCase()+id.slice(1) : 'Unknown';
const modifiers=['','Ctrl+','Alt+','Shift+','Ctrl+Alt+','Ctrl+Shift+','Alt+Shift+','Ctrl+Alt+Shift+'];
const inactive={available:false,supported:false,enabled:false,activePreset:'neutral',effectActive:false,error:''};

export default function ColorCorrectionPanel({profile,onChange,dirty=false,busy=false,active=true}) {
  const settings=profile.colorCorrection ?? colorCorrectionDefaults();
  const [editing,setEditing]=useState(settings.preset==='neutral'?'sun':settings.preset);
  const [status,setStatus]=useState(null),[error,setError]=useState(''),[cycling,setCycling]=useState(false);
  const request=useRef(0), mounted=useRef(true);
  const id=useId().replaceAll(':','');
  useEffect(()=>{mounted.current=true;return()=>{mounted.current=false;request.current++;};},[]);
  useEffect(()=>{
    let disposed=false,polling=false,timer;setStatus(null);setError('');
    const poll=async()=>{
      if(disposed || polling || !active || document.visibilityState==='hidden') return;
      polling=true;
      const token=++request.current;
      try {
        const next=await api.colorCorrectionStatus(profile.game);
        if(!disposed && token===request.current) {setStatus(next);setError('');}
      } catch(e) {if(!disposed && token===request.current) {setStatus(inactive);setError(e.message);}}
      polling=false;
      if(!disposed && active && document.visibilityState!=='hidden') timer=setTimeout(poll,1000);
    };
    const visible=()=>{clearTimeout(timer);if(document.visibilityState!=='hidden') void poll();};
    document.addEventListener('visibilitychange',visible);void poll();
    return()=>{disposed=true;request.current++;clearTimeout(timer);document.removeEventListener('visibilitychange',visible);};
  },[profile.game,profile.id,active]);
  const cycle=async()=>{
    request.current++;setCycling(true);setError('');
    try {const next=await api.cycleColorCorrection(profile.game);if(mounted.current) {request.current++;setStatus(next);}}
    catch(e) {if(mounted.current) setError(e.message);}
    finally {if(mounted.current) setCycling(false);}
  };
  const selected=resolvedColorPreset(settings,editing),startup=resolvedColorPreset(settings);
  const effect=settings.enabled && !colorPresetIsIdentity(startup);
  const key=settings.cycleShortcut.match(/F\d+$/)?.[0] ?? '',mods=key?settings.cycleShortcut.slice(0,-key.length):'Ctrl+Alt+';
  const patchPreset=(field,value)=>onChange({presets:{...settings.presets,[editing]:{...selected,[field]:value}}});
  const canCycle=status?.available && status.supported && status.enabled && !dirty && !busy && !cycling;
  return <div className="color-correction-layout">
    <div className="settings-stack">
      <Panel title="VR color correction" icon={Palette} action={<span className="badge">VR ONLY</span>}>
        <Toggle label="Enable VR color correction" checked={settings.enabled} onChange={enabled=>onChange({enabled})} description="Works independently of helmet artwork and visor position."/>
        <div className="select-field"><label htmlFor={`${id}-startup`}>Startup preset</label><select id={`${id}-startup`} value={settings.preset} onChange={e=>onChange({preset:e.target.value})}>{COLOR_PRESET_IDS.map(value=><option key={value} value={value}>{name(value)}</option>)}</select></div>
        <p className="panel-description">Configured startup: {name(settings.preset)}. Save settings and restart the XR session to apply the enabled state, preset values and shortcut.</p>
        {settings.preset==='neutral' && <Note>Neutral is fixed at 100% for every control and bypasses color grading.</Note>}
        <Note>Headset view only. Clean Output / stream stays unchanged. Supported D3D11 sessions and bridges only; native D3D12 color correction is unavailable.</Note>
      </Panel>
      <Panel title="Preset values">
        <Segmented label="Edit preset" value={editing} onChange={setEditing} options={['sun','night','custom'].map(value=>({value,label:name(value)}))}/>
        <p className="panel-description">Editing {name(editing)}. Choose it as the startup preset or cycle to it in a supported running session after saving and restarting.</p>
        {Object.entries(COLOR_RANGES).map(([field,[min,max]])=><Range key={field} label={name(field)} value={selected[field]} min={min} max={max} onChange={value=>patchPreset(field,value)} hint={field==='gamma'?'Above 100% lifts existing dark detail. It cannot recover clipped blacks.':undefined}/>)}
        <Button onClick={()=>onChange({presets:{...settings.presets,[editing]:colorCorrectionDefaults().presets[editing]}})}>Reset {name(editing)} values</Button>
        <p className="panel-description">Night is an adjustable starting point. Check the result in your headset for your game and lighting.</p>
      </Panel>
    </div>
    <div className="settings-stack">
      <Panel title="Running XR session">
        <div data-testid="color-live-preset" className="color-live-preset"><span>Current runtime preset</span><strong>{status?.available && status.supported?name(status.activePreset):'Unavailable'}</strong></div>
        <p className="panel-description" role="status">{!status?'Checking session…':!status.available?'No active color-control session.':!status.supported?'This session does not support VR color correction.':!status.enabled?'Color correction was disabled when this session started.':status.effectActive?'Color grading is active in the headset.':'Color grading is bypassed in the headset.'}</p>
        {(error || status?.error) && <Note tone="amber">{error || status.error}</Note>}
        <Button disabled={!canCycle} onClick={cycle}>{cycling?'Cycling…':'Cycle live preset'}</Button>
        <p className="panel-description">Neutral → Sun → Night → Custom. Live cycling needs a supported active session and a saved profile. It does not change your saved startup preset or require a session restart.</p>
        {dirty && <Note>Save your profile before cycling the running preset.</Note>}
      </Panel>
      <Panel title="Configured appearance" action={<span className="badge subtle">REFERENCE</span>}>
        <div className="color-reference">
          <div><span>Original</span><svg viewBox="0 0 800 450" role="img" aria-label="Original color reference"><TrackArt id={`${id}-raw`}/></svg></div>
          <div><span>Startup · {settings.enabled?name(settings.preset):'Disabled'}</span><svg viewBox="0 0 800 450" role="img" aria-label="Configured VR color reference"><defs><SceneColorFilter id={`${id}-color`} preset={startup}/></defs><g className="configured-color-scene" filter={effect?`url(#${id}-color)`:undefined}><TrackArt id={`${id}-graded`}/></g></svg></div>
        </div>
        <p className="panel-description">Illustrative sRGB reference with gamma correction. This shows your configured startup appearance; your headset and game can look different.</p>
      </Panel>
      <Panel title="Cycle controls" action={<BindingButton actionId="colorCorrection.cycle"/>}>
        <div className="color-shortcut-grid">
          <div className="select-field"><label htmlFor={`${id}-key`}>Color cycle key</label><select id={`${id}-key`} value={key} onChange={e=>onChange({cycleShortcut:e.target.value?mods+e.target.value:''})}><option value="">Unbound</option>{Array.from({length:12},(_,i)=>`F${i+1}`).map(value=><option key={value}>{value}</option>)}</select></div>
          <div className="select-field"><label htmlFor={`${id}-mods`}>Color key modifiers</label><select id={`${id}-mods`} value={mods} disabled={!key} onChange={e=>onChange({cycleShortcut:e.target.value+key})}>{modifiers.map(value=><option key={value} value={value}>{value?value.slice(0,-1).replaceAll('+',' + '):'None'}</option>)}</select></div>
        </div>
        <p className="panel-description">The keyboard chord cycles while the selected sim has focus. A wheel, joystick or buttonbox binding uses the same live action. Save before starting the session.</p>
      </Panel>
    </div>
  </div>;
}
