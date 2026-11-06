using System;
using System.IO;
using System.Linq;
using System.Text;
using LmuQuadViewLauncher;

// 0.3.3 hardening: runtime cleanup, bounded QVFR backups and preserved profile encoding.
class HardeningTests
{
    static void Require(bool value, string name) { if (!value) throw new Exception(name); }
    static LauncherSettings Qvfr()
    {
        var s = new LauncherSettings(); s.Backend = "QVFR"; s.GameDirectory = Path.GetTempPath(); return s;
    }
    static void Main()
    {
        string root = Path.Combine(Path.GetTempPath(), "lmu-hardening-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            // --- runtime pruning
            string runtime = Path.Combine(root, "runtime"); Directory.CreateDirectory(runtime);
            DateTime now = DateTime.UtcNow;
            Func<string, DateTime, string> layer = (name, time) => {
                var d = Path.Combine(runtime, name); Directory.CreateDirectory(d);
                File.WriteAllText(Path.Combine(d, "XR_APILAYER_MBUCCHIA_quad_views_foveated.json"), "{}");
                Directory.SetLastWriteTimeUtc(d, time); return d;
            };
            var stale = layer("layers-20261101-101010-0123abcd", now.AddDays(-3));
            var fresh = layer("layers-20261104-101010-0123abcd", now.AddHours(-2));
            var foreign = layer("layers-mine", now.AddDays(-3));
            var unexpected = layer("layers-20261101-101011-0123abcd", now.AddDays(-3));
            File.WriteAllText(Path.Combine(unexpected, "notes.txt"), "user file");
            Directory.SetLastWriteTimeUtc(unexpected, now.AddDays(-3));
            string oldIni = Path.Combine(runtime, "autoload-" + new string('a', 32) + ".ini"); File.WriteAllText(oldIni, "x"); File.SetLastWriteTimeUtc(oldIni, now.AddDays(-2));
            string newIni = Path.Combine(runtime, "autoload-" + new string('b', 32) + ".ini"); File.WriteAllText(newIni, "x");
            string otherIni = Path.Combine(runtime, "autoload-user.ini"); File.WriteAllText(otherIni, "x"); File.SetLastWriteTimeUtc(otherIni, now.AddDays(-2));
            var foreignJson = layer("layers-20261101-101012-0123abcd", now.AddDays(-3));
            File.WriteAllText(Path.Combine(foreignJson,"notes.json"),"user data");
            Directory.SetLastWriteTimeUtc(foreignJson,now.AddDays(-3));
            var filesBefore=Directory.GetFiles(runtime,"*",SearchOption.AllDirectories).ToDictionary(path=>path,File.ReadAllText);
            var directoriesBefore=Directory.GetDirectories(runtime,"*",SearchOption.AllDirectories);
            int presenceChecks=0;
            LaunchService.PruneRuntime(runtime,now,()=>{++presenceChecks;return true;});
            Require(presenceChecks==1,"running-game guard was not checked");
            Require(Directory.GetFiles(runtime,"*",SearchOption.AllDirectories).Length==filesBefore.Count&&
                filesBefore.All(file=>File.Exists(file.Key)&&File.ReadAllText(file.Key)==file.Value)&&
                directoriesBefore.All(Directory.Exists),"running game did not preserve all runtime files and folders");
            LaunchService.PruneRuntime(runtime,now,()=>false);
            Require(File.Exists(Path.Combine(foreignJson,"notes.json")),"foreign JSON removed");
            Require(!Directory.Exists(stale), "stale generated layer folder kept");
            Require(Directory.Exists(fresh), "recent layer folder removed (may belong to a running game)");
            Require(Directory.Exists(foreign), "non-generated folder name removed");
            Require(Directory.Exists(unexpected) && File.Exists(Path.Combine(unexpected, "notes.txt")), "folder with a non-manifest file removed");
            Require(!File.Exists(oldIni) && File.Exists(newIni) && File.Exists(otherIni), "autoload INI pruning wrong");
            LaunchService.PruneRuntime(Path.Combine(root, "missing"), now,()=>false); // must not throw
            var outside=Path.Combine(root,"outside");Directory.CreateDirectory(outside);
            var linkedRuntime=Path.Combine(root,"linked-runtime");
            var linkedIni=Path.Combine(outside,"autoload-"+new string('c',32)+".ini");File.WriteAllText(linkedIni,"keep");File.SetLastWriteTimeUtc(linkedIni,now.AddDays(-2));
            var linkStart=new System.Diagnostics.ProcessStartInfo("cmd.exe","/c mklink /J \""+linkedRuntime+"\" \""+outside+"\"") {UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true};
            using(var link=System.Diagnostics.Process.Start(linkStart)){link.WaitForExit();Require(link.ExitCode==0,"junction fixture creation failed");}
            try{LaunchService.PruneRuntime(linkedRuntime,now,()=>false);Require(File.Exists(linkedIni),"linked runtime target modified");}
            finally{Directory.Delete(linkedRuntime,false);}

            // --- QVFR backups and encoding
            string profile = Path.Combine(root, "qvfr", "settings.cfg"); Directory.CreateDirectory(Path.GetDirectoryName(profile));
            var utf16 = new UnicodeEncoding(false, true);
            File.WriteAllText(profile, "# user comment äöü\n[exe:Other.exe]\nturbo_mode=1\n", utf16);
            var s = Qvfr();
            for (int i = 0; i < 6; i++)
            {
                s.FocusWidthPercent = 30 + i; // force a real change every time
                QvfrSettings.SaveProfile(profile, s);
            }
            var backups = Directory.GetFiles(Path.GetDirectoryName(profile), "settings.cfg.lmu-backup-*");
            Require(backups.Length == QvfrSettings.KeptBackups, "expected " + QvfrSettings.KeptBackups + " backups, found " + backups.Length);
            var bytes = File.ReadAllBytes(profile);
            Require(bytes.Length > 2 && bytes[0] == 0xFF && bytes[1] == 0xFE, "UTF-16 BOM not preserved");
            var text = File.ReadAllText(profile, Encoding.Unicode);
            Require(text.Contains("äöü") && text.Contains("[exe:Le Mans Ultimate.exe]") && text.Contains("turbo_mode=1"), "profile content not preserved");

            string plain = Path.Combine(root, "qvfr", "plain.cfg");
            File.WriteAllText(plain, "# plain\n", new UTF8Encoding(false));
            QvfrSettings.SaveProfile(plain, s);
            var plainBytes = File.ReadAllBytes(plain);
            Require(!(plainBytes[0] == 0xEF && plainBytes[1] == 0xBB), "BOM added to a BOM-less UTF-8 profile");
            string bom = Path.Combine(root, "qvfr", "bom.cfg");
            File.WriteAllText(bom, "# bom\n", new UTF8Encoding(true));
            QvfrSettings.SaveProfile(bom, s);
            var bomBytes = File.ReadAllBytes(bom);
            Require(bomBytes[0] == 0xEF && bomBytes[1] == 0xBB && bomBytes[2] == 0xBF, "UTF-8 BOM lost");
            string fresh2 = Path.Combine(root, "qvfr2", "settings.cfg");
            QvfrSettings.SaveProfile(fresh2, s);
            Require(File.Exists(fresh2) && File.ReadAllBytes(fresh2)[0] != 0xEF, "new profile should be UTF-8 without BOM");
            int index=0;
            foreach(var enc in new Encoding[]{new UnicodeEncoding(true,true,true),new UTF32Encoding(false,true,true),new UTF32Encoding(true,true,true)}) {
                var name=Path.Combine(root,"encoded-"+(index++)+".cfg");
                File.WriteAllText(name,"# Grüße \U0001F60A\n",enc);QvfrSettings.SaveProfile(name,s);
                Require(File.ReadAllBytes(name).Take(enc.GetPreamble().Length).SequenceEqual(enc.GetPreamble()),"BOM changed");
                Require(File.ReadAllText(name,enc).Contains("Grüße \U0001F60A"),"encoded content changed");
            }
            foreach(var invalid in new[]{new byte[]{0x23,0xe4,0x0a},new byte[]{0xff,0xfe,0,0xd8},new byte[]{0xff,0xfe,0,0,0,0x11,0x11,0},new byte[]{0x23,0,0x61,0}}) {
                var name=Path.Combine(root,"invalid.cfg");File.WriteAllBytes(name,invalid);
                bool refused=false;try{QvfrSettings.SaveProfile(name,s);}catch(InvalidDataException){refused=true;}
                Require(refused&&File.ReadAllBytes(name).SequenceEqual(invalid),"invalid encoding rewritten");
            }
            var backupBase=Path.Combine(root,"retention.cfg");
            for(int i=0;i<6;i++){
                var name=backupBase+".lmu-backup-"+i.ToString("x32");File.WriteAllText(name,i.ToString());File.SetLastWriteTimeUtc(name,now.AddMinutes(i));
            }
            var personal=backupBase+".lmu-backup-personal";File.WriteAllText(personal,"keep");
            QvfrSettings.PruneBackups(backupBase,3);
            Require(File.Exists(personal),"personal backup deleted");
            Require(Directory.GetFiles(root,"retention.cfg.lmu-backup-*").Where(p=>p!=personal).Select(File.ReadAllText).OrderBy(t=>t).SequenceEqual(new[]{"3","4","5"}),"newest backups not retained");
            // 0.5.0 (from 0.3.6): implausible benchmark timings become missing values
            string ring = Path.Combine(root, "ring.bin");
            using (var fs = new FileStream(ring, FileMode.Create)) fs.SetLength(BenchmarkAbi.Size);
            using (var file = System.IO.MemoryMappedFiles.MemoryMappedFile.CreateFromFile(ring, FileMode.Open, null, 0, System.IO.MemoryMappedFiles.MemoryMappedFileAccess.ReadWrite))
            using (var view = file.CreateViewAccessor(0, 0))
            {
                long offset = BenchmarkAbi.HeaderSize;
                view.Write(offset, 1L); view.Write(offset + 24, -5.0); view.Write(offset + 32, 1e9); view.Write(offset + 40, 1.5); view.Write(offset + 48, Double.PositiveInfinity); view.Write(offset + 64, 9u);
                BenchmarkSample sample;
                Require(BenchmarkReader.TryRead(view, 1, out sample), "valid slot not read");
                Require(Double.IsNaN(sample.IntervalMs) && Double.IsNaN(sample.AppWallMs) && sample.ProviderWallMs == 1.5 && Double.IsNaN(sample.GpuMs), "implausible timing accepted");
                view.Write(offset + 24, 11.25); view.Write(offset + 48, 6.5);
                Require(BenchmarkReader.TryRead(view, 1, out sample) && sample.IntervalMs == 11.25 && sample.GpuMs == 6.5, "plausible timing rejected");
            }
            Console.WriteLine("Hardening tests passed: runtime pruning, bounded QVFR backups, encoding/BOM preservation, benchmark plausibility.");
        }
        finally { try { Directory.Delete(root, true); } catch (IOException) { } }
    }
}
