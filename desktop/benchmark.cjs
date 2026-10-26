"use strict";

// The collector supplies actual timestamped frames and cumulative native totals.
// This module never turns polled FPS/utilization into invented frames or GPU time.
const COUNTERS = ["application", "generated", "repeated", "submitted", "displayed"];
const CONTEXT = ["speedMps", "airTempC", "trackTempC", "rainIntensity", "trackWetness", "sessionTimeSeconds", "timeOfDaySeconds", "solarAltitudeRadians", "carsNearby", "carsVisible", "carsNearbyRadiusMeters"];
const CATEGORIES = ["weather", "daylight", "cloudCover", "trackWetnessState", "carsNearbyMethod", "trackName"];
const CONTEXT_LABELS = { speedMps: "Speed (m/s)", airTempC: "Air temperature (°C)", trackTempC: "Track temperature (°C)", rainIntensity: "Rain intensity (0–1)", trackWetness: "Track wetness (0–1)", sessionTimeSeconds: "Session elapsed (s)", timeOfDaySeconds: "Local simulation clock (first → last)", solarAltitudeRadians: "Solar altitude (radians)", carsNearby: "Nearby cars", carsVisible: "Visible cars", carsNearbyRadiusMeters: "Nearby radius (m)", weather: "Weather", daylight: "Day / night", cloudCover: "Cloud cover", trackWetnessState: "Track surface", carsNearbyMethod: "Nearby measurement method", trackName: "Track" };
const DEFINITIONS = {
  frame: "Application frame interval; not a measured GPU render duration or headset display interval.",
  averageFps: "1000 divided by the arithmetic mean of valid application frame intervals in milliseconds.",
  percentiles: "Nearest-rank percentiles: sorted value at ceil(percentile × sample count).",
  lows: "1% / 0.1% low FPS = 1000 divided by the mean of the slowest ceil(N × 0.01 / 0.001) valid application intervals. Small samples may reduce to one frame.",
  cpu: "Only source-provided CPU measurements. Their measurement boundary must be supplied in metadata; unavailable values remain empty.",
  gpu: "Only source-provided measured GPU durations. Utilization is never converted to milliseconds.",
  counters: "Differences of cumulative source counters over observed intervals. Submitted frames are not proof of frames displayed by the headset. Generated, repeated, application and submitted counts are separate; do not sum them as independent categories.",
  laps: "Lap boundaries are telemetry observations, with polling uncertainty. A complete segment has both observed consecutive boundaries and continuous telemetry. Starting/stopping, skipped laps, reset and gaps are partial.",
  boundary: "A counter interval crossing a lap boundary is retained in the whole recording but not divided between laps. Lap counter coverage can therefore be incomplete.",
  context: "Context summaries use fresh telemetry samples, not time-weighted averages. Nearby vehicles are not necessarily visible. Time of day alone is not a verified day/night classification.",
};
const finite = value => typeof value === "number" && Number.isFinite(value);
const number = value => finite(value) ? value : null;
const duration = value => finite(value) && value > 0 && value <= 60000 ? value : null;
const counter = value => Number.isSafeInteger(value) && value >= 0 ? value : null;
const text = (value, max = 200) => typeof value === "string" ? value.replace(/[\u0000-\u001f\u007f]/g, " ").slice(0, max) : "";
const totals = () => Object.fromEntries(COUNTERS.map(key => [key, null]));
const clone = value => structuredClone(value);
const limit = (value, fallback, ceiling) => Number.isSafeInteger(value) && value > 0 ? Math.min(value, ceiling) : fallback;

function timingStats(values) {
  const sorted = values.filter(value => duration(value) !== null).sort((a, b) => a - b);
  const n = sorted.length;
  if (!n) return { sampleCount: 0, meanMs: null, minMs: null, maxMs: null, p50Ms: null, p95Ms: null, p99Ms: null, p999Ms: null, averageFps: null, low1Fps: null, low01Fps: null };
  const meanMs = sorted.reduce((sum, value) => sum + value, 0) / n;
  const percentile = p => sorted[Math.ceil(n * p) - 1];
  const low = share => { const count = Math.ceil(n * share); return 1000 / (sorted.slice(n - count).reduce((sum, value) => sum + value, 0) / count); };
  return { sampleCount: n, meanMs, minMs: sorted[0], maxMs: sorted[n - 1], p50Ms: percentile(.5), p95Ms: percentile(.95), p99Ms: percentile(.99), p999Ms: percentile(.999), averageFps: 1000 / meanMs, low1Fps: low(.01), low01Fps: low(.001) };
}

function numericSummary(values) {
  let count = 0, sum = 0, min = Infinity, max = -Infinity, first = null, last = null;
  for (const value of values) if (finite(value)) { if (!count) first = value; last = value; count++; sum += value; min = Math.min(min, value); max = Math.max(max, value); }
  return { sampleCount: count, mean: count ? sum / count : null, min: count ? min : null, max: count ? max : null, first, last };
}
function addCounters(result, values) {
  for (const key of COUNTERS) if (counter(values[key]) !== null) result[key] = (result[key] ?? 0) + values[key];
}
function metadata(raw = {}) {
  const result = {};
  if (!raw || typeof raw !== "object" || Array.isArray(raw)) return result;
  // Explicit allowlist keeps profiles, account names and arbitrary objects out.
  for (const key of ["appVersion", "runtime", "runtimeVersion", "headset", "gpu", "graphicsApi", "cpuTimingSource", "gpuTimingSource", "carsNearbyDefinition", "weatherSource", "track", "car", "notes", "profileId", "sessionId", "mode", "helmetStyle", "crop", "framegen"]) {
    if (typeof raw[key] === "string") result[key] = text(raw[key], ["notes", "crop", "framegen"].includes(key) ? 1000 : 200);
  }
  for (const key of ["renderScale", "leftWidth", "leftHeight", "rightWidth", "rightHeight", "refreshHz"]) if (finite(raw[key]) && raw[key] >= 0) result[key] = raw[key];
  return result;
}

function createBenchmark(options = {}) {
  const now = options.now || Date.now;
  const maxSamples = limit(options.maxSamples, 250000, 500000);
  const maxTelemetrySamples = limit(options.maxTelemetrySamples, 50000, 150000);
  const maxDurationMs = limit(options.maxDurationMs, 2 * 60 * 60 * 1000, 4 * 60 * 60 * 1000);
  const maxFrameGapMs = limit(options.maxFrameGapMs, 2000, 10000);
  const maxSegments = 2000;
  let recording = null, finished = null, previousPerf = null, lastFrameSequence = null, lastFrameTime = null;
  let previousTelemetry = null, telemetryLive = false, runtimeSession = null;
  const warn = value => recording?.warnings.add(value);

  function status() {
    return { state: recording ? "recording" : finished ? "complete" : "idle", recording: !!recording, running: !!recording,
      game: recording?.game ?? finished?.game ?? null, profileName: recording?.profileName ?? finished?.profileName ?? "",
      startedAtMs: recording?.startedAtMs ?? finished?.startedAtMs ?? null,
      elapsedMs: recording ? Math.max(0, now() - recording.startedAtMs) : finished?.durationMs ?? 0,
      frameCount: recording?.frames.length ?? finished?.frames.length ?? 0,
      telemetryCount: recording?.telemetry.length ?? finished?.telemetry.length ?? 0,
      lapSegments: recording?.segments.length ?? finished?.laps.length ?? 0,
      completeLapCount: recording ? recording.segments.filter(item => item.startsAtBoundary && item.endsAtBoundary).length : finished?.laps.filter(item => item.complete).length ?? 0,
      stopReason: finished?.stopReason ?? null,
      warnings: recording ? [...recording.warnings] : finished?.warnings ?? [],
      limits: { maxSamples, maxTelemetrySamples, maxDurationMs },
    };
  }
  function segment(at, lap, startsAtBoundary, reason) {
    const last = recording.segments.at(-1);
    at = Math.max(recording.startedAtMs, Math.min(now(), at), last?.startMs ?? 0);
    if (last) { last.endMs = at; last.endReason = reason; last.endsAtBoundary = reason === "lap-crossing"; }
    if (recording.segments.length >= maxSegments) { warn("segment-limit"); stop("segment-limit"); return; }
    recording.segments.push({ id: recording.segments.length + 1, lap, startMs: at, endMs: null, startsAtBoundary, endsAtBoundary: false, startReason: reason, endReason: null, reportedLapSeconds: null });
  }
  function handleTelemetry(input) {
    if (!recording) return;
    const currentTime = now();
    if (input === undefined && (!previousTelemetry || currentTime - previousTelemetry.timestampMs <= 1500)) return;
    const live = input && input.game === recording.game && input.status === "live" && input.connected === true && input.stale === false &&
      finite(input.timestampMs) && currentTime - input.timestampMs >= -100 && currentTime - input.timestampMs <= 1500 &&
      (input.ageMs == null || (finite(input.ageMs) && input.ageMs >= 0 && input.ageMs <= 1500)) && input.data && typeof input.data === "object";
    if (!live) {
      if (telemetryLive) { warn("telemetry-gap"); segment(currentTime, null, false, "telemetry-gap"); }
      telemetryLive = false; previousTelemetry = null;
      return;
    }
    if (previousTelemetry && input.timestampMs <= previousTelemetry.timestampMs) return;
    if (telemetryLive && input.timestampMs - previousTelemetry.timestampMs > 1500) {
      warn("telemetry-gap"); segment(previousTelemetry.timestampMs + 1500, null, false, "telemetry-gap"); telemetryLive = false; previousTelemetry = null;
    }
    if (!recording) return;
    const data = input.data;
    const lap = Number.isSafeInteger(data.lap) && data.lap >= 0 ? data.lap : Number.isSafeInteger(data.lapsCompleted) && data.lapsCompleted >= 0 ? data.lapsCompleted + 1 : null;
    const at = Math.max(recording.startedAtMs, Math.min(currentTime, input.timestampMs));
    const current = recording.segments.at(-1);
    const reset = previousTelemetry && ((data.sessionId != null && previousTelemetry.sessionId != null && data.sessionId !== previousTelemetry.sessionId) ||
      (counter(input.sequence) !== null && counter(previousTelemetry.sequence) !== null && input.sequence < previousTelemetry.sequence) ||
      (lap !== null && previousTelemetry.lap !== null && lap < previousTelemetry.lap) ||
      (lap === previousTelemetry.lap && finite(data.lapTimeSeconds) && finite(previousTelemetry.lapTimeSeconds) && previousTelemetry.lapTimeSeconds - data.lapTimeSeconds > 5));
    if (!current) segment(at, lap, false, "recording-start");
    else if (reset) { warn("telemetry-reset"); segment(at, lap, false, "telemetry-reset"); }
    else if (!telemetryLive) {
      if (current.startMs === at && current.lap === null && current.startReason === "recording-start") current.lap = lap;
      else segment(at, lap, false, "telemetry-resumed");
    } else if (lap !== current.lap) {
      const consecutive = lap !== null && current.lap !== null && lap === current.lap + 1 && at - previousTelemetry.timestampMs <= 1500;
      if (!consecutive) warn("lap-gap");
      if (consecutive && finite(data.lastLapSeconds) && data.lastLapSeconds > 0) current.reportedLapSeconds = data.lastLapSeconds;
      segment(at, lap, consecutive, consecutive ? "lap-crossing" : "lap-gap");
    }
    if (!recording) return;
    if (recording.telemetry.length >= maxTelemetrySamples) { stop("telemetry-limit"); return; }
    const point = { timestampMs: at, sourceTimestampMs: input.timestampMs, lap, sequence: counter(input.sequence), inPits: typeof data.inPits === "boolean" ? data.inPits : null,
      lapTimeSeconds: number(data.lapTimeSeconds), lastLapSeconds: number(data.lastLapSeconds), isDaytime: typeof data.isDaytime === "boolean" ? data.isDaytime : null,
    };
    for (const key of CONTEXT) point[key] = number(data[key]);
    for (const key of CATEGORIES) point[key] = text(data[key], key === "trackName" ? 200 : 80) || null;
    if (!point.daylight && finite(data.solarAltitudeRadians)) point.daylight = data.solarAltitudeRadians >= 0 ? "Sun above horizon" : "Sun below horizon (night / twilight)";
    recording.telemetry.push(point);
    previousTelemetry = { timestampMs: input.timestampMs, sequence: input.sequence, lap, lapTimeSeconds: data.lapTimeSeconds, sessionId: data.sessionId };
    telemetryLive = true;
  }

  function validPerf(input) {
    return input && typeof input.sessionId === "string" && input.sessionId.length > 0 && input.sessionId.length <= 200 && finite(input.timestampMs) &&
      now() - input.timestampMs >= -100 && now() - input.timestampMs <= maxFrameGapMs;
  }
  function start(configuration, initialPerf, telemetry) {
    if (recording) throw new Error("A benchmark is already recording.");
    if (!configuration || !["iracing", "lmu"].includes(configuration.game)) throw new Error("Select a supported game for the benchmark.");
    finished = null; previousPerf = null; previousTelemetry = null; lastFrameSequence = null; lastFrameTime = null; telemetryLive = false; runtimeSession = null;
    const startedAtMs = now();
    recording = { schemaVersion: 1, game: configuration.game, profileName: text(configuration.profileName), metadata: metadata(configuration.metadata),
      startedAtMs, frames: [], telemetry: [], intervals: [], segments: [], warnings: new Set(), framegen: [] };
    segment(startedAtMs, null, false, "recording-start");
    if (validPerf(initialPerf)) {
      runtimeSession = initialPerf.sessionId;
      // A cached snapshot before the click includes an unknowable pre-recording
      // portion. Begin counter coverage at the first fresh in-recording snapshot.
      if (initialPerf.timestampMs < startedAtMs) warn("counter-baseline-before-start");
      else previousPerf = clone({ timestampMs: initialPerf.timestampMs, counters: Object.fromEntries(COUNTERS.map(key => [key, counter(initialPerf.counters?.[key])])) });
    }
    handleTelemetry(telemetry);
    return status();
  }

  function accept(input, telemetry) {
    if (!recording) return status();
    if (now() - recording.startedAtMs >= maxDurationMs) { stop("duration-limit"); return status(); }
    handleTelemetry(telemetry);
    if (!recording) return status();
    if (input == null) return status();
    if (!validPerf(input)) { warn("performance-unavailable"); return status(); }
    if (runtimeSession !== null && input.sessionId !== runtimeSession) { stop("runtime-session-changed"); return status(); }
    runtimeSession = input.sessionId;
    if (previousPerf && input.timestampMs <= previousPerf.timestampMs) return status();
    const currentCounters = Object.fromEntries(COUNTERS.map(key => [key, counter(input.counters?.[key])]));
    if (previousPerf) {
      const delta = totals(); let valid = true;
      for (const key of COUNTERS) if (currentCounters[key] !== null && previousPerf.counters[key] !== null) {
        if (currentCounters[key] >= previousPerf.counters[key]) delta[key] = currentCounters[key] - previousPerf.counters[key];
        else { valid = false; warn("counter-reset"); }
      }
      if (recording.intervals.length >= maxTelemetrySamples) { stop("counter-limit"); return status(); }
      recording.intervals.push({ startMs: previousPerf.timestampMs, endMs: input.timestampMs, counters: delta, valid });
    } else warn("counter-baseline-delayed");
    previousPerf = { timestampMs: input.timestampMs, counters: currentCounters };
    if (input.framegen && typeof input.framegen === "object") {
      const item = { state: text(input.framegen.state, 40), mode: text(input.framegen.mode, 40), multiplier: number(input.framegen.multiplier), refreshHz: number(input.framegen.refreshHz) };
      const last = recording.framegen.at(-1);
      if (!last || Object.keys(item).some(key => item[key] !== last[key])) {
        if (recording.framegen.length < 2000) recording.framegen.push({ timestampMs: input.timestampMs, ...item }); else warn("framegen-history-limit");
      }
    }
    const frames = Array.isArray(input.frames) ? input.frames : [];
    if (frames.length > 4096) warn("frame-packet-truncated");
    for (const raw of frames.slice(-4096)) {
      if (counter(raw?.sequence) === null || !finite(raw.timestampMs) || raw.timestampMs < recording.startedAtMs || raw.timestampMs > input.timestampMs + 100 || duration(raw.frameMs) === null) continue;
      if (lastFrameSequence !== null && raw.sequence <= lastFrameSequence) {
        if (raw.timestampMs > lastFrameTime) { warn("frame-sequence-reset"); stop("frame-sequence-reset"); return status(); }
        continue;
      }
      if (lastFrameTime !== null && raw.timestampMs < lastFrameTime) { warn("frame-clock-regression"); continue; }
      if (lastFrameSequence !== null && raw.sequence !== lastFrameSequence + 1) warn("frame-sequence-gap");
      if (lastFrameTime !== null && raw.timestampMs - lastFrameTime > maxFrameGapMs) warn("frame-time-gap");
      if (recording.frames.length >= maxSamples) { stop("sample-limit"); return status(); }
      recording.frames.push({ sequence: raw.sequence, timestampMs: raw.timestampMs, frameMs: raw.frameMs, cpuMs: duration(raw.cpuMs), gpuMs: duration(raw.gpuMs) });
      lastFrameSequence = raw.sequence; lastFrameTime = raw.timestampMs;
    }
    return status();
  }

  function summarize(source, startMs, endMs, final = false) {
    const contains = timestamp => timestamp >= startMs && (timestamp < endMs || (final && timestamp === endMs));
    const frames = source.frames.filter(item => contains(item.timestampMs));
    const context = source.telemetry.filter(item => contains(item.timestampMs));
    const includedIntervals = source.intervals.filter(item => item.startMs >= startMs && item.endMs <= endMs);
    const overlapping = source.intervals.filter(item => item.startMs < endMs && item.endMs > startMs && (item.startMs < startMs || item.endMs > endMs));
    const counts = totals(); for (const interval of includedIntervals) addCounters(counts, interval.counters);
    const timing = Object.fromEntries([["frame", "frameMs"], ["cpu", "cpuMs"], ["gpu", "gpuMs"]].map(([name, key]) => [name, timingStats(frames.map(item => item[key]))]));
    const contextSummary = Object.fromEntries(CONTEXT.map(key => [key, numericSummary(context.map(item => item[key]))]));
    contextSummary.timeOfDaySeconds.mean = null; // A circular clock has no arithmetic mean.
    for (const key of CATEGORIES) contextSummary[key] = [...new Set(context.map(item => item[key] || (key === "daylight" ? item.isDaytime === true ? "day" : item.isDaytime === false ? "night" : null : null)).filter(Boolean))];
    if (!contextSummary.weather.length) contextSummary.weather = [...contextSummary.cloudCover.map(value => `Cloud cover: ${value}`), ...contextSummary.trackWetnessState.map(value => `Track surface: ${value}`)];
    contextSummary.inPitsSamples = context.filter(item => item.inPits === true).length;
    return { timing, context: contextSummary, counters: counts, counterCoverageComplete: overlapping.length === 0 && includedIntervals.every(item => item.valid) &&
      includedIntervals.length > 0 && includedIntervals[0].startMs <= startMs && includedIntervals.at(-1).endMs >= endMs,
      counterIntervalCount: includedIntervals.length, overlappingCounterIntervals: overlapping.length, durationMs: Math.max(0, endMs - startMs) };
  }
  function buildReport(source, stoppedAtMs, reason) {
    const result = { schemaVersion: 1, game: source.game, profileName: source.profileName, metadata: source.metadata, runtimeSessionId: runtimeSession,
      startedAtMs: source.startedAtMs, stoppedAtMs, durationMs: Math.max(0, stoppedAtMs - source.startedAtMs), stopReason: text(reason, 80),
      definitions: DEFINITIONS, warnings: [...source.warnings], frames: source.frames, telemetry: source.telemetry, counterIntervals: source.intervals, framegen: source.framegen,
      summary: summarize(source, source.startedAtMs, stoppedAtMs, true), laps: [] };
    for (let i = 0; i < source.segments.length; i++) {
      const s = source.segments[i], endMs = s.endMs ?? stoppedAtMs;
      result.laps.push({ ...s, endMs, complete: s.startsAtBoundary && s.endsAtBoundary, ...summarize(source, s.startMs, endMs, i === source.segments.length - 1) });
    }
    result.summary.boundaryUnattributed = totals();
    for (const interval of source.intervals) if (!result.laps.some(lap => interval.startMs >= lap.startMs && interval.endMs <= lap.endMs)) addCounters(result.summary.boundaryUnattributed, interval.counters);
    if (result.summary.timing.frame.sampleCount === 0) result.warnings.push("no-frame-timings");
    if (source.telemetry.length === 0) result.warnings.push("no-telemetry-context");
    if (result.summary.counters.application !== null && result.summary.counters.application > result.frames.length + 1) result.warnings.push("frame-timing-coverage-incomplete");
    return clone(result);
  }
  function stop(reason = "manual") {
    if (!recording) return finished ? clone(finished) : null;
    const source = recording; recording = null;
    finished = buildReport(source, Math.max(source.startedAtMs, now()), reason);
    return clone(finished);
  }
  function report(options = {}) {
    if (!finished) return null;
    if (options.includeSamples === false) {
      const { frames, telemetry, counterIntervals, ...summary } = finished;
      return clone(summary);
    }
    return clone(finished);
  }
  return { start, accept, stop, status, report };
}

const escapeHtml = value => String(value ?? "").replace(/[&<>"']/g, character => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[character]);
const format = (value, decimals = 2) => finite(value) ? value.toFixed(decimals) : "Unavailable";
function clockTime(value) {
  if (!finite(value)) return "Unavailable";
  const seconds = ((Math.floor(value) % 86400) + 86400) % 86400;
  return [Math.floor(seconds / 3600), Math.floor(seconds / 60) % 60, seconds % 60].map(part => String(part).padStart(2, "0")).join(":");
}
function validateReport(report) {
  if (!report || report.schemaVersion !== 1 || !Array.isArray(report.frames) || report.frames.length > 500000 || !Array.isArray(report.laps) || report.laps.length > 2000) throw new Error("Invalid benchmark report.");
}
function chart(frames, startMs, endMs) {
  if (!frames.length) return '<p class="muted">No application frame timings available.</p>';
  // Bucket maxima preserve stalls in a bounded chart; CSV keeps each raw frame.
  const buckets = new Map(); const durationMs = Math.max(1, endMs - startMs);
  for (const f of frames) { const index = Math.min(799, Math.max(0, Math.floor((f.timestampMs - startMs) / durationMs * 800))); buckets.set(index, Math.max(buckets.get(index) || 0, f.frameMs)); }
  const peak = Math.max(1, ...buckets.values());
  const points = [...buckets].sort((a, b) => a[0] - b[0]).map(([x, y]) => `${x},${(120 - y / peak * 110).toFixed(2)}`).join(" ");
  return `<svg viewBox="0 0 800 130" role="img" aria-label="Application frame interval timeline"><polyline fill="none" stroke="#70cfbc" stroke-width="1.5" points="${points}"/></svg><p class="muted">Peak ${format(peak)} ms. Each chart bucket shows its slowest application interval; CSV contains every recorded sample.</p>`;
}
function exportHtml(report) {
  validateReport(report);
  const e = escapeHtml;
  const summary = report.summary;
  const rows = report.laps.map(lap => `<tr><td>${e(lap.lap ?? "Unknown")}</td><td>${lap.complete ? "Complete" : "Partial"}</td><td>${format(lap.durationMs / 1000)} s</td><td>${format(lap.timing.frame.averageFps)}</td><td>${format(lap.timing.frame.low1Fps)}</td><td>${format(lap.timing.frame.p99Ms)}</td><td>${format(lap.timing.cpu.meanMs)}</td><td>${format(lap.timing.gpu.meanMs)}</td><td>${e(lap.counters.generated ?? "Unavailable")}</td><td>${lap.counterCoverageComplete ? "Covered" : "Partial / unavailable"}</td></tr>`).join("");
  const contextRows = CONTEXT.map(key => { const v = summary.context[key]; return `<tr><td>${e(CONTEXT_LABELS[key])}</td><td>${v.sampleCount}</td><td>${key === "timeOfDaySeconds" ? clockTime(v.first) : format(v.min)}</td><td>${key === "timeOfDaySeconds" ? "—" : format(v.mean)}</td><td>${key === "timeOfDaySeconds" ? clockTime(v.last) : format(v.max)}</td></tr>`; }).join("");
  const lapDetails = report.laps.map(lap => {
    const points = report.frames.filter(f => f.timestampMs >= lap.startMs && f.timestampMs < lap.endMs);
    const context = CONTEXT.filter(key => lap.context[key].sampleCount).map(key => `<li>${e(CONTEXT_LABELS[key])}: ${key === "timeOfDaySeconds" ? `${clockTime(lap.context[key].first)} → ${clockTime(lap.context[key].last)}` : `${format(lap.context[key].min)} – ${format(lap.context[key].max)} (sample mean ${format(lap.context[key].mean)})`}</li>`).join("") + CATEGORIES.filter(key => lap.context[key].length).map(key => `<li>${e(CONTEXT_LABELS[key])}: ${e(lap.context[key].join(", "))}</li>`).join("");
    return `<details><summary>Lap ${e(lap.lap ?? "unknown")} · ${lap.complete ? "complete" : "partial"} · ${e(lap.startReason)} / ${e(lap.endReason ?? report.stopReason)}</summary><p>Sim-reported lap time: ${format(lap.reportedLapSeconds)} s. Recorded segment: ${format(lap.durationMs / 1000)} s; these differ for partial laps and polling boundaries.</p>${chart(points, lap.startMs, lap.endMs)}<ul>${context || "<li>Weather / vehicle context unavailable.</li>"}</ul><p>Weather: ${e(lap.context.weather.join(", ") || "Unavailable")} · Day/night: ${e(lap.context.daylight.join(", ") || "Unavailable")}</p><p>Overlapping counter intervals: ${lap.overlappingCounterIntervals}. These are included in the total, not divided between laps.</p></details>`;
  }).join("");
  return `<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; img-src 'none'; base-uri 'none'; form-action 'none'"><title>OpenGrid XR benchmark</title><style>body{font:15px system-ui,sans-serif;background:#10171d;color:#e5edf2;max-width:1200px;margin:32px auto;padding:0 24px;line-height:1.5}h1,h2{color:#9ce3d4}table{border-collapse:collapse;width:100%;display:block;overflow:auto;margin:16px 0}th,td{padding:8px 12px;text-align:left;border-bottom:1px solid #34414b;white-space:nowrap}th{color:#9ce3d4}.muted{color:#a8b7c3}svg{width:100%;background:#17222b;border-radius:8px}details{border:1px solid #34414b;padding:14px;margin:12px 0}summary{cursor:pointer;font-weight:600}code{overflow-wrap:anywhere}li{margin:8px 0}</style></head><body><h1>OpenGrid XR benchmark</h1><p>${e(report.game)} · ${e(report.profileName)} · ${format(report.durationMs / 1000)} seconds · ${e(report.stopReason)}</p><p class="muted">${e(new Date(report.startedAtMs).toISOString())} — ${e(new Date(report.stoppedAtMs).toISOString())}. Local report; no scripts or network resources.</p><h2>Recording summary</h2><p>Application FPS ${format(summary.timing.frame.averageFps)} · 1% low ${format(summary.timing.frame.low1Fps)} · 0.1% low ${format(summary.timing.frame.low01Fps)} · p99 ${format(summary.timing.frame.p99Ms)} ms · ${summary.timing.frame.sampleCount} interval samples.</p><ul>${COUNTERS.map(key => `<li>${e(key)} frames: ${e(summary.counters[key] ?? "Unavailable")}</li>`).join("")}</ul><p>Counter interval coverage: ${summary.counterCoverageComplete ? "covered" : "partial / unavailable"}. Submitted frames do not confirm headset display. Boundary-unattributed generated frames: ${e(summary.boundaryUnattributed.generated ?? "None observed / unavailable")}.</p>${chart(report.frames, report.startedAtMs, report.stoppedAtMs)}<h2>Quality and gaps</h2><ul>${report.warnings.map(w => `<li>${e(w)}</li>`).join("") || "<li>No recorded warnings. This does not prove complete runtime display telemetry.</li>"}</ul><h2>Lap comparison</h2><table><thead><tr><th>Lap</th><th>Segment</th><th>Duration</th><th>App FPS</th><th>1% low</th><th>p99 ms</th><th>CPU ms</th><th>GPU ms</th><th>Generated</th><th>Counters</th></tr></thead><tbody>${rows}</tbody></table>${lapDetails}<h2>Weather and vehicle context</h2><p>Weather: ${e(summary.context.weather.join(", ") || "Unavailable")} · Day/night: ${e(summary.context.daylight.join(", ") || "Unavailable")}</p><table><thead><tr><th>Channel</th><th>Samples</th><th>Min</th><th>Mean</th><th>Max</th></tr></thead><tbody>${contextRows}</tbody></table><h2>Frame-generation state changes</h2><ul>${report.framegen.map(item => `<li>${format((item.timestampMs - report.startedAtMs) / 1000)} s: ${e(item.state)} / ${e(item.mode)}, multiplier ${format(item.multiplier, 0)}, ${format(item.refreshHz)} Hz</li>`).join("") || "<li>Unavailable</li>"}</ul><h2>Configuration</h2><ul>${Object.entries(report.metadata).map(([key, value]) => `<li>${e(key)}: ${e(value)}</li>`).join("") || "<li>No extra configuration supplied.</li>"}</ul><h2>Measurement definitions</h2><ul>${Object.entries(report.definitions).map(([key, value]) => `<li><strong>${e(key)}</strong>: ${e(value)}</li>`).join("")}</ul></body></html>`;
}
function csvCell(value) {
  if (value === null || value === undefined) return "";
  let cell = String(value);
  // Text originating in profile names or metadata must not become a formula.
  if (typeof value === "string" && /^[\s]*[=+\-@\t\r]/.test(cell)) cell = "'" + cell;
  return '"' + cell.replace(/"/g, '""') + '"';
}
function exportCsv(report) {
  validateReport(report);
  const headers = ["rowType", "timestampMs", "endTimestampMs", "lap", "sequence", "frameMs", "cpuMs", "gpuMs", ...COUNTERS, ...CONTEXT, ...CATEGORIES, "inPits", "name", "value"];
  const rows = [headers.join(",")];
  const append = value => rows.push(headers.map(key => csvCell(value[key])).join(","));
  append({ rowType: "metadata", name: "profileName", value: report.profileName });
  append({ rowType: "metadata", name: "game", value: report.game });
  for (const [name, value] of Object.entries(report.metadata)) append({ rowType: "metadata", name, value });
  for (const [name, value] of Object.entries(report.definitions)) append({ rowType: "definition", name, value });
  let index = 0;
  for (const f of report.frames) { while (index + 1 < report.laps.length && f.timestampMs >= report.laps[index].endMs) index++; append({ rowType: "frame", lap: report.laps[index]?.lap, ...f }); }
  for (const point of report.telemetry) append({ rowType: "telemetry", ...point, daylight: point.daylight || (point.isDaytime === true ? "day" : point.isDaytime === false ? "night" : null) });
  for (const interval of report.counterIntervals) append({ rowType: "counter-interval", timestampMs: interval.startMs, endTimestampMs: interval.endMs, ...interval.counters, name: "valid", value: interval.valid });
  for (const lap of report.laps) append({ rowType: "lap-segment", timestampMs: lap.startMs, endTimestampMs: lap.endMs, lap: lap.lap, name: "complete", value: lap.complete });
  return rows.join("\r\n") + "\r\n";
}

module.exports = { createBenchmark, timingStats, exportHtml, exportCsv };
