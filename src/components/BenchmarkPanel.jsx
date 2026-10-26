import React, { useEffect, useState } from 'react';
import { Activity, Download, Play, Square } from 'lucide-react';
import { api } from '../api.mjs';
import { Button, Note, Panel } from './Controls.jsx';
import './BenchmarkPanel.css';

const value=(n,digits=1)=>typeof n==='number'&&Number.isFinite(n)?n.toLocaleString(undefined,{maximumFractionDigits:digits}):'Unavailable';
const duration=ms=>`${Math.floor((ms??0)/60000)}:${String(Math.floor((ms??0)/1000)%60).padStart(2,'0')}`;
const context=(lap,key)=>lap.context?.[key];
const warningLabels={
  'counter-baseline-delayed':'Frame counters became available after recording started; totals cover the measured interval.',
  'counter-baseline-before-start':'The first counter snapshot predates the recording; initial counter coverage is incomplete.',
  'telemetry-gap':'Telemetry was interrupted; affected laps are marked partial.',
  'telemetry-reset':'The telemetry session reset during recording.',
  'lap-gap':'One or more lap crossings were not observed.',
  'counter-reset':'A frame counter reset; that interval is excluded.',
  'performance-unavailable':'Some performance samples were unavailable.',
  'frame-sequence-gap':'Some individual frame intervals were not captured.',
  'frame-sequence-reset':'The frame sequence restarted.',
  'frame-time-gap':'A gap occurred in the frame timing samples.',
  'frame-clock-regression':'Out-of-order frame timestamps were excluded.',
  'frame-packet-truncated':'An oversized frame packet was limited.',
  'no-frame-timings':'No frame intervals were captured.',
  'no-telemetry-context':'No live telemetry context was captured.',
  'frame-timing-coverage-incomplete':'Frame timing coverage is incomplete.',
  'segment-limit':'The lap-segment limit was reached.',
  'framegen-history-limit':'The frame-generation state history reached its limit.',
};
function Weather({lap}){
  const c=lap.context??{};
  return <div className="benchmark-context">
    <span>Air / track: {value(c.airTempC?.mean)} / {value(c.trackTempC?.mean)} °C</span>
    <span>Rain: {value(c.rainIntensity?.max===null?null:c.rainIntensity?.max*100)}%</span>
    <span>Clouds: {Array.isArray(c.cloudCover)?c.cloudCover.join(', ')||'Unavailable':value(c.cloudCover?.mean)}</span>
    <span>Daylight: {c.daylight?.join(', ')||'Unavailable'}</span>
    <span>Nearby: {value(c.carsNearby?.mean)} average · {value(c.carsNearby?.max,0)} peak</span>
    <span>Visible cars: {value(c.carsVisible?.max,0)}</span>
    <span>Traffic method: {c.carsNearbyMethod?.join(', ')||'Unavailable'}</span>
  </div>;
}
export default function BenchmarkPanel({profile,dirty}){
  const [status,setStatus]=useState(null),[report,setReport]=useState(null),[error,setError]=useState(''),[busy,setBusy]=useState(false);
  useEffect(()=>{
    let disposed=false,timer;
    const poll=async()=>{
      try{const next=await api.benchmarkStatus();if(!disposed){setStatus(next);if(next.state==='complete')setReport(await api.benchmarkReport());}}
      catch(e){if(!disposed)setError(e.message);}
      finally{if(!disposed)timer=setTimeout(poll,1000);}
    };poll();return()=>{disposed=true;clearTimeout(timer);};
  },[]);
  async function act(fn){setBusy(true);setError('');try{await fn();setStatus(await api.benchmarkStatus());setReport(await api.benchmarkReport());}catch(e){setError(e.message);}finally{setBusy(false);}}
  const running=status?.running,summary=report?.summary;
  return <div className="benchmark-page">
    <Panel title="Lap benchmark" icon={Activity} description="Record real application frame intervals, generated-frame counts and the conditions around each lap.">
      <div className="benchmark-actions">
        <Button icon={running?Square:Play} variant="primary" disabled={busy||(!running&&dirty)} onClick={()=>act(()=>running?api.benchmarkStop():api.benchmarkStart())}>{running?'Stop recording':'Start recording'}</Button>
        <span role="status">{status?.message||'Checking the recorder…'}</span>
      </div>
      {dirty&&!running&&<Note>Save or discard profile edits before recording.</Note>}
      {error&&<div className="benchmark-error" role="alert">{error}</div>}
      <div className="benchmark-metrics">
        <div><span>Recording</span><strong>{running?'Live':status?.state==='complete'?'Finished':'Ready'}</strong></div>
        <div><span>Elapsed</span><strong>{duration(status?.elapsedMs)}</strong></div>
        <div><span>Frame intervals</span><strong>{value(status?.frameCount??0,0)}</strong></div>
        <div><span>Complete laps</span><strong>{value(status?.completeLapCount??0,0)}</strong></div>
      </div>
      <p className="benchmark-help">Start before crossing the line, drive representative laps, then stop after the next crossing. Recording continues when you leave this page. It uses the saved {profile.game==='iracing'?'iRacing':'Le Mans Ultimate'} profile at start.</p>
      <Note>Enable the Performance HUD and restart the sim if the engine is not running. Telemetry is read while recording. Nearby cars are not necessarily visible; unavailable channels remain empty.</Note>
    </Panel>
    {report&&<>
      <Panel title="Recording results" icon={Activity} description={`${report.profileName} · ${duration(report.durationMs)} · ${report.game}`}>
        <div className="benchmark-metrics">
          <div><span>Application FPS</span><strong>{value(summary?.timing.frame.averageFps)}</strong></div>
          <div><span>1% low FPS</span><strong>{value(summary?.timing.frame.low1Fps)}</strong></div>
          <div><span>p99 frame interval</span><strong>{value(summary?.timing.frame.p99Ms)} ms</strong></div>
          <div><span>Generated frames</span><strong>{value(summary?.counters.generated,0)}</strong></div>
        </div>
        <div className="benchmark-actions"><Button icon={Download} disabled={busy} onClick={()=>act(()=>api.benchmarkExport('html'))}>Export HTML report</Button><Button icon={Download} disabled={busy} onClick={()=>act(()=>api.benchmarkExport('csv'))}>Export CSV data</Button></div>
        <p className="benchmark-help">The HTML report includes frame-time charts, lap comparisons, weather, traffic and frame-generation changes. The recording is also saved locally in your data folder.</p>
        <Weather lap={summary}/>
        {report.warnings.length>0&&<Note>{report.warnings.map(w=>warningLabels[w]??w.replaceAll('-',' ')).join(' ')}</Note>}
      </Panel>
      <Panel title="Lap comparison" icon={Activity} description="Partial laps and missing measurements are labelled explicitly.">
        <div className="benchmark-table"><table><thead><tr><th>Lap</th><th>Coverage</th><th>Duration</th><th>App FPS</th><th>1% low</th><th>p99 ms</th><th>Generated</th><th>Nearby peak</th></tr></thead><tbody>{report.laps.map(lap=><tr key={lap.id}><td>{lap.lap??'Unknown'}</td><td>{lap.complete?'Complete':'Partial'}</td><td>{duration(lap.durationMs)}</td><td>{value(lap.timing.frame.averageFps)}</td><td>{value(lap.timing.frame.low1Fps)}</td><td>{value(lap.timing.frame.p99Ms)}</td><td>{value(lap.counters.generated,0)}{!lap.counterCoverageComplete?' *':''}</td><td>{value(context(lap,'carsNearby')?.max,0)}</td></tr>)}</tbody></table></div>
        <p className="benchmark-help">* Counter intervals crossing a lap boundary stay in the recording total and are not guessed into either lap. Application frame intervals are not GPU render time. Submitted frames do not confirm headset display.</p>
        {report.laps.map(lap=><details key={lap.id} className="benchmark-lap"><summary>Lap {lap.lap??'unknown'} · {lap.complete?'complete':'partial'} · conditions</summary><Weather lap={lap}/></details>)}
      </Panel>
    </>}
  </div>;
}
