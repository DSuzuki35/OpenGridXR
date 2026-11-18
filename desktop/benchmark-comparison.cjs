'use strict';
const fs = require('node:fs/promises');
const path = require('node:path');
const { Worker, isMainThread, parentPort, workerData } = require('node:worker_threads');

// Only the recorder's own timestamped JSON names may be opened through IPC.
const recordingName = /^benchmark-(\d{4}-\d{2}-\d{2})T(\d{2})-(\d{2})-(\d{2})-(\d{3})Z\.json$/;
function validateRecordingId(id) {
  if (typeof id !== 'string' || id.length !== 'benchmark-2000-01-01T00-00-00-000Z.json'.length || !recordingName.test(id))
    throw new Error('Invalid recording selection.');
}
async function listRecordings(directory) {
  let entries;
  try { entries = await fs.readdir(directory, {withFileTypes:true}); }
  catch (error) { if (error.code === 'ENOENT') return []; throw error; }
  return entries.filter(entry => entry.isFile() && recordingName.test(entry.name)).map(entry => entry.name)
    .sort().reverse().slice(0,200).map(id => ({
      id, startedAt: id.replace(recordingName,'$1T$2:$3:$4.$5Z'),
    }));
}

const metrics = [
  ['Application FPS','frame','averageFps','FPS'], ['1% low','frame','low1Fps','FPS'],
  ['Mean frame interval','frame','meanMs','ms'], ['P99 frame interval','frame','p99Ms','ms'],
  ['Mean CPU time','cpu','meanMs','ms'], ['Mean GPU time','gpu','meanMs','ms'],
];
const boundedText = value => typeof value === 'string' ? value.slice(0,200) : '';
const positive = value => Number.isFinite(value) && value > 0 ? value : null;
function summarize(report) {
  const timing = report?.summary?.timing;
  if (report?.schemaVersion !== 1 || !['iracing','lmu'].includes(report.game) || !timing?.frame ||
      !Number.isSafeInteger(timing.frame.sampleCount) || timing.frame.sampleCount < 0 || !Number.isFinite(report.durationMs) || report.durationMs < 0)
    throw new Error('The selected file has no supported benchmark summary.');
  const meta = report.metadata ?? {};
  const tracks = report.summary.context?.trackName;
  const renderer = {off:'Ordinary OpenXR',stereoVrs:'Stereo VRS',quad:'Quad views'}[meta.lmuRenderingMode];
  const provider = {native:'native runtime',vectorxr:'VectorXR',qvfr:'QVFR'}[meta.lmuQuadProvider];
  const settings = [];
  if (report.game === 'lmu') {
    settings.push(typeof renderer === 'string' ? renderer : 'Renderer unrecorded');
    if (meta.lmuRenderingMode === 'quad' && typeof provider === 'string') settings.push(provider);
    if (meta.nvidiaMvrRequested === true) settings.push('MVR requested');
    if (meta.nvidiaMvrRequested === false && meta.lmuRenderingMode === 'quad') settings.push('MVR off');
  }
  if (positive(meta.renderScale)) settings.push(`${meta.renderScale}% render scale`);
  return {game:report.game, profile:boundedText(report.profileName), settings:settings.join(' · '),
    track:Array.isArray(tracks)?tracks.slice(0,3).map(boundedText).filter(Boolean).join(', '):'',
    durationMs:report.durationMs, samples:timing.frame.sampleCount, stopReason:boundedText(report.stopReason),
    warnings:Array.isArray(report.warnings)?report.warnings.slice(0,30).map(boundedText):[],
    values:metrics.map(([,group,key]) => timing[group]?.sampleCount > 0 ? positive(timing[group][key]) : null)};
}
async function readRecording(directory,id) {
  validateRecordingId(id);
  const file = path.join(directory,id);
  if (!(await fs.lstat(file)).isFile()) throw new Error('Select a saved benchmark file.');
  const handle = await fs.open(file,'r');
  try {
    const {size} = await handle.stat();
    if (!size || size > 128 * 1024 * 1024) throw new Error('Comparison supports recordings up to 128 MiB.');
    const bytes = Buffer.allocUnsafe(size);
    let read = 0;
    while (read < size) {
      const {bytesRead} = await handle.read(bytes,read,size-read,read);
      if (!bytesRead) throw new Error('The recording changed while being read.');
      read += bytesRead;
    }
    return summarize(JSON.parse(bytes.toString('utf8')));
  } finally { await handle.close(); }
}
async function compareRecordings(directory,first,second) {
  const a = await readRecording(directory,first);
  const b = await readRecording(directory,second);
  const rows = metrics.map(([label,,,unit],i) => {
    const av = a.values[i], bv = b.values[i];
    return {label,unit,a:av,b:bv,delta:av !== null && bv !== null ? 100 * (bv-av)/av : null};
  });
  const warnings = [];
  for (const [name,run] of [['A',a],['B',b]]) {
    if (run.samples < 1000) warnings.push(`${name} has fewer than 1,000 frame intervals.`);
    if (run.stopReason !== 'manual') warnings.push(`${name} stopped automatically (${run.stopReason || 'reason unavailable'}).`);
    if (run.warnings.length) warnings.push(`${name} recording warnings: ${run.warnings.map(value=>value.replaceAll('-',' ')).join(', ')}.`);
  }
  if (a.game !== b.game) warnings.push('The recordings are from different games.');
  if (a.track && b.track && a.track !== b.track) warnings.push('The recorded tracks differ.');
  if (Math.max(a.durationMs,b.durationMs) > 1.5 * Math.max(1000,Math.min(a.durationMs,b.durationMs)))
    warnings.push('Recording lengths differ by more than 50%.');
  return {a,b,rows,warnings};
}

// A long session contains raw timestamps and telemetry. Parse only the selected
// files, off the Electron main thread, and return the compact comparison.
async function compareInWorker(directory,first,second) {
  validateRecordingId(first);
  validateRecordingId(second);
  if (first === second) throw new Error('Select two different recordings.');
  return new Promise((resolve,reject) => {
    const worker = new Worker(__filename,{workerData:{directory,first,second}});
    worker.once('message',result=>result.error ? reject(new Error(result.error)) : resolve(result));
    worker.once('error',reject);
    worker.once('exit',code=>{if(code) reject(new Error('The benchmark comparison could not finish.'));});
  });
}
if (!isMainThread && parentPort) {
  compareRecordings(workerData.directory,workerData.first,workerData.second)
    .then(result=>parentPort.postMessage(result),error=>parentPort.postMessage({error:error.message}));
}
module.exports = {listRecordings,compareInWorker};
