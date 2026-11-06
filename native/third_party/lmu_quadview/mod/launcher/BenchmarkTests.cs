using System;
using System.Drawing;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Threading;
using LmuQuadViewLauncher;

class BenchmarkTests
{
    static void Check(bool ok, string message) { if (!ok) throw new Exception(message); }
    static void Header(MemoryMappedViewAccessor view, int pid)
    { view.Write(0, BenchmarkAbi.Magic); view.Write(4, 1); view.Write(8, BenchmarkAbi.Size); view.Write(12, pid); view.Write(16, 1000L); view.Write(88, 1); view.Write(96, 1); }
    static void Publish(MemoryMappedViewAccessor view, long counter)
    {
        long offset = BenchmarkAbi.HeaderSize + ((counter - 1) % BenchmarkAbi.Slots) * BenchmarkAbi.SlotSize;
        view.Write(offset, -counter); view.Write(offset + 8, counter * 10); view.Write(offset + 16, (ulong)counter);
        view.Write(offset + 24, 10.0); view.Write(offset + 32, 5.0); view.Write(offset + 40, 1.0); view.Write(offset + 48, Double.NaN); view.Write(offset + 64, 1U);
        Thread.MemoryBarrier(); view.Write(offset, counter); view.Write(24, counter);
    }
    static string AwaitReport(BenchmarkController controller, string previous)
    {
        for (int i = 0; i < 1000; i++) { if (controller.LastReportPath.Length > 0 && controller.LastReportPath != previous) return controller.LastReportPath; Thread.Sleep(10); }
        throw new Exception("Controller report timeout: " + controller.Status);
    }
    static void Main(string[] args)
    {
        Check(typeof(BenchmarkSnapshot).GetProperty("FocusFovValid")!=null,"Overlay rendered FOV is missing");
        var stats = BenchmarkStatistics.Calculate(new double[] { 5, 1, 4, 2, 3, Double.NaN });
        Check(stats.Count == 5 && stats.Mean == 3 && stats.P50 == 3 && stats.P95 == 5 && stats.P99 == 5, "Nearest-rank percentiles");
        Check(BenchmarkStatistics.Calculate(new double[] { Double.NaN }).Count == 0, "Missing metrics must stay missing");
        Check(BenchmarkReport.Html("<script>&\"'").Contains("&lt;script&gt;&amp;&quot;&#39;"), "HTML escaping");
        Check(BenchmarkReport.Json("a\n\"b\\").Equals("\"a\\n\\\"b\\\\\""), "JSON escaping");
        Check(BenchmarkAbi.Size == 1900800 && BenchmarkAbi.BitmapOffset == 327936 && BenchmarkAbi.SlotSize == 80, "ABI dimensions");
        using (var map = MemoryMappedFile.CreateNew(null, BenchmarkAbi.Size))
        using (var view = map.CreateViewAccessor())
        {
            view.Write(0, BenchmarkAbi.Magic); view.Write(4, 1); view.Write(8, BenchmarkAbi.Size); view.Write(12, 42); view.Write(16, 1000L);
            Check(BenchmarkReader.Valid(view, 42), "Valid ABI rejected");
            view.Write(4, 2); Check(!BenchmarkReader.Valid(view, 42), "Version mismatch accepted"); view.Write(4, 1);
            long offset = BenchmarkAbi.HeaderSize;
            view.Write(offset, 1L); view.Write(offset + 8, 500L); view.Write(offset + 24, 10.0); view.Write(offset + 48, Double.NaN); view.Write(offset + 64, 1U);
            BenchmarkSample sample;
            Check(BenchmarkReader.TryRead(view, 1, out sample) && sample.IntervalMs == 10, "Committed sample read");
            view.Write(offset, -1L); Check(!BenchmarkReader.TryRead(view, 1, out sample), "In-flight sample accepted");
            long next = 1, dropped = 0;
            BenchmarkReader.SkipOverwritten(5000, ref next, ref dropped);
            Check(next == 905 && dropped == 904, "Ring overrun accounting");
            var focus=new BenchmarkSnapshot();BenchmarkReader.ReadFocus(view,focus);
            Check(!focus.FocusFovValid,"Old producer must not invent FOV telemetry");
            view.Write(136,2);view.Write(140,3U);view.Write(144,80f);
            view.Write(148,45f);view.Write(152,40f);view.Write(156,46f);view.Write(160,41f);view.Write(168,42UL);
            BenchmarkReader.ReadFocus(view,focus);
            Check(focus.FocusFovValid&&focus.FocusScalePercent==80&&focus.FocusLeftHorizontal==45&&focus.FocusRightVertical==41&&focus.FocusFrame==42,"Actual frame FOV extension offsets");
            Check(BenchmarkOverlay.FocusText(focus)=="Center FOV | L 45.0 x 40.0 deg | R 46.0 x 41.0 deg","Overlay must display both actual eye extents");
            view.Write(136,3);BenchmarkReader.ReadFocus(view,focus);Check(!focus.FocusFovValid,"Torn focus snapshot accepted");
            view.Write(136,4);view.Write(140,2U);BenchmarkReader.ReadFocus(view,focus);Check(!focus.FocusFovValid,"Menu/stale focus shown as current");
            view.Write(136,6);view.Write(140,7U);BenchmarkReader.ReadFocus(view,focus);Check(!BenchmarkOverlay.FocusText(focus).Contains("HOLD")&&!BenchmarkOverlay.FocusText(focus).Contains("AUTO"),"Obsolete dynamic flags must not advertise removed functionality");
            view.Write(148,Single.NaN);view.Write(136,8);BenchmarkReader.ReadFocus(view,focus);Check(!focus.FocusFovValid,"Invalid FOV accepted");
        }
        string root = args.Length > 0 ? Path.GetFullPath(args[0]) : Path.Combine(Path.GetTempPath(), "benchmark-tests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        if (args.Length > 1)
        {
            using (var fixture = MemoryMappedFile.CreateFromFile(args[1], FileMode.Open, null, 0, MemoryMappedFileAccess.Read))
            using (var view = fixture.CreateViewAccessor(0, 0, MemoryMappedFileAccess.Read))
            {
                Check(BenchmarkReader.Valid(view, 4242), "Native-produced header ABI");
                BenchmarkSample sample;
                Check(BenchmarkReader.TryRead(view, 1, out sample), "Native-produced commit");
                Check(sample.Qpc == 123456789 && sample.FrameId == 42 && sample.IntervalMs == 11.25 && sample.AppWallMs == 8.5 && sample.ProviderWallMs == 1.75 && sample.GpuMs == 6.125 && sample.GpuFrameId == 40 && sample.Flags == 15 && sample.Result == 0, "Native-produced sample field offsets");
                var focus=new BenchmarkSnapshot();BenchmarkReader.ReadFocus(view,focus);
                Check(focus.FocusFovValid&&focus.FocusScalePercent==80&&focus.FocusLeftHorizontal==45&&focus.FocusRightVertical==41&&focus.FocusFrame==42,"Native-produced FOV extension");
                Console.WriteLine("Native C++ fixture / C# ABI crosscheck passed.");
            }
        }
        using (var map = MemoryMappedFile.CreateNew(null, BenchmarkAbi.Size))
        using (var view = map.CreateViewAccessor())
        using (var controller = new BenchmarkController(root))
        {
            Header(view,42);controller.OverlayVisible=false;controller.AttachMapping(map,42,DateTime.UtcNow);
            // A hidden producer must still consume telemetry/config, but never
            // allocate the large upload buffer or touch the bitmap seqlock/pixels.
            view.Write(BenchmarkAbi.BitmapOffset,unchecked((int)0x12345678));
            long allocated=GC.GetAllocatedBytesForCurrentThread();controller.Tick();
            allocated=GC.GetAllocatedBytesForCurrentThread()-allocated;
            Check(view.ReadInt32(72)==0&&view.ReadInt32(BenchmarkAbi.BitmapOffset)==0x12345678,"Hidden overlay published bitmap pixels");
            Check(allocated<65536,"Hidden tick allocated a bitmap-sized upload buffer: "+allocated);
            Check(view.ReadInt32(44)==0&&(view.ReadInt32(40)&1)==0,"Hidden overlay configuration was not published");
            controller.OverlayVisible=true;controller.Tick();
            int shown=view.ReadInt32(72);
            Check(shown!=0&&(shown&1)==0,"Showing overlay did not publish a fresh bitmap on the next tick");
            controller.OverlayVisible=false;controller.ToggleRecording();
            for(int i=1;i<=3;i++){Publish(view,i);controller.Tick();}
            Check(controller.Recording&&controller.Snapshot.RecordedSamples==3&&controller.Snapshot.FrameMs==10,"Hidden overlay interrupted telemetry or recording");
            Check(view.ReadInt32(52)==1&&view.ReadInt32(44)==0,"Hidden recording configuration was lost");
            Check(view.ReadInt32(72)==shown,"Hidden recording refreshed bitmap pixels");
            using(Bitmap preview=controller.RenderPreview())Check(controller.Snapshot.FrameHistory.Length==3&&preview.Width==768,"Explicit local preview did not use hidden-overlay samples");
            controller.Template=1;controller.OverlayVisible=true;controller.Tick();
            Check(view.ReadInt32(72)!=shown&&view.ReadInt32(80)==512,"Reshown overlay did not immediately publish the current template");
            controller.ToggleRecording();string hiddenReport=AwaitReport(controller,"");
            Check(File.ReadAllText(Path.ChangeExtension(hiddenReport,".json")).Contains("\"samples\":3"),"Hidden recording report lost samples");
        }
        using (var map = MemoryMappedFile.CreateNew(null, BenchmarkAbi.Size))
        using (var view = map.CreateViewAccessor())
        using (var controller = new BenchmarkController(root))
        {
            Header(view, 42); controller.AttachMapping(map, 42, DateTime.UtcNow); controller.Tick();
            view.Write(108,1600U);view.Write(112,1440U);view.Write(116,1600U);view.Write(120,1440U);
            controller.Tick();Check(controller.Snapshot.Resolution=="Stereo eyes 1600x1440 / 1600x1440","Stereo benchmark invents focus targets");
            view.Write(124,1200U);view.Write(128,1200U);controller.Tick();
            Check(controller.Snapshot.Resolution=="Outer 1600x1440 / 1600x1440  Focus 1200x1200","Quad benchmark dimensions regressed");
            view.Write(136,2);view.Write(140,3U);view.Write(144,80f);view.Write(148,45f);view.Write(152,40f);view.Write(156,46f);view.Write(160,41f);view.Write(168,42UL);
            controller.Tick();Check(controller.Snapshot.FocusFovValid,"Live controller omitted actual FOV");
            view.Write(88,0);controller.Tick();Check(!controller.Snapshot.FocusFovValid,"Stopped session retained rendered FOV");view.Write(88,1);
            Check(controller.Connected && view.ReadInt32(44) == 1 && (view.ReadInt32(40) & 1) == 0, "Config seqlock");
            Check(view.ReadInt32(76) == 768 && view.ReadInt32(80) == 256 && (view.ReadInt32(72) & 1) == 0 && view.ReadInt32(72) != 0, "Bitmap seqlock/dimensions");
            int sequence = view.ReadInt32(72); controller.Tick(); Check(view.ReadInt32(72) == sequence, "Bitmap updates exceeded 5Hz");
            controller.ToggleRecording(); Publish(view, 1); controller.ToggleRecording();
            string manual = AwaitReport(controller, "");
            Check(File.ReadAllText(Path.ChangeExtension(manual, ".json")).Contains("\"samples\":1"), "Manual stop must drain published final sample");
            controller.ToggleRecording(); Publish(view, 2); controller.Tick(); view.Write(0, 0U); controller.Tick();
            Check(!controller.Connected && !controller.Recording && controller.OverlayVisible, "Lost connection stops capture and preserves desired visibility");
            string lost = AwaitReport(controller, manual);
            Check(File.ReadAllText(Path.ChangeExtension(lost, ".json")).Contains("\"partial\":true"), "Controller disconnect partial report");
        }
        using (var map = MemoryMappedFile.CreateNew(null, BenchmarkAbi.Size))
        using (var view = map.CreateViewAccessor())
        using (var controller = new BenchmarkController(root))
        {
            Header(view, 42); controller.AttachMapping(map, 42, DateTime.UtcNow.AddSeconds(-6)); controller.ToggleRecording(); controller.Tick();
            Check(!controller.Connected && !controller.Recording && controller.Status.Contains("heartbeat stopped"), "Stalled heartbeat detection");
            Check(File.ReadAllText(Path.ChangeExtension(AwaitReport(controller, ""), ".json")).Contains("\"partial\":true"), "Stalled heartbeat partial report");
        }
        string blockedRoot = Path.Combine(root, "blocked-root.file"); File.WriteAllText(blockedRoot, "fixture: directory creation must fail");
        using (var map = MemoryMappedFile.CreateNew(null, BenchmarkAbi.Size))
        using (var view = map.CreateViewAccessor())
        using (var controller = new BenchmarkController(blockedRoot))
        {
            Header(view, 42); controller.AttachMapping(map, 42, DateTime.UtcNow); controller.ToggleRecording();
            for (int i = 0; i < 200 && controller.Recording; i++) { Thread.Sleep(5); controller.Tick(); }
            Check(!controller.Recording && controller.Status.Contains("Report could not be saved"), "Background IO failure stops capture and remains visible");
        }
        string completed = null, failure = null;
        var capture = new BenchmarkCapture(root, delegate(string path, string error) { completed = path; failure = error; }, "Outer 1600x1440 / 1600x1440  Focus 1200x1200");
        capture.Add(new BenchmarkSample { Counter = 1, Qpc = 10, FrameId = 1, IntervalMs = 10, AppWallMs = 2, ProviderWallMs = 1, GpuMs = Double.NaN, Flags = 5 });
        capture.Add(new BenchmarkSample { Counter = 2, Qpc = 20, FrameId = 2, IntervalMs = 20, AppWallMs = 3, ProviderWallMs = 1, GpuMs = 4, GpuFrameId = 1, Flags = 9, Result = -1 });
        Thread.Sleep(10);
        capture.Stop(true, "Connection lost <fixture>", 7, 1000);
        Check(capture.Wait(10000), "Partial report completion timeout");
        Check(failure == null && completed != null && File.Exists(completed), "Partial report missing: " + failure);
        string report = File.ReadAllText(completed), json = File.ReadAllText(Path.ChangeExtension(completed, ".json"));
        Check(report.Contains("Connection lost &lt;fixture&gt;") && report.Contains("Partial capture") && report.Contains("7"), "Partial report metadata");
        Check(json.Contains("\"partial\":true") && json.Contains("\"gpuSpan\":{\"count\":1"), "Sparse GPU statistics");
        Check(json.Contains("\"sampleSpanSeconds\":0.01") && json.Contains("\"elapsedSeconds\":") && !json.Contains("\"elapsedSeconds\":0,"), "Capture elapsed duration and retained QPC span");
        Check(json.Contains("\"renderedResolutionAtStart\":\"Outer 1600x1440 / 1600x1440  Focus 1200x1200\"") && report.Contains("Rendered dimensions at start: Outer 1600x1440"), "Rendered dimensions metadata in partial report");
        Check(json.Contains("\"submittedSceneSamples\":1") && json.Contains("\"unsubmittedSceneSamples\":1") && json.Contains("\"failedSceneSamples\":1"), "Submission and failed frame counts");
        Check(File.ReadAllLines(Path.ChangeExtension(completed, ".csv")).Length == 3, "CSV checkpoint rows");
        string bounded = null;
        var boundedCapture = new BenchmarkCapture(root, delegate(string path, string error) { bounded = path; });
        for (int i = 0; i < BenchmarkCapture.MaxSamples + 3; i++) boundedCapture.Add(new BenchmarkSample { Counter = i + 1, Flags = 1, IntervalMs = 10, GpuMs = Double.NaN });
        boundedCapture.Stop(false, "Cap fixture", 0, 1000);
        Check(boundedCapture.Wait(15000) && bounded != null, "Bounded capture report");
        Check(File.ReadAllText(Path.ChangeExtension(bounded, ".json")).Contains("\"truncatedSamples\":3"), "Capture truncation must be explicit");
        using (var controller = new BenchmarkController(root))
        {
            Check(!controller.Connected && !controller.Recording, "Constructor side effects");
            controller.Template = 0;
            using (Bitmap bmp = controller.RenderPreview()) { Check(bmp.Width <= 768 && bmp.Height <= 512 && bmp.GetPixel(0, 0).A == 255, "Minimal opaque bitmap bounds"); bmp.Save(Path.Combine(root, "minimal.png")); }
            controller.Template = 1;
            using (Bitmap bmp = controller.RenderPreview()) { Check(bmp.Width <= 768 && bmp.Height <= 512, "Detailed bitmap bounds"); bmp.Save(Path.Combine(root, "detailed.png")); }
        }
        var live = new BenchmarkSnapshot { Fps = 89.9, FrameMs = 11.12, AppWallMs = 7.52, ProviderWallMs = 1.24, GpuSpanMs = 6.31, GpuSourceFrame = 4932, ProjectionCount = 1, RecordedSamples = 5941, Resolution = "Outer 1600x1440 / 1600x1440  Focus 1200x1200", Diagnostic = "Scene projections submitted; headset output is not observable here.", FrameHistory = new double[] { 11, 11.1, 11.3, 10.9, 11.1, 14, 11.4, 11.2, 11.1, 11.3, 11.1 } };
        live.FocusFovValid=true;live.FocusScalePercent=80;live.FocusLeftHorizontal=51.4;live.FocusLeftVertical=45.2;live.FocusRightHorizontal=50.8;live.FocusRightVertical=45.2;live.FocusFrame=5941;
        using (Bitmap bmp = BenchmarkOverlay.Render(live, 1, true, true)) bmp.Save(Path.Combine(root, "detailed-live.png"));
        using (Bitmap bmp = BenchmarkOverlay.Render(live, 0, true, true)) bmp.Save(Path.Combine(root, "minimal-live.png"));
        Console.WriteLine("Benchmark tests passed: ABI, seqlock, overruns, percentiles, missing values, escaping, partial capture, CSV and bitmap previews. Artifacts: " + root);
    }
}
