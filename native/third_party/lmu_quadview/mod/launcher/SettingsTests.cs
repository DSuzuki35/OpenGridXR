using System;
using System.Globalization;
using System.IO;
using System.Threading;
using LmuQuadViewLauncher;
class SettingsTests
{
    static void Require(bool value, string name) { if (!value) throw new Exception(name); }
    static void Rejected(Action action) { bool rejected=false; try { action(); } catch(ArgumentException) { rejected=true; } catch(InvalidDataException) { rejected=true; } catch(FormatException) { rejected=true; } Require(rejected,"Invalid settings accepted"); }
    static void Main()
    {
        string folder=Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"settings-test-"+Guid.NewGuid().ToString("N")); Directory.CreateDirectory(folder);
        Require(typeof(LauncherSettings).GetProperty("DynamicFocusEnabled")!=null,"Dynamic focus opt-in missing");
        var s=new LauncherSettings { Backend="QVFR", OuterPercent=10,InnerPercent=200,FocusWidthPercent=33,FocusHeightPercent=25 };
        foreach(var name in new[]{"CropWidthPercent","CropHeightPercent"}) {
            var property=typeof(LauncherSettings).GetProperty(name);
            Require(property!=null,"Rendered FOV crop setting missing: "+name);
            Require((decimal)property.GetValue(new LauncherSettings(),null)==100,"Crop must default to full FOV");
        }
        Require(typeof(LauncherSettings).GetProperty("FocusOffsetXPercent")!=null,"Horizontal offset setting missing");
        Require(typeof(LauncherSettings).GetProperty("FocusOffsetYPercent")!=null,"Vertical offset setting missing");
        Require(!new LauncherSettings().BenchmarkEnabled,"Benchmark must default off");
        Require(new LauncherSettings().BenchmarkVisibilityButton==-1&&new LauncherSettings().BenchmarkRecordingButton==-1,"Default joystick bindings must be unassigned");
        s.BenchmarkEnabled=true;s.BenchmarkTemplate=1;s.BenchmarkX=-1.25m;s.BenchmarkY=0.5m;s.BenchmarkDistance=2.5m;s.BenchmarkWidth=0.8m;
        s.BenchmarkVisibilityDevice=Guid.NewGuid().ToString("D");s.BenchmarkVisibilityName="Wheel <left>";s.BenchmarkVisibilityButton=127;
        s.BenchmarkRecordingDevice=Guid.NewGuid().ToString("D");s.BenchmarkRecordingName="Wheel right";s.BenchmarkRecordingButton=0;
        string old="# custom\n[exe:DCS.exe]\nfocus_multiplier=1.7\n";
        Thread.CurrentThread.CurrentCulture=CultureInfo.GetCultureInfo("de-DE");
        string text=QvfrSettings.ProfileText(old,s);
        Require(text.StartsWith(old.TrimEnd('\n')),"Other app settings changed");
        Require(text.Contains("peripheral_multiplier=0.1\nfocus_multiplier=2\n"),"Linear resolution / culture conversion");
        Require(text.Contains("horizontal_focus_section=0.33\nvertical_focus_section=0.25\n"),"Independent size conversion");
        Require(QvfrSettings.ProfileText(text,s)==text,"Profile idempotency");
        string scoped=QvfrSettings.ProfileText(text+"sharpen_focus_view=0.1\n",s);
        Require(scoped.Contains("[exe:Le Mans Ultimate.exe]\nsharpen_focus_view=0.1"),"Unowned suffix key moved into another app section");
        Require(QvfrSettings.ProfileText(scoped,s)==scoped,"Scope preservation is not idempotent");
        string path=Path.Combine(folder,"settings.cfg"); File.WriteAllText(path,old);
        QvfrSettings.SaveProfile(path,s); string[] backups=Directory.GetFiles(folder,"*.lmu-backup-*");
        Require(backups.Length==1 && File.ReadAllText(backups[0])==old,"Backup preservation");
        QvfrSettings.SaveProfile(path,s); Require(Directory.GetFiles(folder,"*.lmu-backup-*").Length==1,"Unchanged profile rewritten");
        s.Backend="Native"; s.OuterPercent=100; QvfrSettings.SaveProfile(path,s); Require(File.ReadAllText(path)==text,"Native overwrote QVFR");
        s.Backend="QVFR"; Rejected(delegate{QvfrSettings.Validate(s);}); s.OuterPercent=10;
        Rejected(delegate{QvfrSettings.ProfileText("# BEGIN LMU QUAD VIEW MOD PROFILE",s);});
        Rejected(delegate{QvfrSettings.ProfileText("# END LMU QUAD VIEW MOD PROFILE\n# BEGIN LMU QUAD VIEW MOD PROFILE",s);});
        s.OuterPercent=9; Rejected(delegate{QvfrSettings.Validate(s);}); s.OuterPercent=10;
        s.FocusWidthPercent=100; Rejected(delegate{QvfrSettings.Validate(s);}); s.FocusWidthPercent=33;
        s.FocusHeightPercent=9; Rejected(delegate{QvfrSettings.Validate(s);}); s.FocusHeightPercent=25;
        s.OuterBlur=4; Rejected(delegate{QvfrSettings.Validate(s);}); s.OuterBlur=1.25m;
        s.FocusAspectRatio=2.19283709218806m;s.RenderProfiling=true;s.RenderProfileSeconds=300;s.DiagnosticsEnabled=false;s.StereoDrawAudit=true;s.StereoShaderCapture=true;s.StereoPairProof=true;s.CpuUploadReuse=true;
        s.FocusOffsetXPercent=-50;s.FocusOffsetYPercent=12.5m;
        s.CropWidthPercent=75.5m;s.CropHeightPercent=42.25m;
        Require(!new LauncherSettings().DynamicFocusEnabled,"Dynamic focus must default off");
        s.DynamicFocusEnabled=true;s.DynamicFocusMinPercent=65.5m;s.DynamicFocusMaxPercent=125;s.DynamicFocusReservePercent=2.5m;
        string xml=Path.Combine(folder,"launcher-settings.xml"); QvfrSettings.Save(xml,s); var loaded=QvfrSettings.Load(xml);
        Require(loaded.DynamicFocusEnabled&&loaded.DynamicFocusMinPercent==65.5m&&loaded.DynamicFocusMaxPercent==125&&loaded.DynamicFocusReservePercent==2.5m,"Dynamic focus roundtrip");
        s.DynamicFocusMinPercent=126;Rejected(delegate{QvfrSettings.Validate(s);});s.DynamicFocusMinPercent=65.5m;
        s.DynamicFocusReservePercent=0;Rejected(delegate{QvfrSettings.Validate(s);});s.DynamicFocusReservePercent=2.5m;
        Require(loaded.BenchmarkEnabled&&loaded.BenchmarkTemplate==1&&loaded.BenchmarkX==-1.25m&&loaded.BenchmarkY==0.5m&&loaded.BenchmarkDistance==2.5m&&loaded.BenchmarkWidth==0.8m,"Benchmark placement/template did not roundtrip");
        Require(loaded.BenchmarkVisibilityDevice==s.BenchmarkVisibilityDevice&&loaded.BenchmarkVisibilityName==s.BenchmarkVisibilityName&&loaded.BenchmarkVisibilityButton==127&&loaded.BenchmarkRecordingDevice==s.BenchmarkRecordingDevice&&loaded.BenchmarkRecordingButton==0,"Distinct joystick bindings did not roundtrip");
        s.BenchmarkVisibilityButton=128;Rejected(delegate {QvfrSettings.Validate(s);});s.BenchmarkVisibilityButton=127;
        s.BenchmarkTemplate=2;Rejected(delegate {QvfrSettings.Validate(s);});s.BenchmarkTemplate=1;
        s.BenchmarkDistance=0;Rejected(delegate {QvfrSettings.Validate(s);});s.BenchmarkDistance=2.5m;
        string separateDevice=s.BenchmarkRecordingDevice;int separateButton=s.BenchmarkRecordingButton;s.BenchmarkRecordingDevice=s.BenchmarkVisibilityDevice;s.BenchmarkRecordingButton=s.BenchmarkVisibilityButton;Rejected(delegate {QvfrSettings.Validate(s);});s.BenchmarkRecordingDevice=separateDevice;s.BenchmarkRecordingButton=separateButton;
        string device=s.BenchmarkRecordingDevice;s.BenchmarkRecordingDevice="invalid";Rejected(delegate {QvfrSettings.Validate(s);});s.BenchmarkRecordingDevice=device;
        Require(loaded.CropWidthPercent==75.5m&&loaded.CropHeightPercent==42.25m,"Independent crop values did not roundtrip invariantly");
        Require(File.ReadAllText(xml).Contains("<CropWidthPercent>75.5</CropWidthPercent>"),"Crop XML used local decimal separator");
        foreach(var invalid in new[]{24.9m,100.1m}) {
            s.CropWidthPercent=invalid;Rejected(delegate{QvfrSettings.Validate(s);});s.CropWidthPercent=75.5m;
            s.CropHeightPercent=invalid;Rejected(delegate{QvfrSettings.Validate(s);});s.CropHeightPercent=42.25m;
        }
        s.CropWidthPercent=25;s.CropHeightPercent=100;QvfrSettings.Validate(s);s.CropWidthPercent=75.5m;s.CropHeightPercent=42.25m;
        Require(QvfrSettings.ProfileText(old,s)==text,"Crop settings leaked into provider profile");
        Require(loaded.FocusOffsetXPercent==-50&&loaded.FocusOffsetYPercent==12.5m,"Offsets did not roundtrip invariantly");
        s.FocusOffsetXPercent=-50.1m;Rejected(delegate{QvfrSettings.Validate(s);});s.FocusOffsetXPercent=50;
        s.FocusOffsetYPercent=50.1m;Rejected(delegate{QvfrSettings.Validate(s);});s.FocusOffsetYPercent=-50;
        QvfrSettings.Validate(s);s.FocusOffsetXPercent=-50;s.FocusOffsetYPercent=12.5m;
        Require(QvfrSettings.ProfileText(old,s)==text,"Offsets leaked into provider profile");
        Require(loaded.RenderProfiling,"Profiling setting did not roundtrip");
        Require(!loaded.DiagnosticsEnabled&&new LauncherSettings().DiagnosticsEnabled,"Diagnostics off setting/default lost");
        Require(loaded.StereoDrawAudit&&!new LauncherSettings().StereoDrawAudit,"Stereo audit preference/default lost");
        Require(loaded.StereoShaderCapture&&!new LauncherSettings().StereoShaderCapture,"Shader capture preference/default lost");
        Require(loaded.StereoPairProof&&loaded.CpuUploadReuse&&!new LauncherSettings().StereoPairProof&&!new LauncherSettings().CpuUploadReuse&&!new LauncherSettings().CpuUploadBatch,"Prototype preferences/defaults lost");
        s.CpuUploadBatch=true;QvfrSettings.Save(xml,s);Require(QvfrSettings.Load(xml).CpuUploadBatch,"CPU upload fast-path preference lost");
        Require(loaded.RenderProfileSeconds==300&&new LauncherSettings().RenderProfileSeconds==120,"Capture length roundtrip/default");
        s.RenderProfileSeconds=29;Rejected(delegate{QvfrSettings.Validate(s);});s.RenderProfileSeconds=301;Rejected(delegate{QvfrSettings.Validate(s);});s.RenderProfileSeconds=300;
        Require(!new LauncherSettings().RenderProfiling,"Profiling enabled by default");
        Require(loaded.FocusWidthPercent==33 && loaded.FocusHeightPercent==25 && loaded.InnerPercent==200 && loaded.OuterBlur==1.25m,"Settings roundtrip");
        Require(loaded.FocusAspectRatio==s.FocusAspectRatio,"Measured FOV calibration lost on roundtrip");
        File.WriteAllText(xml,"<QuadView><GameDirectory>D:\\Game</GameDirectory><DebugFocus>true</DebugFocus></QuadView>");
        loaded=QvfrSettings.Load(xml); Require(loaded.Backend=="Native" && loaded.DebugFocus && loaded.OuterPercent==25,"Old settings migration");
        Require(loaded.DiagnosticsEnabled,"Existing settings must retain diagnostic logging");
        Require(!loaded.StereoDrawAudit,"Existing settings must not enable draw auditing");
        Require(!loaded.StereoShaderCapture,"Existing settings must not enable shader capture");
        Require(!loaded.StereoPairProof&&!loaded.CpuUploadReuse&&!loaded.CpuUploadBatch,"Existing settings must not enable prototype options");
        Require(loaded.FocusOffsetXPercent==0&&loaded.FocusOffsetYPercent==0,"Old settings must default offsets to zero");
        Require(loaded.CropWidthPercent==100&&loaded.CropHeightPercent==100,"Old settings must preserve full rendered FOV");
        File.WriteAllText(xml,"<QuadView><CropWidthPercent>75.5</CropWidthPercent></QuadView>");
        loaded=QvfrSettings.Load(xml);Require(loaded.CropWidthPercent==75.5m&&loaded.CropHeightPercent==100,"Partially migrated crop settings lost independent default");
        foreach(var invalid in new[]{"75,5","24.9","100.1","NaN",""}) {
            File.WriteAllText(xml,"<QuadView><CropHeightPercent>"+invalid+"</CropHeightPercent></QuadView>");
            Rejected(delegate{QvfrSettings.Load(xml);});
        }
        Require(loaded.FocusAspectRatio==0,"Old settings invent headset geometry");
        s.Backend="VectorXR";s.VectorXrManifest=@"E:\VectorXR\layer.json";s.QuadViewsManifest=@"E:\QVFR\layer.json";
        QvfrSettings.Save(xml,s);loaded=QvfrSettings.Load(xml);
        Require(loaded.Backend=="VectorXR"&&loaded.VectorXrManifest==s.VectorXrManifest&&loaded.QuadViewsManifest==s.QuadViewsManifest,"Separate provider settings roundtrip");
        string unchangedProfile=File.ReadAllText(path);QvfrSettings.SaveProfile(path,s);
        Require(File.ReadAllText(path)==unchangedProfile,"VectorXR overwrote the mbucchia profile");
        File.WriteAllText(xml,"<QuadView><Backend>VRS</Backend><VrsPeripheralRate>4</VrsPeripheralRate></QuadView>");
        loaded=QvfrSettings.Load(xml);QvfrSettings.Validate(loaded);
        Require(loaded.Backend=="VRS","VRS selection lost");QvfrSettings.Save(xml,loaded);
        Require(File.ReadAllText(xml).Contains("<VrsPeripheralRate>4</VrsPeripheralRate>"),"VRS rate must roundtrip");
        QvfrSettings.SaveProfile(path,loaded);Require(File.ReadAllText(path)==unchangedProfile,"VRS changed quad provider profile");
        foreach(var invalidRate in new[]{0,3,8}) {
            File.WriteAllText(xml,"<QuadView><Backend>VRS</Backend><VrsPeripheralRate>"+invalidRate+"</VrsPeripheralRate></QuadView>");
            Rejected(delegate{QvfrSettings.Validate(QvfrSettings.Load(xml));});
        }
        File.WriteAllText(xml,"<QuadView><CpuOuterReuse>true</CpuOuterReuse><OuterSsr>true</OuterSsr></QuadView>");
        loaded=QvfrSettings.Load(xml);QvfrSettings.Save(xml,loaded);
        Require(File.ReadAllText(xml).Contains("<CpuOuterReuse>true</CpuOuterReuse>")&&File.ReadAllText(xml).Contains("<OuterSsr>true</OuterSsr>"),"Performance experiment preferences must roundtrip");
        File.WriteAllText(xml,"<QuadView />");loaded=QvfrSettings.Load(xml);QvfrSettings.Save(xml,loaded);
        Require(File.ReadAllText(xml).Contains("<CpuOuterReuse>false</CpuOuterReuse>")&&File.ReadAllText(xml).Contains("<OuterSsr>false</OuterSsr>"),"Existing settings must not enable new performance experiments");
        Console.WriteLine("C# settings tests passed: independent dimensions, invariant culture, backups, preservation, validation, migration, VRS.");
    }
}
