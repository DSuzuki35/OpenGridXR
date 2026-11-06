import {useEffect,useState} from 'react';
import {Eye,FolderOpen,Play,Camera,RefreshCw} from 'lucide-react';
import {Button,Note,Panel,Range,Toggle} from './Controls.jsx';
import {api,isBrowserPreview} from '../api.mjs';
import {lmuRenderingDefaults,validateLmuRendering,lmuRenderingLimits,updateLmuRendering} from '../lmu-rendering.mjs';

export default function QuadViewPage({profile,onChange,onViewChange,revision,dirty,busy,run,onNavigate}){
  const settings={...lmuRenderingDefaults(),...profile.lmuRendering};
  const [status,setStatus]=useState(null),[session,setSession]=useState(null),[error,setError]=useState(''),[review,setReview]=useState(null);
  const [steamOption,setSteamOption]=useState('');
  const [adjustment,setAdjustment]=useState('');
  useEffect(()=>setAdjustment(''),[profile.id]);
  useEffect(()=>{
    let live=true,pending=false;
    const refresh=async()=>{if(pending)return;pending=true;try{const next=await api.lmuStatus();if(live){setStatus(next);setSession(next.session||null);setError('');}}catch(e){if(live)setError(e.message);}finally{pending=false;}};
    // Pause while the window is hidden; refresh immediately when it returns.
    const tick=()=>{if(!window.document.hidden)void refresh();};
    const visible=()=>tick();
    refresh();const timer=setInterval(tick,3000);window.document.addEventListener('visibilitychange',visible);
    return()=>{live=false;clearInterval(timer);window.document.removeEventListener('visibilitychange',visible);};
  },[]);
  useEffect(()=>setReview(null),[profile.id,revision,dirty]);
  if(profile.game!=='lmu')return <Panel title="LMU rendering"><Note>Select a Le Mans Ultimate profile to configure quad views or stereo VRS.</Note></Panel>;
  let invalid='';try{validateLmuRendering(settings);}catch(e){invalid=e.message;}
  const quad=settings.mode==='quad',vrs=settings.mode==='stereoVrs',active=quad||vrs;
  const diagnostics=status?.diagnostics;
  const hooksUnavailable=diagnostics?.state==='unavailable'||session?.hookStatus==='unavailable';
  const limits=lmuRenderingLimits(settings);
  const rangeLimits=key=>limits?{allowedMin:limits[key].min,allowedMax:limits[key].max,
    hint:key==='peripheralScale'?`Max. ${limits[key].max}% with the current focus settings.`:
      `Min. ${limits[key].min}% with the current peripheral resolution${key==='focusScale'?' and focus size':''}.`}:{};
  const set=patch=>{
    const next=updateLmuRendering(settings,patch),requested={...settings,...patch};
    const changed=[];
    if(next.peripheralScale!==requested.peripheralScale)changed.push(`peripheral resolution to ${next.peripheralScale}%`);
    if(next.focusScale!==requested.focusScale)changed.push(`focus resolution to ${next.focusScale}%`);
    setAdjustment(changed.length?`Adjusted ${changed.join(' and ')} to keep this quad-view combination valid.`:'');
    onChange(next);
  };
  const controlsLocked=Boolean(busy);
  const launchLocked=controlsLocked||dirty||!active||Boolean(invalid)||!status?.compatibility?.supported||isBrowserPreview;
  const operate=async(name,fn)=>run(name,async()=>{const result=await fn();setStatus(await api.lmuStatus());return result;},result=>result?.message);
  const extent=value=>Number.isFinite(value)?value.toFixed(1):'—';
  return <div className="lmu-rendering-page" style={{display:'grid',gap:20}}>
    <Panel title="LMU quad views & VRS" icon={Eye}>
      <Note>Use your saved rendering profile with the normal Steam launch or the offline test launcher. Steam/EAC compatibility is experimental: valid signatures do not guarantee acceptance. Save your profile and restart LMU after changing rendering settings.</Note>
      <fieldset disabled={controlsLocked} style={{border:0,padding:0,margin:'16px 0 0',display:'grid',gap:16}}>
        <Toggle label="Disable crop" checked={profile.view.disableCrop===true} onChange={disableCrop=>onViewChange({disableCrop})} description="Bypass rectangular crop and contour masking. Saved values and the helmet overlay are preserved. Save and restart LMU to apply."/>
        <div className="select-field"><label htmlFor="lmu-mode">LMU rendering mode</label><select id="lmu-mode" value={settings.mode} onChange={e=>set({mode:e.target.value})}>
          <option value="off">Off · ordinary OpenXR</option><option value="quad">Quad views · outer + focus views</option><option value="stereoVrs">Stereo VRS · two native views</option>
        </select></div>
        {quad&&<Note tone="warning">Quad views can add roughly 3–4 ms of CPU frametime, depending on the scene and system. Recommended only with sufficient CPU headroom. Check your CPU frametime before enabling it.</Note>}
        {quad&&<div className="select-field"><label htmlFor="lmu-provider">Quad-view provider</label><select id="lmu-provider" value={settings.provider} onChange={e=>set({provider:e.target.value})}>
          <option value="native">Native runtime quad views</option><option value="qvfr">Quad-Views-Foveated (QVFR)</option><option value="vectorxr">Embedded VectorXR</option>
        </select></div>}
        {active&&<><div className="select-field"><label htmlFor="lmu-gaze">Focus position</label><select id="lmu-gaze" value={settings.gaze} onChange={e=>set({gaze:e.target.value})}><option value="auto">Eye tracking when available</option><option value="fixed">Fixed gaze</option></select></div>
          <Note>View & crop owns the shared field of view for all rendered views. <button type="button" className="text-button" onClick={()=>onNavigate('view')}>Open View & crop</button></Note>
          {(vrs||settings.provider!=='native')&&<>
            <Range label="Focus width" value={settings.focusWidth} min={10} max={90} {...rangeLimits('focusWidth')} onChange={value=>set({focusWidth:value})}/>
            <Range label="Focus height" value={settings.focusHeight} min={10} max={90} {...rangeLimits('focusHeight')} onChange={value=>set({focusHeight:value})}/>
          </>}
          {quad&&settings.provider==='native'&&<Note>Native quad dimensions and baseline focus size come from your OpenXR runtime. The values below adjust position and composition.</Note>}
          {quad&&settings.provider!=='native'&&<><Note>Grey slider sections are unavailable with the current settings. Values stop at the allowed limit.</Note><Range label="Peripheral resolution" value={settings.peripheralScale} min={10} max={400} {...rangeLimits('peripheralScale')} onChange={value=>set({peripheralScale:value})}/><Range label="Focus resolution" value={settings.focusScale} min={25} max={400} {...rangeLimits('focusScale')} onChange={value=>set({focusScale:value})}/></>}
          {quad&&<><Range label="Focus horizontal offset" value={settings.focusOffsetX} min={-50} max={50} onChange={value=>set({focusOffsetX:value})}/><Range label="Focus vertical offset" value={settings.focusOffsetY} min={-50} max={50} onChange={value=>set({focusOffsetY:value})}/>
            <Toggle label="Native outer resolution" checked={settings.nativeOuterResolution} onChange={value=>set({nativeOuterResolution:value})} description="Use the verified smaller outer render targets when their layouts are compatible."/>
            </>}
          {vrs&&<><div className="select-field"><label htmlFor="lmu-rate">Peripheral shading blocks</label><select id="lmu-rate" value={settings.peripheralShadingRate} onChange={e=>set({peripheralShadingRate:Number(e.target.value)})}><option value={1}>1 × 1</option><option value={2}>2 × 2</option><option value={4}>4 × 4</option></select></div><Note>Stereo VRS uses the two native eye renders, without extra focus views. Verified world passes use NVIDIA variable-rate shading when supported; unsupported devices retain ordinary stereo.</Note></>}
          <details><summary>Advanced rendering settings</summary><div style={{display:'grid',gap:16,marginTop:16}}>
            {quad&&<>
              <Toggle label="CPU upload reuse" checked={settings.cpuUploadReuse} onChange={value=>set({cpuUploadReuse:value})} description="Reuse verified uploads across focus views. Disables upload batching."/>
              <Toggle label="CPU upload batching" checked={settings.cpuUploadBatch} onChange={value=>set({cpuUploadBatch:value})} description="Batch verified uploads. Disables CPU upload reuse and CPU outer reuse."/>
              <Toggle label="CPU outer reuse" checked={settings.cpuOuterReuse} onChange={value=>set({cpuOuterReuse:value})} description="Reuse verified outer-view CPU work. Enables CPU upload reuse and disables batching."/>
              <Toggle label="Outer SSR" checked={settings.outerSsr} disabled={!settings.nativeOuterResolution} onChange={value=>set({outerSsr:value})} description="Use outer-view screen-space reflections with native outer resolution. Requires native outer resolution."/>
              <Note>These optional optimizations default off. Compare CPU and GPU frametimes and check image quality on your system.</Note>
            </>}
            <Toggle label="Show focus outline" checked={settings.debugFocus} onChange={value=>set({debugFocus:value})}/>
          </div></details>
        </>}
        {active&&<><Toggle label="Prefer AMD 3D V-Cache cores" checked={status?.preferences?.cpuCacheAffinity===true} disabled={!status||isBrowserPreview} onChange={cpuCacheAffinity=>operate('lmu-preferences',()=>api.setLmuPreferences({cpuCacheAffinity}))} description="This PC only · applies to quad views and stereo VRS on the next LMU launch. Excluded from shared profiles."/><Note>On supported asymmetric AMD 3D V-Cache CPUs, prefer the cache-rich cores. Other CPUs keep their Windows scheduling policy. No performance gain has been measured; compare on your system.</Note></>}
      </fieldset>
      {adjustment&&<Note>{adjustment}</Note>}
      {invalid&&<Note tone="warning">{invalid}</Note>}
      {dirty&&<Note>Save your profile to validate and launch these settings.</Note>}
    </Panel>
    <Panel title="Installation & launch" icon={FolderOpen}>
      <p style={{overflowWrap:'anywhere'}}>{status?.paths?.executable||'No LMU executable selected.'}</p>
      <Note>{status?.compatibility?.message||'Checking the local installation…'}</Note>
      <Button disabled={controlsLocked||isBrowserPreview} onClick={()=>operate('lmu-choose',()=>api.chooseLmuExecutable())}>Choose LMU executable</Button>
      <div style={{marginTop:18}}>
        <h3>Normal Steam launch</h3>
        <p>Save your profile, install the runtime layer, then start LMU in VR normally through Steam. The layer loads your saved rendering settings automatically. OpenGrid does not need to be open.</p>
        <Note>Remove the old OpenGrid --lmu-steam-launch option from Steam if you previously added it. EAC remains enabled. Signed DLLs are required; acceptance still depends on EAC.</Note>
        <details><summary>Legacy launch option (diagnostics)</summary>
        <Button disabled={controlsLocked||isBrowserPreview} onClick={()=>operate('lmu-steam-option',async()=>{const result=await api.copyLmuSteamLaunchOption();setSteamOption(result.option);return result;})}>Copy Steam launch option</Button>
        {steamOption&&<pre style={{whiteSpace:'pre-wrap',overflowWrap:'anywhere'}}>{steamOption}</pre>}
        <Note>Optional explicit launcher with process-specific provider isolation. No automatic offline fallback.</Note></details>
      </div>
      {quad&&settings.provider==='qvfr'&&<><p style={{overflowWrap:'anywhere'}}>{status?.paths?.qvfrManifest||'No QVFR manifest selected.'}</p><Note>{status?.providers?.qvfr?.message||'Select the installed QVFR provider.'} Saving prepares a private provider copy and backs up and updates only the LMU block in QVFR settings.</Note><Button disabled={controlsLocked||isBrowserPreview} onClick={()=>operate('lmu-provider',()=>api.chooseLmuProvider())}>Choose QVFR manifest</Button></>}
      {quad&&settings.provider==='vectorxr'&&<Note>Uses the engine included with OpenGrid XR. Automatic startup keeps your registered layers unchanged. Disable conflicting per-game features in standalone tools if necessary.</Note>}
      <div style={{display:'flex',gap:10,flexWrap:'wrap',marginTop:18}}>
        <Button icon={RefreshCw} disabled={launchLocked} onClick={()=>operate('lmu-review',async()=>{const result=await api.previewLmuLaunch(profile.id,revision);setReview(result);return result;})}>Check launch</Button>
        <Button icon={Play} disabled={launchLocked} onClick={()=>operate('lmu-offline',()=>api.launchLmuOffline(profile.id,revision))}>Launch LMU offline</Button>
        {quad&&<Button icon={Camera} disabled={launchLocked} onClick={()=>operate('lmu-offline',()=>api.launchLmuOffline(profile.id,revision,{capture:true}))}>Launch with four-view capture</Button>}
      </div>
      {review&&<Note>{review.message} Provider: {review.provider}. Layers: {review.layers.join(' → ')}.</Note>}
      {error&&<Note tone="warning">{error}</Note>}
    </Panel>
    <Panel title="Actual rendered views" icon={Eye}>
      {hooksUnavailable&&<Note tone="warning">{diagnostics?.activeProcess===false?'Last LMU launch failed; the process has exited.':'LMU rendering hooks are unavailable.'} {diagnostics?.reason||'The rendering integration could not start.'} Close LMU, set LMU rendering mode to Off, save your profile, then restart LMU to use standard stereo. Check game-build compatibility before enabling quad views or stereo VRS again.</Note>}
      {diagnostics?.state==='starting'&&<Note>LMU rendering is starting: {diagnostics.reason}</Note>}
      {diagnostics?.state==='ready'&&diagnostics.warning&&<Note>Native outer fallback: {diagnostics.warning}</Note>}
      {diagnostics?.state==='ready'&&diagnostics.nativeOuterRefusalCount>0&&<p>Native outer fallbacks observed: {diagnostics.nativeOuterRefusalCount}</p>}
      {diagnostics&&<><p>CPU preference outcome: {diagnostics.cpuAffinity}</p><p>Guarded reentries: {diagnostics.reentryCount} · Outer image timeouts: {diagnostics.outerTimeoutCount}</p></>}
      {session?.focusFovValid?<><p>Rendered frame {session.frameId} · {session.provider}</p><p>Left focus: {extent(session.focusLeftHorizontal)}° × {extent(session.focusLeftVertical)}° · Right focus: {extent(session.focusRightHorizontal)}° × {extent(session.focusRightVertical)}°</p></>:<Note>No rendered quad scene. Actual FOV appears after a valid four-view scene has rendered.</Note>}
      {session?.outerSizes?.length>0&&<p>{session.mode==='stereoVrs'?'Stereo eyes':'Outer views'}: {session.outerSizes.map(s=>`${s.width} × ${s.height}`).join(' / ')}</p>}
      {session?.focusFovValid&&session.focusSizes?.length>0&&<p>Focus views: {session.focusSizes.map(s=>`${s.width} × ${s.height}`).join(' / ')}</p>}
      {session?.streamingExpansionUnsupported&&<Note>Quad rendering cannot expand the clean-output field of view beyond the shared rendered views.</Note>}
      {status?.capture?.state!=='inactive'&&status?.capture&&<><Note>Capture: {status.capture.state}. Load a track before requesting the four scene views. Inspect outer0/1 and app0/1 with the run log’s FOV and pose data. Image readback is unsuitable for performance measurements.</Note><p style={{overflowWrap:'anywhere'}}>{status.capture.directory}</p><Button icon={Camera} disabled={controlsLocked||status.capture.state!=='armed'} onClick={()=>operate('lmu-capture',()=>api.triggerLmuCapture(status.capture.runId))}>Capture this scene</Button></>}
    </Panel>
  </div>;
}
