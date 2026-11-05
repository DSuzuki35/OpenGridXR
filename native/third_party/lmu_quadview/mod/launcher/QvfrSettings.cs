using System;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using System.Xml;
using System.Xml.Linq;

namespace LmuQuadViewLauncher
{
    public sealed class LauncherSettings
    {
        public string GameDirectory { get; set; }
        public string Backend { get; set; }
        public int VrsPeripheralRate { get; set; }
        public string FoveationMode { get; set; }
        public string QuadViewsManifest { get; set; }
        public string VectorXrManifest { get; set; }
        public bool DebugFocus { get; set; }
        public bool RenderProfiling { get; set; }
        public bool DiagnosticsEnabled { get; set; }
        public bool BenchmarkEnabled { get; set; }
        public int BenchmarkTemplate { get; set; }
        public decimal BenchmarkX { get; set; }
        public decimal BenchmarkY { get; set; }
        public decimal BenchmarkDistance { get; set; }
        public decimal BenchmarkWidth { get; set; }
        public string BenchmarkVisibilityDevice { get; set; }
        public string BenchmarkVisibilityName { get; set; }
        public int BenchmarkVisibilityButton { get; set; }
        public string BenchmarkRecordingDevice { get; set; }
        public string BenchmarkRecordingName { get; set; }
        public int BenchmarkRecordingButton { get; set; }
        public bool StereoDrawAudit { get; set; }
        public bool StereoShaderCapture { get; set; }
        public bool StereoPairProof { get; set; }
        public bool CpuUploadReuse { get; set; }
        public bool CpuUploadBatch { get; set; }
        public bool CpuOuterReuse { get; set; }
        public bool OuterSsr { get; set; }
        public int RenderProfileSeconds { get; set; }
        public bool NativeOuterResolution { get; set; }
        public bool DynamicFocusEnabled { get; set; }
        public decimal DynamicFocusMinPercent { get; set; }
        public decimal DynamicFocusMaxPercent { get; set; }
        public decimal DynamicFocusReservePercent { get; set; }
        public decimal OuterBlur { get; set; }
        public decimal OuterPercent { get; set; }
        public decimal InnerPercent { get; set; }
        public decimal CropWidthPercent { get; set; }
        public decimal CropHeightPercent { get; set; }
        public decimal FocusWidthPercent { get; set; }
        public decimal FocusHeightPercent { get; set; }
        public decimal FocusOffsetXPercent { get; set; }
        public decimal FocusOffsetYPercent { get; set; }
        // Provider percentages retain their old meaning; this is UI calibration only.
        public decimal FocusAspectRatio { get; set; }
        public LauncherSettings()
        {
            GameDirectory = @"D:\SteamLibrary\steamapps\common\Le Mans Ultimate";
            Backend = "Native"; FoveationMode = "auto"; QuadViewsManifest = ""; VectorXrManifest = "";
            VrsPeripheralRate = 2;
            NativeOuterResolution = true; OuterPercent = 25; InnerPercent = 100;
            RenderProfileSeconds=120;
            DiagnosticsEnabled=true;
            BenchmarkY=-0.25m;BenchmarkDistance=1.2m;BenchmarkWidth=0.65m;
            BenchmarkVisibilityDevice=BenchmarkRecordingDevice="";
            BenchmarkVisibilityName=BenchmarkRecordingName="";
            BenchmarkVisibilityButton=BenchmarkRecordingButton=-1;
            FocusWidthPercent = FocusHeightPercent = 50;
            CropWidthPercent = CropHeightPercent = 100;
            DynamicFocusMinPercent=70;DynamicFocusMaxPercent=100;DynamicFocusReservePercent=2;
        }
    }
    public static class QvfrSettings
    {
        const string Begin = "# BEGIN LMU QUAD VIEW MOD PROFILE";
        const string End = "# END LMU QUAD VIEW MOD PROFILE";
        public static void Validate(LauncherSettings s)
        {
            if (s == null) throw new ArgumentNullException("settings");
            if(s.DynamicFocusMinPercent<25||s.DynamicFocusMaxPercent>200||s.DynamicFocusMinPercent>s.DynamicFocusMaxPercent)
                throw new ArgumentException("Dynamic focus requires 25 <= minimum <= maximum <= 200 percent of baseline FOV.");
            if(s.DynamicFocusReservePercent<1||s.DynamicFocusReservePercent>30)
                throw new ArgumentException("Dynamic focus GPU reserve must be between 1 and 30 percent.");
            if(s.BenchmarkTemplate<0||s.BenchmarkTemplate>1)throw new ArgumentException("Select Minimal or Detailed benchmark template.");
            if(s.BenchmarkX < -2 || s.BenchmarkX > 2 || s.BenchmarkY < -2 || s.BenchmarkY > 2 || s.BenchmarkDistance < 0.3m || s.BenchmarkDistance > 5 || s.BenchmarkWidth < 0.2m || s.BenchmarkWidth > 2)throw new ArgumentException("Benchmark placement is outside the supported range.");
            ValidateBinding(s.BenchmarkVisibilityDevice,s.BenchmarkVisibilityButton);
            ValidateBinding(s.BenchmarkRecordingDevice,s.BenchmarkRecordingButton);
            if(!String.IsNullOrEmpty(s.BenchmarkVisibilityDevice)&&!String.IsNullOrEmpty(s.BenchmarkRecordingDevice)&&new Guid(s.BenchmarkVisibilityDevice)==new Guid(s.BenchmarkRecordingDevice)&&s.BenchmarkVisibilityButton==s.BenchmarkRecordingButton)throw new ArgumentException("Choose different joystick buttons for visibility and recording.");
            if(s.CropWidthPercent<25||s.CropWidthPercent>100||s.CropHeightPercent<25||s.CropHeightPercent>100)
                throw new ArgumentException("Rendered FOV width and height must be between 25 and 100 percent.");
            if(s.FocusOffsetXPercent < -50 || s.FocusOffsetXPercent > 50 || s.FocusOffsetYPercent < -50 || s.FocusOffsetYPercent > 50) throw new ArgumentException("Focus offsets must be between -50 and +50 percent.");
            if(s.RenderProfileSeconds<30||s.RenderProfileSeconds>300)throw new ArgumentException("Capture length must be between 30 and 300 seconds.");
            if(s.FocusAspectRatio!=0&&(s.FocusAspectRatio<0.1m||s.FocusAspectRatio>10m)) throw new ArgumentException("Invalid measured focus aspect ratio.");
            if (String.IsNullOrWhiteSpace(s.GameDirectory)) throw new ArgumentException("Select the LMU game folder.");
            if (s.Backend != "Native" && s.Backend != "QVFR" && s.Backend != "VectorXR" && s.Backend != "VRS") throw new ArgumentException("Choose Native, QVFR, VectorXR or Stereo VRS.");
            if(s.VrsPeripheralRate!=1&&s.VrsPeripheralRate!=2&&s.VrsPeripheralRate!=4)throw new ArgumentException("VRS peripheral shading blocks must be 1x1, 2x2 or 4x4.");
            if (s.FoveationMode != "auto" && s.FoveationMode != "fixed") throw new ArgumentException("Choose Auto or Fixed foveation.");
            if (s.OuterBlur < 0 || s.OuterBlur > 3) throw new ArgumentException("Outer smoothing must be between 0 and 3 pixels.");
            if (s.OuterPercent < 10 || s.OuterPercent > 400 || s.InnerPercent < 25 || s.InnerPercent > 400 ||
                s.FocusWidthPercent < 10 || s.FocusWidthPercent > 90 || s.FocusHeightPercent < 10 || s.FocusHeightPercent > 90)
                throw new ArgumentException("Resolution or focus size is outside the supported range.");
            if (s.Backend == "QVFR" && s.OuterPercent > s.InnerPercent * Math.Min(s.FocusWidthPercent, s.FocusHeightPercent) / 100m)
                throw new ArgumentException("The outer image must fit within the focus buffer. Reduce outer resolution, or increase inner resolution / focus width and height.");
        }
        static void ValidateBinding(string device,int button) {
            Guid id;
            if(String.IsNullOrEmpty(device)) { if(button != -1)throw new ArgumentException("An unassigned joystick binding must have button -1."); }
            else if(!Guid.TryParse(device,out id)||id==Guid.Empty||button<0||button>127)throw new ArgumentException("Joystick binding needs an instance GUID and button 0-127.");
        }
        static string Value(XElement root, string name, string fallback) { return (string)root.Element(name) ?? fallback; }
        public static LauncherSettings Load(string path)
        {
            var s = new LauncherSettings();
            if (!File.Exists(path)) return s;
            var options = new XmlReaderSettings { DtdProcessing = DtdProcessing.Prohibit, XmlResolver = null };
            XElement x;
            using (var reader = XmlReader.Create(path, options)) x = XElement.Load(reader);
            if (x.Name != "QuadView") throw new InvalidDataException("Unrecognized launcher settings.");
            s.GameDirectory = Value(x, "GameDirectory", s.GameDirectory);
            s.Backend = Value(x, "Backend", s.Backend); s.FoveationMode = Value(x, "FoveationMode", s.FoveationMode);
            s.VrsPeripheralRate = Int32.Parse(Value(x,"VrsPeripheralRate","2"),CultureInfo.InvariantCulture);
            s.QuadViewsManifest = Value(x, "QuadViewsManifest", "");
            s.VectorXrManifest = Value(x, "VectorXrManifest", "");
            s.DebugFocus = Boolean.Parse(Value(x, "DebugFocus", "false"));
            s.RenderProfiling = Boolean.Parse(Value(x, "RenderProfiling", "false"));
            s.DiagnosticsEnabled=Boolean.Parse(Value(x,"DiagnosticsEnabled","true"));
            s.BenchmarkEnabled=Boolean.Parse(Value(x,"BenchmarkEnabled","false"));
            s.BenchmarkTemplate=Int32.Parse(Value(x,"BenchmarkTemplate","0"),CultureInfo.InvariantCulture);
            s.BenchmarkX=Decimal.Parse(Value(x,"BenchmarkX","0"),CultureInfo.InvariantCulture);
            s.BenchmarkY=Decimal.Parse(Value(x,"BenchmarkY","-0.25"),CultureInfo.InvariantCulture);
            s.BenchmarkDistance=Decimal.Parse(Value(x,"BenchmarkDistance","1.2"),CultureInfo.InvariantCulture);
            s.BenchmarkWidth=Decimal.Parse(Value(x,"BenchmarkWidth","0.65"),CultureInfo.InvariantCulture);
            s.BenchmarkVisibilityDevice=Value(x,"BenchmarkVisibilityDevice","");
            s.BenchmarkVisibilityName=Value(x,"BenchmarkVisibilityName","");
            s.BenchmarkVisibilityButton=Int32.Parse(Value(x,"BenchmarkVisibilityButton","-1"),CultureInfo.InvariantCulture);
            s.BenchmarkRecordingDevice=Value(x,"BenchmarkRecordingDevice","");
            s.BenchmarkRecordingName=Value(x,"BenchmarkRecordingName","");
            s.BenchmarkRecordingButton=Int32.Parse(Value(x,"BenchmarkRecordingButton","-1"),CultureInfo.InvariantCulture);
            s.StereoDrawAudit=Boolean.Parse(Value(x,"StereoDrawAudit","false"));
            s.StereoShaderCapture=Boolean.Parse(Value(x,"StereoShaderCapture","false"));
            s.StereoPairProof=Boolean.Parse(Value(x,"StereoPairProof","false"));
            s.CpuUploadReuse=Boolean.Parse(Value(x,"CpuUploadReuse","false"));
            s.CpuUploadBatch=Boolean.Parse(Value(x,"CpuUploadBatch","false"));
            s.CpuOuterReuse=Boolean.Parse(Value(x,"CpuOuterReuse","false"));
            s.OuterSsr=Boolean.Parse(Value(x,"OuterSsr","false"));
            s.RenderProfileSeconds=Int32.Parse(Value(x,"RenderProfileSeconds","120"),CultureInfo.InvariantCulture);
            s.NativeOuterResolution = Boolean.Parse(Value(x, "NativeOuterResolution", "true"));
            s.DynamicFocusEnabled=Boolean.Parse(Value(x,"DynamicFocusEnabled","false"));
            s.DynamicFocusMinPercent=Decimal.Parse(Value(x,"DynamicFocusMinPercent","70"),CultureInfo.InvariantCulture);
            s.DynamicFocusMaxPercent=Decimal.Parse(Value(x,"DynamicFocusMaxPercent","100"),CultureInfo.InvariantCulture);
            s.DynamicFocusReservePercent=Decimal.Parse(Value(x,"DynamicFocusReservePercent","2"),CultureInfo.InvariantCulture);
            s.OuterBlur = Decimal.Parse(Value(x, "OuterBlur", "0"), CultureInfo.InvariantCulture);
            s.OuterPercent = Decimal.Parse(Value(x, "OuterPercent", "25"), CultureInfo.InvariantCulture);
            s.InnerPercent = Decimal.Parse(Value(x, "InnerPercent", "100"), CultureInfo.InvariantCulture);
            s.CropWidthPercent = Decimal.Parse(Value(x, "CropWidthPercent", "100"), NumberStyles.AllowDecimalPoint, CultureInfo.InvariantCulture);
            s.CropHeightPercent = Decimal.Parse(Value(x, "CropHeightPercent", "100"), NumberStyles.AllowDecimalPoint, CultureInfo.InvariantCulture);
            s.FocusWidthPercent = Decimal.Parse(Value(x, "FocusWidthPercent", "50"), CultureInfo.InvariantCulture);
            s.FocusHeightPercent = Decimal.Parse(Value(x, "FocusHeightPercent", "50"), CultureInfo.InvariantCulture);
            s.FocusOffsetXPercent = Decimal.Parse(Value(x, "FocusOffsetXPercent", "0"), CultureInfo.InvariantCulture);
            s.FocusOffsetYPercent = Decimal.Parse(Value(x, "FocusOffsetYPercent", "0"), CultureInfo.InvariantCulture);
            s.FocusAspectRatio = Decimal.Parse(Value(x, "FocusAspectRatio", "0"), CultureInfo.InvariantCulture);
            Validate(s); return s;
        }
        public static void Save(string path, LauncherSettings s)
        {
            Validate(s);
            var xml = new XElement("QuadView", new XElement("GameDirectory", s.GameDirectory),
                new XElement("Backend", s.Backend), new XElement("FoveationMode", s.FoveationMode),
                new XElement("VrsPeripheralRate",s.VrsPeripheralRate),
                new XElement("QuadViewsManifest", s.QuadViewsManifest ?? ""), new XElement("VectorXrManifest", s.VectorXrManifest ?? ""), new XElement("DebugFocus", s.DebugFocus),
                new XElement("RenderProfiling", s.RenderProfiling),
                new XElement("DiagnosticsEnabled",s.DiagnosticsEnabled),
                new XElement("BenchmarkEnabled",s.BenchmarkEnabled),
                new XElement("BenchmarkTemplate",s.BenchmarkTemplate),
                new XElement("BenchmarkX",s.BenchmarkX),
                new XElement("BenchmarkY",s.BenchmarkY),
                new XElement("BenchmarkDistance",s.BenchmarkDistance),
                new XElement("BenchmarkWidth",s.BenchmarkWidth),
                new XElement("BenchmarkVisibilityDevice",s.BenchmarkVisibilityDevice),
                new XElement("BenchmarkVisibilityName",s.BenchmarkVisibilityName),
                new XElement("BenchmarkVisibilityButton",s.BenchmarkVisibilityButton),
                new XElement("BenchmarkRecordingDevice",s.BenchmarkRecordingDevice),
                new XElement("BenchmarkRecordingName",s.BenchmarkRecordingName),
                new XElement("BenchmarkRecordingButton",s.BenchmarkRecordingButton),
                new XElement("StereoDrawAudit",s.StereoDrawAudit),
                new XElement("StereoShaderCapture",s.StereoShaderCapture),
                new XElement("StereoPairProof",s.StereoPairProof),
                new XElement("CpuUploadReuse",s.CpuUploadReuse),
                new XElement("CpuUploadBatch",s.CpuUploadBatch),
                new XElement("CpuOuterReuse",s.CpuOuterReuse),
                new XElement("OuterSsr",s.OuterSsr),
                new XElement("RenderProfileSeconds",s.RenderProfileSeconds),
                new XElement("NativeOuterResolution", s.NativeOuterResolution), new XElement("UseRuntimeSettings", true),
                new XElement("DynamicFocusEnabled",s.DynamicFocusEnabled),
                new XElement("DynamicFocusMinPercent",s.DynamicFocusMinPercent),new XElement("DynamicFocusMaxPercent",s.DynamicFocusMaxPercent),
                new XElement("DynamicFocusReservePercent",s.DynamicFocusReservePercent),
                new XElement("OuterBlur", s.OuterBlur),
                new XElement("OuterPercent", s.OuterPercent), new XElement("InnerPercent", s.InnerPercent),
                new XElement("CropWidthPercent", s.CropWidthPercent), new XElement("CropHeightPercent", s.CropHeightPercent),
                new XElement("FocusWidthPercent", s.FocusWidthPercent), new XElement("FocusHeightPercent", s.FocusHeightPercent),
                new XElement("FocusOffsetXPercent", s.FocusOffsetXPercent), new XElement("FocusOffsetYPercent", s.FocusOffsetYPercent),
                new XElement("FocusAspectRatio", s.FocusAspectRatio));
            WriteAtomic(path, xml.ToString(), false);
        }
        public static string ProfileText(string existing, LauncherSettings s)
        {
            Validate(s); existing = existing ?? "";
            int starts = Regex.Matches(existing, Regex.Escape(Begin)).Count;
            int ends = Regex.Matches(existing, Regex.Escape(End)).Count;
            if (starts != ends || starts > 1) throw new InvalidDataException("Malformed LMU profile markers. QVFR settings were not changed.");
            string clean = existing;
            if (starts == 1)
            {
                string pattern = "(?ms)^" + Regex.Escape(Begin) + "\\r?$.*?^" + Regex.Escape(End) + "\\r?$(?:\\n)?";
                var match = Regex.Match(existing, pattern);
                bool suffixNeedsSection = false;
                if (match.Success)
                {
                    foreach (string line in existing.Substring(match.Index + match.Length).Split('\n'))
                    {
                        string trimmed = line.Trim();
                        if (trimmed.StartsWith("[", StringComparison.Ordinal)) break;
                        if (trimmed.Length == 0 || trimmed.StartsWith("#", StringComparison.Ordinal) || trimmed.StartsWith(";", StringComparison.Ordinal)) continue;
                        suffixNeedsSection = true; break;
                    }
                }
                // The removed block selected the LMU section. Keep that scope for
                // user-added keys following it, before moving our overrides last.
                clean = Regex.Replace(existing, pattern, suffixNeedsSection ? "[exe:Le Mans Ultimate.exe]\n" : "");
                if (clean == existing) throw new InvalidDataException("Malformed LMU profile block. QVFR settings were not changed.");
            }
            Func<decimal, string> fraction = delegate(decimal n) { return (n / 100m).ToString("0.####", CultureInfo.InvariantCulture); };
            return clean.TrimEnd('\r', '\n') + "\n\n" + Begin + "\n[exe:Le Mans Ultimate.exe]\n" +
                "prefer_foveated_rendering=0\nforce_no_eye_tracking=0\n" +
                "peripheral_multiplier=" + fraction(s.OuterPercent) + "\nfocus_multiplier=" + fraction(s.InnerPercent) +
                "\nhorizontal_focus_section=" + fraction(s.FocusWidthPercent) + "\nvertical_focus_section=" + fraction(s.FocusHeightPercent) +
                "\nhorizontal_fixed_section=" + fraction(s.FocusWidthPercent) + "\nvertical_fixed_section=" + fraction(s.FocusHeightPercent) +
                "\nturbo_mode=0\n" + End + "\n";
        }
        public static void SaveProfile(LauncherSettings s)
        {
            SaveProfile(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Quad-Views-Foveated", "settings.cfg"), s);
        }
        public static void SaveProfile(string path, LauncherSettings s)
        {
            Validate(s); if (s.Backend != "QVFR") return;
            string old = File.Exists(path) ? File.ReadAllText(path) : "";
            string updated = ProfileText(old, s);
            if (updated != old) WriteAtomic(path, updated, true);
        }
        static void WriteAtomic(string path, string text, bool backup)
        {
            path = Path.GetFullPath(path); Directory.CreateDirectory(Path.GetDirectoryName(path));
            string temp = path + ".tmp-" + Guid.NewGuid().ToString("N");
            try
            {
                File.WriteAllText(temp, text, new UTF8Encoding(false));
                if (File.Exists(path)) File.Replace(temp, path, backup ? path + ".lmu-backup-" + Guid.NewGuid().ToString("N") : null);
                else File.Move(temp, path);
            }
            finally { if (File.Exists(temp)) File.Delete(temp); }
        }
    }
}
