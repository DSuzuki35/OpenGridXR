using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.Globalization;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

namespace LmuQuadViewLauncher
{
    internal static class BenchmarkAbi
    {
        internal const uint Magic = 0x51424D4C;
        internal const int Size = 1900800, HeaderSize = 256, Slots = 4096, SlotSize = 80, BitmapOffset = 327936;
    }

    internal struct BenchmarkSample
    {
        internal long Counter, Qpc;
        internal ulong FrameId, GpuFrameId;
        internal double IntervalMs, AppWallMs, ProviderWallMs, GpuMs;
        internal uint Flags;
        internal int Result;
        internal bool Scene { get { return (Flags & 1) != 0; } }
    }

    public sealed class BenchmarkSnapshot
    {
        public bool FocusFovValid { get; internal set; }
        public bool DynamicFocus { get; internal set; }
        public bool DynamicFocusHolding { get; internal set; }
        public double FocusScalePercent { get; internal set; }
        public double FocusLeftHorizontal { get; internal set; }
        public double FocusLeftVertical { get; internal set; }
        public double FocusRightHorizontal { get; internal set; }
        public double FocusRightVertical { get; internal set; }
        public ulong FocusFrame { get; internal set; }
        public double Fps { get; internal set; }
        public double FrameMs { get; internal set; }
        public double AppWallMs { get; internal set; }
        public double ProviderWallMs { get; internal set; }
        public double GpuSpanMs { get; internal set; }
        public ulong GpuSourceFrame { get; internal set; }
        public long DroppedSamples { get; internal set; }
        public long RecordedSamples { get; internal set; }
        public int NativeStatus { get; internal set; }
        public int DiagnosticReason { get; internal set; }
        public int ProjectionCount { get; internal set; }
        public int LastXrResult { get; internal set; }
        public uint GpuFailures { get; internal set; }
        public string Resolution { get; internal set; }
        public string Diagnostic { get; internal set; }
        public double[] FrameHistory { get; internal set; }
        public BenchmarkSnapshot() { Fps = FrameMs = AppWallMs = ProviderWallMs = GpuSpanMs = Double.NaN; Resolution = "Waiting for scene"; Diagnostic = "No connection"; FrameHistory = new double[0]; }
    }

    internal static class BenchmarkReader
    {
        internal static void ReadFocus(MemoryMappedViewAccessor view,BenchmarkSnapshot snapshot)
        {
            snapshot.FocusFovValid=false;
            int sequence=view.ReadInt32(136);if(sequence==0||(sequence&1)!=0)return;
            Thread.MemoryBarrier();
            uint flags=view.ReadUInt32(140);float scale=view.ReadSingle(144);
            float lh=view.ReadSingle(148),lv=view.ReadSingle(152),rh=view.ReadSingle(156),rv=view.ReadSingle(160);
            ulong frame=view.ReadUInt64(168);
            Thread.MemoryBarrier();if(view.ReadInt32(136)!=sequence)return;
            snapshot.DynamicFocus=(flags&2)!=0;snapshot.DynamicFocusHolding=(flags&4)!=0;
            if((flags&1)==0||frame==0||!BenchmarkStatistics.IsValue(scale)||scale<25||scale>200)return;
            foreach(float extent in new[]{lh,lv,rh,rv})if(!BenchmarkStatistics.IsValue(extent)||extent<=0||extent>=180)return;
            snapshot.FocusScalePercent=scale;snapshot.FocusLeftHorizontal=lh;snapshot.FocusLeftVertical=lv;
            snapshot.FocusRightHorizontal=rh;snapshot.FocusRightVertical=rv;snapshot.FocusFrame=frame;snapshot.FocusFovValid=true;
        }
        internal static bool Valid(MemoryMappedViewAccessor view, int pid)
        { return view.Capacity >= BenchmarkAbi.Size && view.ReadUInt32(0) == BenchmarkAbi.Magic && view.ReadInt32(4) == 1 && view.ReadInt32(8) == BenchmarkAbi.Size && view.ReadInt32(12) == pid && view.ReadInt64(16) > 0; }
        internal static void SkipOverwritten(long published, ref long next, ref long dropped)
        {
            long oldest = Math.Max(1, published - BenchmarkAbi.Slots + 1);
            if (next < oldest) { dropped += oldest - next; next = oldest; }
        }
        internal static bool TryRead(MemoryMappedViewAccessor view, long counter, out BenchmarkSample sample)
        {
            sample = new BenchmarkSample();
            long offset = BenchmarkAbi.HeaderSize + ((counter - 1) % BenchmarkAbi.Slots) * BenchmarkAbi.SlotSize;
            long before = view.ReadInt64(offset);
            if (before != counter) return false;
            Thread.MemoryBarrier();
            sample.Counter = counter; sample.Qpc = view.ReadInt64(offset + 8); sample.FrameId = view.ReadUInt64(offset + 16);
            sample.IntervalMs = view.ReadDouble(offset + 24); sample.AppWallMs = view.ReadDouble(offset + 32);
            sample.ProviderWallMs = view.ReadDouble(offset + 40); sample.GpuMs = view.ReadDouble(offset + 48);
            sample.GpuFrameId = view.ReadUInt64(offset + 56); sample.Flags = view.ReadUInt32(offset + 64); sample.Result = view.ReadInt32(offset + 68);
            Thread.MemoryBarrier();
            if (view.ReadInt64(offset) != before) return false;
            if ((sample.Flags & 8) == 0) sample.GpuMs = Double.NaN;
            return true;
        }
    }

    public sealed class BenchmarkController : IDisposable
    {
        readonly string root;
        readonly object completedLock = new object();
        readonly Queue<double> history = new Queue<double>();
        MemoryMappedFile mapping;
        MemoryMappedViewAccessor view;
        Process process;
        BenchmarkCapture capture;
        DateTime nextDiscover, nextBitmap, lastHeartbeatTime, gpuTime;
        long nextSample, dropped, captureDroppedStart, heartbeat, frequency;
        bool disposed, visible = true;
        int template;
        float x, y = -.25F, distance = 1.2F, width = .65F;
        string reportPath = "", reportError, completedPath;
        string status = "No connection. Enable Benchmark, then launch LMU with diagnostics enabled.";
        public BenchmarkController(string root) { this.root = root; Snapshot = new BenchmarkSnapshot(); }
        public bool Connected { get { return view != null; } }
        public bool Recording { get { return capture != null; } }
        public bool OverlayVisible { get { return visible; } set { visible = value; WriteConfigSafely(); } }
        public int Template { get { return template; } set { template = value == 1 ? 1 : 0; WriteConfigSafely(); } }
        public float OverlayX { get { return x; } set { x = Clamp(value, -2, 2, 0); WriteConfigSafely(); } }
        public float OverlayY { get { return y; } set { y = Clamp(value, -2, 2, -.25F); WriteConfigSafely(); } }
        public float OverlayDistance { get { return distance; } set { distance = Clamp(value, .3F, 5, 1.2F); WriteConfigSafely(); } }
        public float OverlayWidth { get { return width; } set { width = Clamp(value, .2F, 2, .65F); WriteConfigSafely(); } }
        public string Status { get { lock (completedLock) return reportError == null ? status : status + " Report could not be saved: " + reportError; } }
        public string LastReportPath { get { lock (completedLock) return reportPath; } }
        public BenchmarkSnapshot Snapshot { get; private set; }
        static float Clamp(float value, float min, float max, float fallback) { return Single.IsNaN(value) || Single.IsInfinity(value) ? fallback : Math.Max(min, Math.Min(max, value)); }

        public void Tick()
        {
            if (disposed) return;
            lock (completedLock)
            {
                if (completedPath != null) { reportPath = completedPath; completedPath = null; }
            }
            if (capture != null && capture.HasStopped) { capture = null; WriteConfigSafely(); }
            DateTime now = DateTime.UtcNow;
            try
            {
                if (view == null)
                {
                    if (now < nextDiscover) return;
                    nextDiscover = now.AddSeconds(1); Discover(now);
                    if (view == null) return;
                }
                if (process != null && process.HasExited) { Disconnect("LMU exited", true); return; }
                if (!BenchmarkReader.Valid(view, view.ReadInt32(12))) { Disconnect("Benchmark mapping is incompatible", true); return; }
                long currentHeartbeat = view.ReadInt64(32);
                if (currentHeartbeat != heartbeat) { heartbeat = currentHeartbeat; lastHeartbeatTime = now; }
                if ((now - lastHeartbeatTime).TotalSeconds > 5) { Disconnect("Benchmark heartbeat stopped. Inspect the latest logs/quad-*.log.", true); return; }
                ReadSamples(now);
                if (view == null) return;
                ReadDiagnostics();
                if (now >= nextBitmap)
                {
                    nextBitmap = now.AddMilliseconds(200);
                    Snapshot.FrameHistory = history.ToArray();
                    WriteConfig();
                    using (Bitmap bitmap = RenderPreview()) WriteBitmap(bitmap);
                }
            }
            catch (Exception ex) { if (ex is OutOfMemoryException) throw; Disconnect("Benchmark connection interrupted: " + ex.Message, true); }
        }

        void Discover(DateTime now)
        {
            Process[] candidates = Process.GetProcessesByName("Le Mans Ultimate");
            try { foreach (Process candidate in candidates)
            {
                MemoryMappedFile found = null;
                try
                {
                    found = MemoryMappedFile.OpenExisting("Local\\LMUQuadView.Benchmark." + candidate.Id.ToString(CultureInfo.InvariantCulture), MemoryMappedFileRights.ReadWrite);
                    AttachMapping(found, candidate.Id, now); process = candidate; return;
                }
                catch (Exception ex)
                {
                    if (view != null) { view.Dispose(); view = null; mapping = null; }
                    if (found != null) found.Dispose();
                    if (!(ex is FileNotFoundException)) status = "Waiting for benchmark connection: " + ex.Message;
                }
            } } finally { foreach (Process candidate in candidates) if (candidate != process) candidate.Dispose(); }
        }

        // Internal transport seam also permits ABI integration fixtures without launching LMU.
        internal void AttachMapping(MemoryMappedFile found, int pid, DateTime now)
        {
            MemoryMappedViewAccessor access = found.CreateViewAccessor(0, BenchmarkAbi.Size, MemoryMappedFileAccess.ReadWrite);
            if (!BenchmarkReader.Valid(access, pid)) { access.Dispose(); throw new InvalidDataException("Incompatible benchmark ABI"); }
            mapping = found; view = access; frequency = view.ReadInt64(16);
            // A newly attached launcher starts at the live edge; pre-attachment history is not capture loss.
            nextSample = Math.Max(1, view.ReadInt64(24)); dropped = 0; heartbeat = view.ReadInt64(32); lastHeartbeatTime = now; nextBitmap = DateTime.MinValue;
            status = "Connected; waiting for a rendered scene."; WriteConfig();
        }

        void ReadSamples(DateTime now)
        {
            long published = view.ReadInt64(24);
            if (published < nextSample - 1) { Disconnect("Benchmark stream restarted", true); return; }
            BenchmarkReader.SkipOverwritten(published, ref nextSample, ref dropped);
            for (int read = 0; nextSample <= published && read < BenchmarkAbi.Slots; read++)
            {
                BenchmarkSample sample;
                if (!BenchmarkReader.TryRead(view, nextSample, out sample))
                {
                    long latest = view.ReadInt64(24);
                    long previous = nextSample; BenchmarkReader.SkipOverwritten(latest, ref nextSample, ref dropped);
                    if (nextSample == previous) break; // In-flight overwrite: retry next tick, never accept a torn slot.
                    continue;
                }
                nextSample++;
                if (capture != null) capture.Add(sample);
                if (sample.Scene)
                {
                    history.Enqueue(sample.IntervalMs); if (history.Count > 240) history.Dequeue();
                    Snapshot.FrameMs = sample.IntervalMs; Snapshot.Fps = BenchmarkStatistics.IsValue(sample.IntervalMs) && sample.IntervalMs > 0 ? 1000 / sample.IntervalMs : Double.NaN;
                    Snapshot.AppWallMs = sample.AppWallMs; Snapshot.ProviderWallMs = sample.ProviderWallMs;
                    if (BenchmarkStatistics.IsValue(sample.GpuMs)) { Snapshot.GpuSpanMs = sample.GpuMs; Snapshot.GpuSourceFrame = sample.GpuFrameId; gpuTime = now; }
                }
                else { Snapshot.FrameMs = Snapshot.Fps = Snapshot.AppWallMs = Snapshot.ProviderWallMs = Double.NaN; }
            }
            if ((now - gpuTime).TotalSeconds > 2) Snapshot.GpuSpanMs = Double.NaN;
            Snapshot.DroppedSamples = dropped;
            Snapshot.RecordedSamples = capture == null ? 0 : capture.Count;
        }

        void ReadDiagnostics()
        {
            if (view == null) return;
            BenchmarkReader.ReadFocus(view,Snapshot);
            Snapshot.NativeStatus = view.ReadInt32(84); Snapshot.LastXrResult = view.ReadInt32(92); Snapshot.ProjectionCount = view.ReadInt32(96);
            if(view.ReadInt32(88)==0||Snapshot.NativeStatus==4||Snapshot.ProjectionCount==0)Snapshot.FocusFovValid=false;
            Snapshot.GpuFailures = view.ReadUInt32(100); Snapshot.DiagnosticReason = view.ReadInt32(132);
            bool stereo=view.ReadUInt32(108)>0&&view.ReadUInt32(112)>0&&view.ReadUInt32(116)>0&&view.ReadUInt32(120)>0&&view.ReadUInt32(124)==0&&view.ReadUInt32(128)==0;
            Snapshot.Resolution = (stereo?"Stereo eyes ":"Outer ") + view.ReadUInt32(108) + "x" + view.ReadUInt32(112) + " / " + view.ReadUInt32(116) + "x" + view.ReadUInt32(120) + (stereo?"":"  Focus " + view.ReadUInt32(124) + "x" + view.ReadUInt32(128));
            string diagnostic;
            if (Snapshot.NativeStatus == 4) diagnostic = "XR frame call failed; see logs/quad-*.log (XrResult " + Snapshot.LastXrResult + ").";
            else if (Snapshot.DiagnosticReason == 1) diagnostic = "Stale/invalid capture; inspect logs/quad-*.log.";
            else if (Snapshot.DiagnosticReason == 2) diagnostic = "Invalid eye/resource identity; inspect logs/quad-*.log.";
            else if (Snapshot.DiagnosticReason == 3) diagnostic = "Invalid view pose; inspect logs/quad-*.log.";
            else if (Snapshot.NativeStatus == 3 || Snapshot.DiagnosticReason == 4) diagnostic = "Overlay unavailable; scene may still render. See logs/quad-*.log.";
            else if (Snapshot.NativeStatus == 1) diagnostic = "Benchmark initializing; waiting for scene.";
            else if (Snapshot.NativeStatus == 2 || Snapshot.ProjectionCount == 0) diagnostic = "No submitted scene projections; check scene capture in logs/quad-*.log.";
            else if (view.ReadInt32(88) == 0) diagnostic = "XR session is not running.";
            else diagnostic = "Scene projections submitted; headset output is not observable here.";
            if (dropped > 0) diagnostic += " Ring loss: " + dropped + ".";
            if (!BenchmarkStatistics.IsValue(Snapshot.GpuSpanMs)) diagnostic += " GPU timing unavailable.";
            Snapshot.Diagnostic = diagnostic; status = diagnostic;
        }

        public void ToggleOverlay() { OverlayVisible = !OverlayVisible; }
        public void ToggleRecording()
        {
            if (disposed) return;
            if (capture != null) { try { ReadSamples(DateTime.UtcNow); StopCapture(false, "Manual stop"); WriteConfigSafely(); } catch (Exception ex) { Disconnect("Recording connection interrupted: " + ex.Message, true); } return; }
            if (!Connected) { status = "Recording requires a connected LMU benchmark session."; return; }
            // Drain already published samples before marking the manual start boundary.
            try { ReadSamples(DateTime.UtcNow); } catch (Exception ex) { Disconnect("Recording connection interrupted: " + ex.Message, true); return; }
            if (!Connected) return;
            captureDroppedStart = dropped;
            lock (completedLock) reportError = null;
            capture = new BenchmarkCapture(root, ReportCompleted, Snapshot.Resolution); status = "Recording manual lap / scene capture."; WriteConfigSafely();
        }
        void ReportCompleted(string path, string error) { lock (completedLock) { if (path != null) { reportPath = path; completedPath = path; } if (error != null) reportError = error; } }
        void StopCapture(bool partial, string reason)
        {
            BenchmarkCapture current = capture; capture = null;
            if (current != null) { current.Stop(partial, reason, dropped - captureDroppedStart, frequency); status = "Saving " + (partial ? "partial " : "") + "benchmark report..."; }
        }
        void Disconnect(string reason, bool partial)
        {
            StopCapture(partial, reason);
            if (view != null) { bool previousVisible = visible; try { visible = false; WriteConfig(); } catch { } finally { visible = previousVisible; } view.Dispose(); view = null; }
            if (mapping != null) { mapping.Dispose(); mapping = null; }
            if (process != null) { process.Dispose(); process = null; }
            history.Clear(); Snapshot = new BenchmarkSnapshot(); status = reason; nextDiscover = DateTime.UtcNow.AddSeconds(2);
        }
        void WriteConfigSafely() { if (view == null || disposed) return; try { WriteConfig(); } catch (Exception ex) { Disconnect("Unable to update overlay: " + ex.Message, true); } }
        void WriteConfig()
        {
            int seq = view.ReadInt32(40); seq = (seq & ~1) + 1;
            view.Write(40, seq); Thread.MemoryBarrier();
            view.Write(44, visible ? 1 : 0); view.Write(48, template); view.Write(52, Recording ? 1 : 0);
            view.Write(56, x); view.Write(60, y); view.Write(64, distance); view.Write(68, width);
            Thread.MemoryBarrier(); view.Write(40, unchecked(seq + 1));
        }
        void WriteBitmap(Bitmap bitmap)
        {
            Rectangle rect = new Rectangle(0, 0, bitmap.Width, bitmap.Height);
            BitmapData data = bitmap.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppPArgb);
            try
            {
                byte[] bytes = new byte[bitmap.Width * bitmap.Height * 4];
                for (int row = 0; row < bitmap.Height; row++) Marshal.Copy(IntPtr.Add(data.Scan0, row * data.Stride), bytes, row * bitmap.Width * 4, bitmap.Width * 4);
                int seq = (view.ReadInt32(72) & ~1) + 1; view.Write(72, seq); Thread.MemoryBarrier();
                view.WriteArray(BenchmarkAbi.BitmapOffset, bytes, 0, bytes.Length); view.Write(76, bitmap.Width); view.Write(80, bitmap.Height);
                Thread.MemoryBarrier(); view.Write(72, unchecked(seq + 1));
            }
            finally { bitmap.UnlockBits(data); }
        }
        public Bitmap RenderPreview() { return BenchmarkOverlay.Render(Snapshot, template, Recording, Connected); }
        public void OpenLastReport()
        {
            try { string path = LastReportPath; if (!String.IsNullOrEmpty(path) && File.Exists(path)) Process.Start(new ProcessStartInfo(path) { UseShellExecute = true }); else status = "No completed report yet."; }
            catch (Exception ex) { status = "Could not open report: " + ex.Message; }
        }
        public void Dispose() { if (disposed) return; Disconnect("Launcher closed", true); disposed = true; }
    }

    internal sealed class BenchmarkStatistics
    {
        internal int Count;
        internal double Mean, P50, P95, P99, Max;
        internal static bool IsValue(double n) { return !Double.IsNaN(n) && !Double.IsInfinity(n) && n >= 0; }
        internal static BenchmarkStatistics Calculate(IEnumerable<double> values)
        {
            List<double> sorted = new List<double>(); double sum = 0;
            foreach (double n in values) if (IsValue(n)) { sorted.Add(n); sum += n; }
            sorted.Sort();
            var stats = new BenchmarkStatistics { Count = sorted.Count, Mean = Double.NaN, P50 = Double.NaN, P95 = Double.NaN, P99 = Double.NaN, Max = Double.NaN };
            if (sorted.Count == 0) return stats;
            stats.Mean = sum / sorted.Count; stats.P50 = sorted[(int)Math.Ceiling(sorted.Count * .50) - 1]; stats.P95 = sorted[(int)Math.Ceiling(sorted.Count * .95) - 1]; stats.P99 = sorted[(int)Math.Ceiling(sorted.Count * .99) - 1]; stats.Max = sorted[sorted.Count - 1]; return stats;
        }
    }

    // One foreground worker owns all disk IO and report computation. UI adds bounded structs only.
    // On graceful launcher exit the worker finishes its final flush before the process exits.
    internal sealed class BenchmarkCapture
    {
        internal const int MaxSamples = 180000;
        readonly object gate = new object();
        readonly List<BenchmarkSample> samples = new List<BenchmarkSample>(8192);
        readonly Thread worker;
        readonly Action<string, string> completed;
        readonly string root;
        readonly DateTime started = DateTime.UtcNow;
        readonly Stopwatch duration = Stopwatch.StartNew();
        readonly string resolution;
        bool stopped, partial;
        string reason;
        long dropped, truncated, frequency;
        internal BenchmarkCapture(string root, Action<string, string> completed, string resolution = null)
        {
            this.root = root; this.completed = completed; this.resolution = resolution ?? "Unavailable at capture start";
            worker = new Thread(Run) { IsBackground = false, Name = "LMU benchmark report" }; worker.Start();
        }
        internal long Count { get { lock (gate) return samples.Count; } }
        internal bool HasStopped { get { lock (gate) return stopped; } }
        internal void Add(BenchmarkSample sample) { lock (gate) { if (stopped) return; if (samples.Count < MaxSamples) samples.Add(sample); else truncated++; } }
        internal void Stop(bool partial, string reason, long dropped, long frequency) { lock (gate) { if (stopped) return; this.partial = partial; this.reason = reason; this.dropped = dropped; this.frequency = frequency; duration.Stop(); stopped = true; Monitor.PulseAll(gate); } }
        internal bool Wait(int milliseconds) { return worker.Join(milliseconds); }
        void Run()
        {
            string html = null, error = null;
            try
            {
                string directory = Path.Combine(root, "benchmarks"); Directory.CreateDirectory(directory);
                string stem = Path.Combine(directory, "benchmark-" + started.ToString("yyyyMMdd-HHmmss-fff", CultureInfo.InvariantCulture) + "-" + Guid.NewGuid().ToString("N").Substring(0, 6));
                int written = 0;
                using (var stream = new FileStream(stem + ".csv", FileMode.CreateNew, FileAccess.Write, FileShare.Read))
                using (var writer = new StreamWriter(stream, new UTF8Encoding(false)))
                {
                    writer.WriteLine("counter,qpc,frame_id,scene_interval_ms,app_wall_ms,provider_endframe_wall_ms,gpu_span_ms,gpu_source_frame_id,flags,xr_result");
                    while (true)
                    {
                        BenchmarkSample[] batch; bool done;
                        lock (gate) { batch = samples.GetRange(written, samples.Count - written).ToArray(); done = stopped; }
                        foreach (BenchmarkSample sample in batch) writer.WriteLine(BenchmarkReport.Csv(sample));
                        written += batch.Length; writer.Flush(); stream.Flush(true);
                        if (done) break;
                        lock (gate) { if (!stopped) Monitor.Wait(gate, 2000); }
                    }
                }
                BenchmarkSample[] all;
                lock (gate) all = samples.ToArray();
                BenchmarkReport.Write(stem, all, started, partial, reason, dropped, truncated, frequency, duration.Elapsed.TotalSeconds, resolution);
                html = stem + ".html";
            }
            catch (Exception ex) { error = ex.Message; }
            finally { lock (gate) stopped = true; completed(html, error); }
        }
    }

    internal static class BenchmarkReport
    {
        internal const string Limits = "Scene intervals measure application scene cadence, not displayed headset FPS. Scene samples may include frames without a submitted scene projection or with failed XR calls; these counts are shown explicitly and can overlap. App wall time spans BeginFrame through pre-submit and includes waits; it is not CPU utilization. Provider EndFrame wall time can include runtime waits. GPU spans are sparse delayed query results with source frame IDs; they can include idle gaps and exclude downstream provider/compositor work. They are neither total GPU frame time nor GPU utilization. Menu/non-scene samples are counted separately and excluded from scene statistics. Missing measurements are omitted, never replaced with zero. Percentiles use nearest rank. The graph preserves each bucket's maximum interval. Elapsed capture time uses a monotonic clock; sample span covers retained timestamps only. Rendered dimensions are the values observed at capture start. No headset/compositor visibility is inferred.";
        internal static string Html(string value) { return (value ?? "").Replace("&", "&amp;").Replace("<", "&lt;").Replace(">", "&gt;").Replace("\"", "&quot;").Replace("'", "&#39;"); }
        internal static string Json(string value)
        {
            StringBuilder b = new StringBuilder("\"");
            foreach (char c in value ?? "") { if (c == '"') b.Append("\\\""); else if (c == '\\') b.Append("\\\\"); else if (c == '\n') b.Append("\\n"); else if (c == '\r') b.Append("\\r"); else if (c == '\t') b.Append("\\t"); else if (c < 32 || c == '<' || c == '>' || c == '&') b.Append("\\u" + ((int)c).ToString("x4")); else b.Append(c); }
            return b.Append('"').ToString();
        }
        internal static string Number(double value) { return BenchmarkStatistics.IsValue(value) ? value.ToString("0.######", CultureInfo.InvariantCulture) : "null"; }
        internal static string Display(double value) { return BenchmarkStatistics.IsValue(value) ? value.ToString("0.00", CultureInfo.InvariantCulture) : "Unavailable"; }
        internal static string Csv(BenchmarkSample s)
        {
            return String.Join(",", new string[] { s.Counter.ToString(CultureInfo.InvariantCulture), s.Qpc.ToString(CultureInfo.InvariantCulture), s.FrameId.ToString(CultureInfo.InvariantCulture), Cell(s.IntervalMs), Cell(s.AppWallMs), Cell(s.ProviderWallMs), Cell(s.GpuMs), s.GpuFrameId.ToString(CultureInfo.InvariantCulture), s.Flags.ToString(CultureInfo.InvariantCulture), s.Result.ToString(CultureInfo.InvariantCulture) });
        }
        static string Cell(double value) { return BenchmarkStatistics.IsValue(value) ? Number(value) : ""; }
        static string StatsJson(BenchmarkStatistics s) { return "{\"count\":" + s.Count + ",\"mean\":" + Number(s.Mean) + ",\"p50\":" + Number(s.P50) + ",\"p95\":" + Number(s.P95) + ",\"p99\":" + Number(s.P99) + ",\"max\":" + Number(s.Max) + "}"; }
        internal static void Write(string stem, BenchmarkSample[] samples, DateTime started, bool partial, string reason, long dropped, long truncated, long frequency, double duration, string resolution)
        {
            List<double> interval = new List<double>(), app = new List<double>(), provider = new List<double>(), gpu = new List<double>();
            long scenes = 0, menus = 0, submitted = 0, failed = 0;
            foreach (BenchmarkSample s in samples) { if (!s.Scene) { menus++; continue; } scenes++; if ((s.Flags & 4) != 0) submitted++; if (s.Result < 0) failed++; interval.Add(s.IntervalMs); app.Add(s.AppWallMs); provider.Add(s.ProviderWallMs); if ((s.Flags & 8) != 0) gpu.Add(s.GpuMs); }
            BenchmarkStatistics[] stats = { BenchmarkStatistics.Calculate(interval), BenchmarkStatistics.Calculate(app), BenchmarkStatistics.Calculate(provider), BenchmarkStatistics.Calculate(gpu) };
            double fps = stats[0].Count > 0 && stats[0].Mean > 0 ? 1000 / stats[0].Mean : Double.NaN;
            string[] labels = { "Scene frame interval", "CPU-side app wall (includes waits)", "Provider EndFrame wall", "Sparse GPU span" };
            string[] keys = { "sceneInterval", "appWall", "providerWall", "gpuSpan" };
            double sampleSpan = samples.Length > 1 && frequency > 0 ? (samples[samples.Length - 1].Qpc - samples[0].Qpc) / (double)frequency : 0;
            var json = new StringBuilder("{\"schemaVersion\":1,\"startedUtc\":").Append(Json(started.ToString("o"))).Append(",\"partial\":").Append(partial ? "true" : "false").Append(",\"stopReason\":").Append(Json(reason)).Append(",\"qpcFrequency\":").Append(frequency).Append(",\"samples\":").Append(samples.Length).Append(",\"sceneSamples\":").Append(scenes).Append(",\"menuSamples\":").Append(menus).Append(",\"droppedSamples\":").Append(dropped).Append(",\"truncatedSamples\":").Append(truncated).Append(",\"sampleCap\":").Append(BenchmarkCapture.MaxSamples).Append(",\"applicationFps\":").Append(Number(fps));
            for (int i = 0; i < 4; i++) json.Append(',').Append(Json(keys[i])).Append(':').Append(StatsJson(stats[i]));
            json.Append(",\"elapsedSeconds\":").Append(Number(duration)).Append(",\"sampleSpanSeconds\":").Append(Number(sampleSpan)).Append(",\"renderedResolutionAtStart\":").Append(Json(resolution)).Append(",\"submittedSceneSamples\":").Append(submitted).Append(",\"unsubmittedSceneSamples\":").Append(scenes - submitted).Append(",\"failedSceneSamples\":").Append(failed);
            json.Append(",\"measurementLimits\":").Append(Json(Limits)).Append('}');
            File.WriteAllText(stem + ".json", json.ToString(), new UTF8Encoding(false));
            var html = new StringBuilder("<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>LMU QuadView benchmark</title><style>body{margin:0;background:#0f0f12;color:#f4f4f6;font:15px 'Inter','Segoe UI',sans-serif}main{max-width:1050px;margin:48px auto;padding:0 28px}header{border-bottom:1px solid #32343c;padding-bottom:24px}small,.muted{color:#9da0aa}h1{font-size:32px;letter-spacing:-1px;margin:12px 0}h2{font-size:19px}section{background:#1a1b1f;border:1px solid #32343c;border-radius:16px;padding:24px;margin-top:20px}.accent,a{color:#a79bf0}.hero{font:56px 'DM Mono',Consolas,monospace}.warn{color:#f87171}table{width:100%;border-collapse:collapse;text-align:right;font:14px Consolas,monospace}th,td{padding:12px 8px;border-bottom:1px solid #32343c}th:first-child,td:first-child{text-align:left;font-family:'Segoe UI',sans-serif}svg{width:100%;height:auto}p{line-height:1.6}.overflow{overflow-x:auto}</style><main><header><small class=\"accent\">LMU QUADVIEW / BENCHMARK</small><h1>").Append(partial ? "Partial capture" : "Manual scene capture").Append("</h1><p class=\"muted\">").Append(Html(started.ToString("yyyy-MM-dd HH:mm:ss 'UTC'"))).Append(" &middot; ").Append(Html(reason)).Append("</p></header><section><div class=\"hero\">").Append(Display(fps)).Append(" <span class=\"muted\" style=\"font-size:22px\">app FPS</span></div><p>Derived from mean scene interval &middot; ").Append(scenes).Append(" scene samples &middot; ").Append(menus).Append(" menu/non-scene samples</p><p class=\"").Append(dropped > 0 || truncated > 0 ? "warn" : "muted").Append("\">Overwritten ring samples: ").Append(dropped).Append(" &middot; Samples omitted at capture cap: ").Append(truncated).Append(" (cap ").Append(BenchmarkCapture.MaxSamples).Append("). Statistics cover retained samples only.</p></section><section><h2>Timing distribution <small>milliseconds</small></h2><div class=\"overflow\"><table><thead><tr><th>Measurement</th><th>Count</th><th>Mean</th><th>p50</th><th>p95</th><th>p99</th><th>Max</th></tr></thead><tbody>");
            for (int i = 0; i < 4; i++) { var s = stats[i]; html.Append("<tr><td>").Append(labels[i]).Append("</td><td>").Append(s.Count).Append("</td><td>").Append(Display(s.Mean)).Append("</td><td>").Append(Display(s.P50)).Append("</td><td>").Append(Display(s.P95)).Append("</td><td>").Append(Display(s.P99)).Append("</td><td>").Append(Display(s.Max)).Append("</td></tr>"); }
            string captureMetadata = "<section><h2>Capture context</h2><p>Elapsed capture: " + Display(duration) + " seconds &middot; Retained sample span: " + Display(sampleSpan) + " seconds</p><p>Rendered dimensions at start: " + Html(resolution) + "</p><p class=\"" + (scenes > submitted || failed > 0 ? "warn" : "muted") + "\">Scene samples with submitted scene projection: " + submitted + " &middot; Without submitted scene projection: " + (scenes - submitted) + " &middot; Failed XR calls in scene samples: " + failed + ". Submission does not confirm displayed headset output.</p></section>";
            int firstSectionEnd = html.ToString().IndexOf("</section>", StringComparison.Ordinal) + "</section>".Length;
            html.Insert(firstSectionEnd, captureMetadata);
            html.Append("</tbody></table></div></section><section><h2>Scene frame interval</h2>").Append(Graph(interval)).Append("<p class=\"muted\">Horizontal axis: scene sample order. Vertical axis: milliseconds. Up to 900 buckets; each shows its maximum to retain spikes.</p></section><section><h2>Measurement limits</h2><p>").Append(Html(Limits)).Append("</p><p><a href=\"").Append(Html(Path.GetFileName(stem))).Append(".csv\">Raw CSV checkpoint</a> &middot; <a href=\"").Append(Html(Path.GetFileName(stem))).Append(".json\">Summary JSON</a></p></section></main></html>");
            File.WriteAllText(stem + ".html", html.ToString(), new UTF8Encoding(false));
        }
        static string Graph(List<double> values)
        {
            double max = 0; foreach (double v in values) if (BenchmarkStatistics.IsValue(v)) max = Math.Max(max, v);
            if (max <= 0) return "<p class=\"muted\">No valid scene intervals captured.</p>";
            int buckets = Math.Min(900, values.Count); StringBuilder points = new StringBuilder();
            for (int b = 0; b < buckets; b++) { double peak = Double.NaN; int start = b * values.Count / buckets, end = (b + 1) * values.Count / buckets; for (int i = start; i < end; i++) if (BenchmarkStatistics.IsValue(values[i])) peak = Double.IsNaN(peak) ? values[i] : Math.Max(peak, values[i]); if (Double.IsNaN(peak)) continue; points.Append((50 + 890.0 * b / Math.Max(1, buckets - 1)).ToString("0.0", CultureInfo.InvariantCulture)).Append(',').Append((230 - 200 * peak / max).ToString("0.0", CultureInfo.InvariantCulture)).Append(' '); }
            return "<svg viewBox=\"0 0 960 260\" role=\"img\" aria-label=\"Scene frame interval graph\"><path d=\"M50 25V230H945\" fill=\"none\" stroke=\"#32343c\"/><text x=\"0\" y=\"25\" fill=\"#9da0aa\" font-size=\"12\">" + Display(max) + "</text><text x=\"10\" y=\"235\" fill=\"#9da0aa\" font-size=\"12\">0</text><polyline fill=\"none\" stroke=\"#a79bf0\" stroke-width=\"2\" points=\"" + points + "\"/></svg>";
        }
    }

    internal static class BenchmarkOverlay
    {
        static readonly Color Background = Color.FromArgb(15, 15, 18), Card = Color.FromArgb(26, 27, 31), Border = Color.FromArgb(50, 52, 60), Text = Color.FromArgb(244, 244, 246), Muted = Color.FromArgb(157, 160, 170), Accent = Color.FromArgb(167, 155, 240), Red = Color.FromArgb(248, 113, 113);
        static string PickFont(params string[] choices)
        {
            using (InstalledFontCollection fonts = new InstalledFontCollection()) foreach (string choice in choices) foreach (FontFamily family in fonts.Families) if (String.Equals(family.Name, choice, StringComparison.OrdinalIgnoreCase)) return choice;
            return FontFamily.GenericSansSerif.Name;
        }
        static readonly string Sans = PickFont("Archivo", "Inter", "Segoe UI"), Mono = PickFont("DM Mono", "Consolas");
        internal static Bitmap Render(BenchmarkSnapshot s, int template, bool recording, bool connected)
        {
            bool detailed = template == 1; Bitmap bitmap = new Bitmap(768, detailed ? 512 : 256, PixelFormat.Format32bppPArgb);
            using (Graphics g = Graphics.FromImage(bitmap))
            {
                g.Clear(Background); g.SmoothingMode = SmoothingMode.AntiAlias; g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
                Panel(g, new RectangleF(12, 12, 744, bitmap.Height - 24));
                Draw(g, "LMU QUADVIEW", 30, 26, 20, Accent, false, 340, 28);
                using (Brush dot = new SolidBrush(recording ? Red : Muted)) g.FillEllipse(dot, 544, 33, 10, 10);
                Draw(g, recording ? "RECORDING" : "STANDBY", 562, 25, 18, recording ? Red : Muted, true, 175, 30);
                Draw(g, BenchmarkStatistics.IsValue(s.Fps) ? s.Fps.ToString("0.0", CultureInfo.InvariantCulture) : "--", 28, 58, 68, Text, true, 360, 86);
                Draw(g, "APP FPS", 34, 148, 17, Muted, false, 250, 30);
                Draw(g, Value(s.FrameMs), 430, 78, 37, Text, true, 290, 58);
                Draw(g, "SCENE INTERVAL / ms", 432, 145, 16, Muted, false, 290, 28);
                if (!detailed)
                {
                    Line(g, 30, 185, 738, 185);
                    Draw(g, connected ? CompactStatus(s) : "Waiting for LMU benchmark connection", 32, 195, 14, Muted, false, 702, 22);
                    Draw(g, connected?FocusText(s):"Center FOV --",32,217,16,Accent,false,704,25);
                }
                else
                {
                    Line(g, 30, 184, 738, 184);
                    Metric(g, 34, "CPU-SIDE APP WALL", s.AppWallMs, "includes waits");
                    Metric(g, 278, "PROVIDER WALL", s.ProviderWallMs, "EndFrame / waits");
                    Metric(g, 522, "SPARSE GPU SPAN", s.GpuSpanMs, BenchmarkStatistics.IsValue(s.GpuSpanMs) ? "source frame " + s.GpuSourceFrame : "no new query result");
                    Line(g, 30, 314, 738, 314);
                    Draw(g, "FRAME / ms", 32, 326, 14, Muted, true, 140, 25);
                    History(g, s.FrameHistory, new RectangleF(180, 326, 550, 48));
                    Draw(g, "PROJECTIONS " + s.ProjectionCount + "     RING LOSS " + s.DroppedSamples + "     SAMPLES " + s.RecordedSamples, 32, 384, 15, Accent, true, 704, 25);
                    Draw(g, s.Resolution, 32, 411, 14, Muted, true, 704, 24);
                    Draw(g, connected?FocusText(s):"Center FOV --",32,434,16,Accent,false,704,25);
                    Draw(g, connected ? s.Diagnostic : "Enable Benchmark, then launch LMU with diagnostics enabled.", 32, 460, 12, Muted, false, 700, 22);
                    Draw(g, (connected&&s.FocusFovValid?"FOV frame "+s.FocusFrame+" | ":"")+"GPU excludes provider / compositor.", 32, 486, 11, Muted, false, 705, 16);
                }
            }
            return bitmap;
        }
        internal static string FocusText(BenchmarkSnapshot s)
        {
            if(!s.FocusFovValid)return "Center FOV -- (no rendered quad scene)";
            return String.Format(CultureInfo.InvariantCulture,"Center {0:0.#}% {1} | L {2:0.0} x {3:0.0} deg | R {4:0.0} x {5:0.0} deg",
                s.FocusScalePercent,s.DynamicFocus?(s.DynamicFocusHolding?"HOLD":"AUTO"):"FIXED",
                s.FocusLeftHorizontal,s.FocusLeftVertical,s.FocusRightHorizontal,s.FocusRightVertical);
        }
        static string CompactStatus(BenchmarkSnapshot s)
        {
            if (s.NativeStatus == 4) return "XR frame call failed (XrResult " + s.LastXrResult + ")";
            if (s.NativeStatus == 3 || s.DiagnosticReason == 4) return "Overlay update failed; inspect logs/quad-*.log";
            if (s.DiagnosticReason == 1) return "Stale or invalid scene capture";
            if (s.DiagnosticReason == 2) return "Invalid eye / resource identity";
            if (s.DiagnosticReason == 3) return "Invalid view pose";
            return s.ProjectionCount == 0 ? "No submitted scene projections" : "Projections " + s.ProjectionCount + "   |   Ring loss " + s.DroppedSamples + (BenchmarkStatistics.IsValue(s.GpuSpanMs) ? "" : "   |   GPU unavailable");
        }
        static string Value(double value) { return BenchmarkStatistics.IsValue(value) ? value.ToString("0.00", CultureInfo.InvariantCulture) : "--"; }
        static void History(Graphics g, double[] values, RectangleF bounds)
        {
            using (Pen grid = new Pen(Border)) g.DrawLine(grid, bounds.Left, bounds.Bottom, bounds.Right, bounds.Bottom);
            double max = 16.67; foreach (double value in values) if (BenchmarkStatistics.IsValue(value)) max = Math.Max(max, value);
            using (Pen pen = new Pen(Accent, 2))
            {
                PointF previous = new PointF(); bool havePrevious = false;
                for (int i = 0; i < values.Length; i++)
                {
                    if (!BenchmarkStatistics.IsValue(values[i])) { havePrevious = false; continue; }
                    PointF point = new PointF(bounds.Left + bounds.Width * i / Math.Max(1, values.Length - 1), bounds.Bottom - bounds.Height * (float)(values[i] / max));
                    if (havePrevious) g.DrawLine(pen, previous, point); previous = point; havePrevious = true;
                }
            }
        }
        static void Metric(Graphics g, int x, string title, double value, string note) { Draw(g, title, x, 202, 14, Muted, false, 225, 24); Draw(g, Value(value), x, 230, 32, Text, true, 220, 47); Draw(g, note, x, 279, 13, Muted, false, 225, 24); }
        static void Draw(Graphics g, string text, float x, float y, float size, Color color, bool mono, float width, float height)
        {
            using (Font font = new Font(mono ? Mono : Sans, size, FontStyle.Regular, GraphicsUnit.Pixel)) using (Brush brush = new SolidBrush(color)) using (StringFormat format = new StringFormat()) { format.Trimming = StringTrimming.EllipsisCharacter; g.DrawString(text ?? "", font, brush, new RectangleF(x, y, width, height), format); }
        }
        static void Line(Graphics g, int x, int y, int x2, int y2) { using (Pen pen = new Pen(Border)) g.DrawLine(pen, x, y, x2, y2); }
        static void Panel(Graphics g, RectangleF r)
        {
            using (GraphicsPath path = new GraphicsPath()) { const float d = 28; path.AddArc(r.X, r.Y, d, d, 180, 90); path.AddArc(r.Right - d, r.Y, d, d, 270, 90); path.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90); path.AddArc(r.X, r.Bottom - d, d, d, 90, 90); path.CloseFigure(); using (Brush brush = new SolidBrush(Card)) g.FillPath(brush, path); using (Pen pen = new Pen(Border)) g.DrawPath(pen, path); }
        }
    }
}
