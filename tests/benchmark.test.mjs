import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
const { createBenchmark, exportHtml, exportCsv, timingStats } = createRequire(import.meta.url)('../desktop/benchmark.cjs');

const telemetry = (timestampMs, lap, extra = {}) => ({ game: 'iracing', status: 'live', connected: true, stale: false, timestampMs, ageMs: 0, sequence: timestampMs, data: { lap, lapsCompleted: lap - 1, lapTimeSeconds: 3, ...extra } });
const perf = (timestampMs, n, frames = [], extra = {}) => ({ sessionId: 'session-a', timestampMs, counters: { application: n, generated: n * 2, submitted: n * 3, repeated: 0, displayed: null }, frames, ...extra });
const frame = (sequence, timestampMs, frameMs = 10) => ({ sequence, timestampMs, frameMs, cpuMs: 4, gpuMs: null });
const setup = (options = {}) => { let clock = 1000; const b = createBenchmark({ now: () => clock, ...options }); return { b, at: t => { clock = t; } }; };

test('individual frames deduplicate while cumulative FG deltas preserve unsampled frames', () => {
  const { b, at } = setup(); b.start({ game: 'iracing', profileName: 'Test' }, perf(1000, 100), telemetry(1000, 2));
  at(1100); b.accept(perf(1100, 110, [frame(101, 1010), frame(102, 1020)]), telemetry(1100, 2));
  at(1200); b.accept(perf(1200, 120, [frame(101, 1010), frame(102, 1020), frame(103, 1030)]), telemetry(1200, 2));
  const r = b.stop(); assert.equal(r.frames.length, 3); assert.equal(r.summary.counters.application, 20); assert.equal(r.summary.counters.generated, 40); assert.equal(r.summary.counters.displayed, null); assert.equal(r.summary.timing.gpu.sampleCount, 0);
});

test('only lap segments bounded by two consecutive observed crossings are complete', () => {
  const { b, at } = setup(); b.start({ game: 'iracing' }, perf(1000, 0), telemetry(1000, 3));
  at(2000); b.accept(perf(2000, 10, [frame(1, 1500)]), telemetry(2000, 4, { lapTimeSeconds: 0.1 }));
  at(3000); b.accept(perf(3000, 20, [frame(2, 2500)]), telemetry(3000, 5, { lapTimeSeconds: 0.1 }));
  at(3500); b.accept(perf(3500, 25, [frame(3, 3400)]), telemetry(3500, 5));
  const r = b.stop(); assert.deepEqual(r.laps.map(x => x.complete), [false, true, false]); assert.deepEqual(r.laps.map(x => x.lap), [3, 4, 5]); assert.equal(r.laps[1].timing.frame.sampleCount, 1);
});

test('telemetry loss and lap reset split partial segments and never fabricate context', () => {
  const { b, at } = setup(); b.start({ game: 'iracing' }, perf(1000, 0), telemetry(1000, 9));
  at(2000); b.accept(perf(2000, 10), { ...telemetry(2000, 9), stale: true });
  at(3000); b.accept(perf(3000, 20), telemetry(3000, 1));
  const r = b.stop(); assert.ok(r.laps.every(x => !x.complete)); assert.ok(r.warnings.includes('telemetry-gap')); assert.equal(r.summary.context.carsVisible.sampleCount, 0); assert.equal(r.summary.context.rainIntensity.sampleCount, 0);
});

test('session switch stops instead of mixing counters and frames', () => {
  const { b, at } = setup(); b.start({ game: 'iracing' }, perf(1000, 5)); at(1100); b.accept(perf(1100, 9, [frame(1, 1050)]));
  at(1200); b.accept(perf(1200, 400, [frame(1, 1150)], { sessionId: 'other' }));
  const r = b.report(); assert.equal(r.stopReason, 'runtime-session-changed'); assert.equal(r.summary.counters.application, 4); assert.equal(r.frames.length, 1);
});

test('counter resets invalidate that delta and keep later totals labelled incomplete', () => {
  const { b, at } = setup(); b.start({ game: 'iracing' }, perf(1000, 100)); at(1100); b.accept(perf(1100, 105)); at(1200); b.accept(perf(1200, 2)); at(1300); b.accept(perf(1300, 5));
  const r = b.stop(); assert.equal(r.summary.counters.generated, 16); assert.ok(r.warnings.includes('counter-reset')); assert.equal(r.summary.counterCoverageComplete, false);
});

test('counter intervals that straddle a lap boundary remain unattributed per lap', () => {
  const { b, at } = setup(); b.start({ game: 'iracing' }, perf(1000, 0), telemetry(1000, 1)); at(2000); b.accept(perf(2000, 20), telemetry(1700, 2));
  const r = b.stop(); assert.equal(r.summary.counters.generated, 40); assert.equal(r.laps[0].counters.generated, null); assert.equal(r.laps[1].counters.generated, null); assert.equal(r.summary.boundaryUnattributed.generated, 40);
});

test('statistics define slowest-frame lows and nearest-rank percentiles without GPU estimates', () => {
  const stats = timingStats([10, 10, 20, 100]); assert.equal(stats.p99Ms, 100); assert.equal(stats.low1Fps, 10); assert.equal(stats.averageFps, 1000 / 35); assert.equal(stats.sampleCount, 4);
  assert.equal(timingStats([]).averageFps, null);
});

test('exports escape imported profile data and prevent spreadsheet formulas', () => {
  const { b } = setup(); b.start({ game: 'iracing', profileName: '=HYPERLINK("https://bad")<script>alert(1)</script>' }, perf(1000, 0), telemetry(1000, 1));
  const r = b.stop(); const html = exportHtml(r); assert.ok(!html.includes('<script>')); assert.ok(html.includes('&lt;script&gt;')); assert.ok(html.includes('Content-Security-Policy')); assert.ok(exportCsv(r).includes("'=HYPERLINK"));
});

test('memory and duration limits finalize recordings with explicit reason', () => {
  const { b, at } = setup({ maxSamples: 2 }); b.start({ game: 'iracing' }, perf(1000, 0)); at(1200); b.accept(perf(1200, 3, [frame(1, 1010), frame(2, 1020), frame(3, 1030)])); assert.equal(b.report().frames.length, 2); assert.equal(b.report().stopReason, 'sample-limit');
  const s = setup({ maxDurationMs: 50 }); s.b.start({ game: 'iracing' }); s.at(1100); s.b.accept(null, null); assert.equal(s.b.report().stopReason, 'duration-limit');
});

test('old frame ring and stale telemetry cannot appear as new recording data', () => {
  const { b, at } = setup(); b.start({ game: 'iracing' }, perf(1000, 2, [frame(1, 800), frame(2, 900)]), telemetry(1000, 1)); at(4000); b.accept(perf(4000, 3, [frame(2, 900), frame(3, 3900)]), telemetry(1000, 1, { carsVisible: 9 })); const r = b.stop(); assert.equal(r.frames.length, 1); assert.equal(r.summary.context.carsVisible.sampleCount, 0); assert.ok(r.warnings.includes('telemetry-gap'));
});

test('independent telemetry polls preserve categorical source context and native solar altitude', () => {
  const { b, at } = setup(); b.start({ game: 'iracing' }, perf(1000, 0), telemetry(1000, 1));
  at(1050); b.accept(null, telemetry(1050, 1, { cloudCover: 'partly-cloudy', trackWetnessState: 'damp', solarAltitudeRadians: -.1, carsNearby: 3, carsVisible: null, carsNearbyRadiusMeters: 100, carsNearbyMethod: 'wrapped-track-distance', trackName: 'Spa' }));
  at(1100); b.accept(perf(1100, 10), undefined);
  const r = b.stop(); assert.ok(!r.warnings.includes('performance-unavailable')); assert.ok(!r.warnings.includes('telemetry-gap'));
  assert.deepEqual(r.summary.context.cloudCover, ['partly-cloudy']); assert.deepEqual(r.summary.context.trackWetnessState, ['damp']); assert.equal(r.summary.context.carsNearby.mean, 3); assert.equal(r.summary.context.solarAltitudeRadians.mean, -.1); assert.ok(exportHtml(r).includes('wrapped-track-distance')); assert.ok(exportCsv(r).includes('partly-cloudy'));
});

test('silent telemetry interval makes lap partial even when next packet itself is fresh', () => {
  const { b, at } = setup(); b.start({ game: 'iracing' }, perf(1000, 0), telemetry(1000, 1));
  at(1500); b.accept(perf(1500, 10), telemetry(1500, 2));
  at(4000); b.accept(perf(4000, 40), telemetry(4000, 3));
  const r = b.stop(); assert.ok(r.laps.every(x => !x.complete)); assert.ok(r.warnings.includes('telemetry-gap'));
});

test('frame sampling limit stops without blocking report on many small lap segments', () => {
  const { b, at } = setup(); b.start({ game: 'iracing' }, perf(1000, 0), telemetry(1000, 1));
  for (let i = 1; i <= 120; i++) { at(1000 + i * 1000); b.accept(perf(1000 + i * 1000, i * 100, Array.from({ length: 100 }, (_, j) => frame((i - 1) * 100 + j + 1, 1000 + (i - 1) * 1000 + j * 10 + 1))), telemetry(1000 + i * 1000, i + 1)); }
  const r = b.stop(); assert.equal(r.frames.length, 12000); assert.equal(r.laps.length, 121); assert.equal(r.summary.counters.application, 12000); assert.equal(r.laps.filter(x => x.complete).length, 119);
});

test('same-session frame sequence reset is surfaced instead of silently dropping new frames', () => {
  const { b, at } = setup(); b.start({ game: 'iracing' }, perf(1000, 0)); at(1100); b.accept(perf(1100, 10, [frame(10, 1090)])); at(1200); b.accept(perf(1200, 20, [frame(1, 1190)])); assert.equal(b.report()?.stopReason, 'frame-sequence-reset');
});

test('stale initial performance snapshot does not claim complete counter coverage from the record button', () => {
  const { b, at } = setup(); b.start({ game: 'iracing' }, perf(900, 100)); at(1100); b.accept(perf(1100, 120, [frame(101, 1050)])); const r = b.stop(); assert.equal(r.summary.counterCoverageComplete, false); assert.ok(r.warnings.includes('counter-baseline-before-start'));
});

test('reported lap duration is separate from timestamp-observed segment duration', () => {
  const { b, at } = setup(); b.start({ game: 'iracing' }, perf(1000, 0), telemetry(1000, 1));
  at(2000); b.accept(perf(2000, 10), telemetry(2000, 2, { lastLapSeconds: 90.125 })); const r = b.stop(); assert.equal(r.laps[0].reportedLapSeconds, 90.125); assert.equal(r.laps[0].durationMs, 1000); assert.ok(exportHtml(r).includes('90.13'));
});

test('time of day is a clock range, never a misleading average across midnight', () => {
  const { b, at } = setup(); b.start({ game: 'iracing' }, perf(1000, 0), telemetry(1000, 1, { timeOfDaySeconds: 86399, cloudCover: 'overcast', trackWetnessState: 'wet' })); at(1100); b.accept(perf(1100, 5), telemetry(1100, 1, { timeOfDaySeconds: 1 })); const r = b.stop(); assert.equal(r.summary.context.timeOfDaySeconds.mean, null); const html = exportHtml(r); assert.ok(html.includes('23:59:59')); assert.ok(html.includes('Air temperature (°C)')); assert.ok(html.includes('overcast'));
});

test('bounded benchmark configuration metadata survives and polling can omit raw samples', () => {
  const { b, at } = setup(); const configuration = { profileId:'profile-1', sessionId:'abc', mode:'driving', renderScale:150, crop:'{"top":10}', framegen:'{"enabled":true}', helmetStyle:'formula' };
  b.start({game:'iracing',metadata:configuration},perf(1000,0)); at(1100); b.accept(perf(1100,5,[frame(1,1050)])); b.stop(); const r=b.report({includeSamples:false}); assert.deepEqual(r.metadata,configuration); assert.equal(r.frames,undefined); assert.equal(r.telemetry,undefined); assert.equal(r.counterIntervals,undefined); assert.equal(r.summary.timing.frame.sampleCount,1); assert.equal(b.report().frames.length,1);
});
