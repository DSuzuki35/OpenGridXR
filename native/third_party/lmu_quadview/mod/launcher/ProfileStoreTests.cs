using System;
using System.IO;
using System.Linq;
using System.Text;
using LmuQuadViewLauncher;

class ProfileStoreTests
{
    static void Require(bool value, string name) { if (!value) throw new Exception(name); }
    static void Throws<T>(Action action, string name) where T : Exception
    {
        try { action(); } catch (T) { return; }
        throw new Exception(name);
    }
    static LauncherSettings Local()
    {
        return new LauncherSettings {
            GameDirectory = @"C:\Games\LMU", QuadViewsManifest = @"C:\QVFR\qvfr.json", VectorXrManifest = @"C:\Vec\v.json",
            Backend = "QVFR", OuterPercent = 30, InnerPercent = 120, FocusWidthPercent = 40, FocusHeightPercent = 35,
            FocusOffsetXPercent = -3.5m, FocusOffsetYPercent = 2, CropWidthPercent = 90, CropHeightPercent = 85.5m,
            DiagnosticsEnabled = false, RenderProfiling = true, BenchmarkVisibilityDevice = Guid.NewGuid().ToString("D"),
            BenchmarkVisibilityButton = 3, BenchmarkVisibilityName = "Wheel", FocusAspectRatio = 1.25m, CpuOuterReuse = true };
    }
    static string Write(string folder, string name, string text)
    {
        var path = Path.Combine(folder, name); File.WriteAllText(path, text, new UTF8Encoding(false)); return path;
    }
    static string Valid(string name, string backend = "QVFR", string outer = "25", string extra = "")
    {
        return "<LmuQuadViewProfile format=\"1\"><Name>" + name + "</Name><Modified>2026-11-04T10:00:00Z</Modified><Settings>" +
            "<Backend>" + backend + "</Backend><FoveationMode>auto</FoveationMode><VrsPeripheralRate>2</VrsPeripheralRate>" +
            "<DebugFocus>false</DebugFocus><NativeOuterResolution>true</NativeOuterResolution><CpuUploadReuse>false</CpuUploadReuse>" +
            "<CpuUploadBatch>false</CpuUploadBatch><CpuOuterReuse>false</CpuOuterReuse><OuterSsr>false</OuterSsr><OuterBlur>0</OuterBlur>" +
            "<OuterPercent>" + outer + "</OuterPercent><InnerPercent>100</InnerPercent><CropWidthPercent>100</CropWidthPercent>" +
            "<CropHeightPercent>100</CropHeightPercent><FocusWidthPercent>50</FocusWidthPercent><FocusHeightPercent>50</FocusHeightPercent>" +
            "<FocusOffsetXPercent>0</FocusOffsetXPercent><FocusOffsetYPercent>0</FocusOffsetYPercent>" + extra + "</Settings></LmuQuadViewProfile>";
    }
    static void Main()
    {
        string root = Path.Combine(Path.GetTempPath(), "lmu-profiles-" + Guid.NewGuid().ToString("N"));
        try
        {
            var store = new ProfileStore(Path.Combine(root, "profiles"));
            Require(store.List().Count == 0, "missing folder should list nothing");

            // save / list / load round trip
            var local = Local();
            store.Save(SettingsProfile.Capture("  Race   night ", local), false);
            Require(store.List().Single().Name == "Race night", "name not normalized");
            var loaded = store.Load("race NIGHT");
            Require(loaded.OuterPercent == 30 && loaded.InnerPercent == 120 && loaded.FocusOffsetXPercent == -3.5m && loaded.CropHeightPercent == 85.5m
                && loaded.CpuOuterReuse && loaded.Backend == "QVFR", "values changed in round trip");
            Throws<IOException>(() => store.Save(SettingsProfile.Capture("RACE NIGHT", local), false), "case-insensitive duplicate accepted");
            local.OuterPercent = 20; store.Save(SettingsProfile.Capture("Race night", local), true);
            Require(store.Load("Race night").OuterPercent == 20 && store.List().Count == 1, "overwrite failed or duplicated");

            // applying keeps everything machine-specific
            var other = new LauncherSettings { GameDirectory = @"E:\LMU", QuadViewsManifest = @"E:\q.json", DiagnosticsEnabled = true, FocusAspectRatio = 0.9m,
                BenchmarkRecordingDevice = Guid.NewGuid().ToString("D"), BenchmarkRecordingButton = 5, BenchmarkRecordingName = "Box", CpuCacheAffinity = false };
            var applied = store.Load("Race night").ApplyTo(other);
            Require(applied.GameDirectory == @"E:\LMU" && applied.QuadViewsManifest == @"E:\q.json" && applied.DiagnosticsEnabled
                && applied.FocusAspectRatio == 0.9m && applied.BenchmarkRecordingButton == 5 && !applied.RenderProfiling, "local settings overwritten by profile");
            Require(applied.OuterPercent == 20 && applied.Backend == "QVFR" && other.OuterPercent == 25, "profile not applied or source mutated");
            Require(!applied.CpuCacheAffinity, "profile changed machine CPU preference");

            // rename, case-only rename, conflicts, delete
            store.Save(SettingsProfile.Capture("VRS daily", new LauncherSettings { Backend = "VRS", VrsPeripheralRate = 4 }), false);
            Throws<IOException>(() => store.Rename("VRS daily", "race night"), "rename onto existing profile accepted");
            store.Rename("VRS daily", "VRS Daily");
            Require(store.List().Any(p => p.Name == "VRS Daily") && store.List().Count == 2, "case-only rename failed");
            store.Rename("VRS Daily", "Stereo VRS 4x4");
            Require(store.Load("Stereo VRS 4x4").VrsPeripheralRate == 4 && !store.Exists("VRS Daily"), "rename lost data");
            Require(Directory.GetFiles(store.Directory).Length == 2, "rename left an old file behind");
            store.Delete("Stereo VRS 4x4");
            Require(!store.Exists("Stereo VRS 4x4") && store.List().Count == 1, "delete failed");
            Throws<FileNotFoundException>(() => store.Delete("Stereo VRS 4x4"), "deleting a missing profile succeeded");

            // export -> import into another user's store
            string exported = Path.Combine(root, "share", "Race night.lmuqv");
            store.Export("Race night", exported);
            var exportedText = File.ReadAllText(exported);
            Require(!exportedText.Contains(@"C:\") && !exportedText.Contains("Wheel") && !exportedText.Contains("GameDirectory"), "export leaks local paths or devices");
            Require(!exportedText.Contains("CpuCacheAffinity"), "profile exported machine CPU preference");
            Require(!exportedText.Contains("OuterBlur"), "profile export retained removed smoothing");
            foreach(var oldBlur in new[]{"1.25","3","invalid"}) {
                string legacyPath=Write(root,"legacy-blur.lmuqv",Valid("Legacy").Replace("<OuterBlur>0</OuterBlur>","<OuterBlur>"+oldBlur+"</OuterBlur>"));
                var legacySettings=ProfileStore.ReadImport(legacyPath).ApplyTo(other);
                var child=LaunchService.MakeStartInfo(root,legacySettings,new LaunchService.LayerPlan(),Path.Combine(root,"unused.log"));
                Require(child.EnvironmentVariables["LMU_QV_OUTER_BLUR"]=="0","Legacy profile smoothing can still activate blur");
            }
            var friend = new ProfileStore(Path.Combine(root, "friend"));
            var incoming = ProfileStore.ReadImport(exported);
            friend.Save(incoming, false);
            Require(friend.Load("Race night").OuterPercent == 20, "import round trip failed");
            Require(friend.AvailableName("Race night") == "Race night (2)", "collision name");
            incoming.Name = friend.AvailableName(incoming.Name); friend.Save(incoming, false);
            Require(friend.AvailableName("race night") == "race night (3)" && friend.List().Count == 2, "second import copy");

            // hostile or broken shared files
            string bad = Path.Combine(root, "bad"); Directory.CreateDirectory(bad);
            string secret = Write(bad, "secret.txt", "TOPSECRET");
            Throws<InvalidDataException>(() => ProfileStore.ReadImport(Write(bad, "xxe.lmuqv",
                "<?xml version=\"1.0\"?><!DOCTYPE p [<!ENTITY x SYSTEM \"file:///" + secret.Replace('\\', '/') + "\">]>" + Valid("&x;"))), "DTD/external entity accepted");
            Throws<InvalidDataException>(() => ProfileStore.ReadImport(Write(bad, "big.lmuqv", Valid("Big", extra: "<Pad>" + new string('x', 70000) + "</Pad>"))), "oversized file accepted");
            Throws<InvalidDataException>(() => ProfileStore.ReadImport(Write(bad, "root.lmuqv", "<QuadView><GameDirectory>X</GameDirectory></QuadView>")), "settings file accepted as profile");
            Throws<InvalidDataException>(() => ProfileStore.ReadImport(Write(bad, "future.lmuqv", Valid("Future").Replace("format=\"1\"", "format=\"2\""))), "newer format accepted");
            Throws<InvalidDataException>(() => ProfileStore.ReadImport(Write(bad, "range.lmuqv", Valid("Range", outer: "9999"))), "out-of-range value accepted");
            Throws<InvalidDataException>(() => ProfileStore.ReadImport(Write(bad, "backend.lmuqv", Valid("Bad", backend: "Evil"))), "unknown backend accepted");
            Throws<InvalidDataException>(() => ProfileStore.ReadImport(Write(bad, "nan.lmuqv", Valid("NaN", outer: "1e3"))), "exponent number accepted");
            Throws<InvalidDataException>(() => ProfileStore.ReadImport(Write(bad, "missing.lmuqv", Valid("Missing").Replace("<OuterSsr>false</OuterSsr>", ""))), "missing value accepted");
            Throws<InvalidDataException>(() => ProfileStore.ReadImport(Write(bad, "garbage.lmuqv", "not xml")), "garbage accepted");
            Throws<InvalidDataException>(() => ProfileStore.ReadImport(Write(bad, "ctrl.lmuqv", Valid("A&#x7;B"))), "control character name accepted");
            var injected = ProfileStore.ReadImport(Write(bad, "extra.lmuqv", Valid("Extra", extra: "<GameDirectory>C:\\Evil</GameDirectory><DiagnosticsEnabled>true</DiagnosticsEnabled>")));
            var safe = injected.ApplyTo(other);
            Require(safe.GameDirectory == @"E:\LMU" && safe.OuterPercent == 25, "unknown elements were not ignored");

            Throws<InvalidDataException>(() => ProfileStore.ReadImport(Write(bad, "duplicate.lmuqv", Valid("Duplicate", extra: "<OuterPercent>9999</OuterPercent>"))), "ambiguous duplicate value accepted");
            Throws<InvalidDataException>(() => ProfileStore.ReadImport(Write(bad, "nested.lmuqv", Valid("Nested").Replace("<Backend>QVFR</Backend>", "<Backend><Value>QVFR</Value></Backend>"))), "nested scalar accepted");
            Throws<InvalidDataException>(() => ProfileStore.ReadImport(Write(bad, "duplicate-settings.lmuqv", Valid("Duplicate").Replace("</LmuQuadViewProfile>", "<Settings /></LmuQuadViewProfile>"))), "duplicate settings accepted");

            // A profile repairs rendering without replacing unsaved local values.
            var invalidRendering = Local(); invalidRendering.OuterPercent = 400; invalidRendering.GameDirectory = @"F:\Unsaved LMU";
            var repaired = incoming.ApplyTo(invalidRendering);
            foreach (var property in typeof(LauncherSettings).GetProperties().Where(p => typeof(SettingsProfile).GetProperty(p.Name) == null))
                Require(Object.Equals(property.GetValue(repaired, null), property.GetValue(invalidRendering, null)), "profile changed local field " + property.Name);

            // corrupt files in the profile folder do not break the list
            Write(store.Directory, "broken.lmuqv", "<broken");
            Require(store.List().Count == 1, "corrupt profile broke listing");

            // names and file names
            Throws<ArgumentException>(() => ProfileStore.CleanName("   "), "empty name");
            Throws<ArgumentException>(() => ProfileStore.CleanName(new string('a', ProfileStore.MaxNameLength + 1)), "long name");
            Require(ProfileStore.CleanName(new string('a', ProfileStore.MaxNameLength)).Length == ProfileStore.MaxNameLength, "max length name rejected");
            store.Save(SettingsProfile.Capture("CON", new LauncherSettings()), false);
            store.Save(SettingsProfile.Capture("a/b:c*?.d", new LauncherSettings()), false);
            store.Save(SettingsProfile.Capture("a_b_c", new LauncherSettings()), false);
            Require(store.Exists("con") && store.Exists("a/b:c*?.d") && store.Exists("a_b_c") && store.List().Count == 4, "special names not stored separately");
            Require(Directory.GetFiles(store.Directory, "*.lmuqv").All(f => !Path.GetFileNameWithoutExtension(f).Equals("CON", StringComparison.OrdinalIgnoreCase)), "reserved device name used as file name");
            Require(ProfileStore.CleanName("Ünïcødé  Prøfil") == "Ünïcødé Prøfil", "unicode name");

            Console.WriteLine("Profile store tests passed: save/load/overwrite, rename, delete, export/import, collisions, local-value isolation, hostile files, names.");
        }
        finally { try { Directory.Delete(root, true); } catch (IOException) { } }
    }
}
