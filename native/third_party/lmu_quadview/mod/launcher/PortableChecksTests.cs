using System;
using System.IO;
using System.IO.MemoryMappedFiles;
using LmuQuadViewLauncher;

// 0.5.1: checks that need no Windows-only fixtures (also run under Mono).
class PortableChecksTests
{
    static void Require(bool value, string name) { if (!value) throw new Exception(name); }
    static void Main()
    {
        string root = Path.Combine(Path.GetTempPath(), "lmu-portable-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            // Implausible benchmark timings become missing values.
            string ring = Path.Combine(root, "ring.bin");
            using (var fs = new FileStream(ring, FileMode.Create)) fs.SetLength(BenchmarkAbi.Size);
            using (var file = MemoryMappedFile.CreateFromFile(ring, FileMode.Open, null, 0, MemoryMappedFileAccess.ReadWrite))
            using (var view = file.CreateViewAccessor(0, 0))
            {
                long o = BenchmarkAbi.HeaderSize;
                view.Write(o, 1L); view.Write(o + 24, -5.0); view.Write(o + 32, 1e9); view.Write(o + 40, 1.5); view.Write(o + 48, Double.PositiveInfinity); view.Write(o + 64, 9u);
                BenchmarkSample s;
                Require(BenchmarkReader.TryRead(view, 1, out s), "valid slot not read");
                Require(Double.IsNaN(s.IntervalMs) && Double.IsNaN(s.AppWallMs) && s.ProviderWallMs == 1.5 && Double.IsNaN(s.GpuMs), "implausible timing accepted");
                view.Write(o + 24, 11.25); view.Write(o + 48, 6.5);
                Require(BenchmarkReader.TryRead(view, 1, out s) && s.IntervalMs == 11.25 && s.GpuMs == 6.5, "plausible timing rejected");
            }

            // Old update downloads are pruned; recent, foreign and non-installer folders stay.
            string updates = Path.Combine(root, "updates"); Directory.CreateDirectory(updates);
            DateTime now = DateTime.UtcNow;
            Func<string, string, DateTime, string> folder = (name, file, time) => {
                var d = Path.Combine(updates, name); Directory.CreateDirectory(d);
                File.WriteAllText(Path.Combine(d, file), "x"); Directory.SetLastWriteTimeUtc(d, time); return d;
            };
            var old = folder(new string('a', 32), "LMU-Quad-View-Setup.exe", now.AddDays(-10));
            var recent = folder(new string('b', 32), "LMU-Quad-View-Setup.exe", now.AddDays(-1));
            var foreign = folder("my-installers", "Setup.exe", now.AddDays(-30));
            var other = folder(new string('c', 32), "notes.txt", now.AddDays(-30));
            UpdateService.PruneDownloads(updates, now);
            Require(!Directory.Exists(old), "old download kept");
            Require(Directory.Exists(recent) && Directory.Exists(foreign) && Directory.Exists(other), "wrong folder removed");
            UpdateService.PruneDownloads(Path.Combine(root, "missing"), now); // must not throw
            Console.WriteLine("Portable checks passed: benchmark plausibility, update download retention.");
        }
        finally { try { Directory.Delete(root, true); } catch (IOException) { } }
    }
}
