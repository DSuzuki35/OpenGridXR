using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Xml;
using System.Xml.Linq;

namespace LmuQuadViewLauncher {
    // Portable setting profiles contributed by Markus Stahl, integrated for 0.4.0.
    // Profiles can be saved, renamed, deleted, exported
    // and imported from other users. A profile only carries portable rendering
    // choices. Machine- or person-specific values (game folder, provider manifest
    // paths, joystick device IDs, diagnostics, headset FOV calibration) always stay
    // with the local settings, so an imported file can never redirect paths or
    // enable debug tooling.
    public sealed class SettingsProfile {
        public string Name { get; set; }
        public DateTime ModifiedUtc { get; set; }
        public string Backend { get; set; }
        public string FoveationMode { get; set; }
        public int VrsPeripheralRate { get; set; }
        public bool DebugFocus { get; set; }
        public bool NativeOuterResolution { get; set; }
        public bool CpuUploadReuse { get; set; }
        public bool CpuUploadBatch { get; set; }
        public bool CpuOuterReuse { get; set; }
        public bool OuterSsr { get; set; }
        public decimal OuterPercent { get; set; }
        public decimal InnerPercent { get; set; }
        public decimal CropWidthPercent { get; set; }
        public decimal CropHeightPercent { get; set; }
        public decimal FocusWidthPercent { get; set; }
        public decimal FocusHeightPercent { get; set; }
        public decimal FocusOffsetXPercent { get; set; }
        public decimal FocusOffsetYPercent { get; set; }

        public static SettingsProfile Capture(string name, LauncherSettings s) {
            if (s == null) throw new ArgumentNullException("s");
            return new SettingsProfile {
                Name = ProfileStore.CleanName(name), ModifiedUtc = DateTime.UtcNow,
                Backend = s.Backend, FoveationMode = s.FoveationMode, VrsPeripheralRate = s.VrsPeripheralRate,
                DebugFocus = s.DebugFocus, NativeOuterResolution = s.NativeOuterResolution,
                CpuUploadReuse = s.CpuUploadReuse, CpuUploadBatch = s.CpuUploadBatch, CpuOuterReuse = s.CpuOuterReuse, OuterSsr = s.OuterSsr,
                OuterPercent = s.OuterPercent, InnerPercent = s.InnerPercent,
                CropWidthPercent = s.CropWidthPercent, CropHeightPercent = s.CropHeightPercent,
                FocusWidthPercent = s.FocusWidthPercent, FocusHeightPercent = s.FocusHeightPercent,
                FocusOffsetXPercent = s.FocusOffsetXPercent, FocusOffsetYPercent = s.FocusOffsetYPercent };
        }
        // Returns a validated copy of the local settings with this profile applied.
        public LauncherSettings ApplyTo(LauncherSettings local) {
            if (local == null) throw new ArgumentNullException("local");
            var s = QvfrSettings.Clone(local);
            s.Backend = Backend; s.FoveationMode = FoveationMode; s.VrsPeripheralRate = VrsPeripheralRate;
            s.DebugFocus = DebugFocus; s.NativeOuterResolution = NativeOuterResolution;
            s.CpuUploadReuse = CpuUploadReuse; s.CpuUploadBatch = CpuUploadBatch; s.CpuOuterReuse = CpuOuterReuse; s.OuterSsr = OuterSsr;
            s.OuterPercent = OuterPercent; s.InnerPercent = InnerPercent;
            s.CropWidthPercent = CropWidthPercent; s.CropHeightPercent = CropHeightPercent;
            s.FocusWidthPercent = FocusWidthPercent; s.FocusHeightPercent = FocusHeightPercent;
            s.FocusOffsetXPercent = FocusOffsetXPercent; s.FocusOffsetYPercent = FocusOffsetYPercent;
            QvfrSettings.Validate(s);
            return s;
        }
        public string Summary() {
            string provider = Backend == "VRS" ? "Stereo VRS " + VrsPeripheralRate + "x" + VrsPeripheralRate
                : Backend == "QVFR" ? "Quad-Views-Foveated" : Backend == "VectorXR" ? "VectorXR" : "Native OpenXR";
            var parts = new List<string> { provider, FoveationMode == "fixed" ? "fixed focus" : "eye tracking" };
            if (Backend == "QVFR") parts.Add("outer " + Number(OuterPercent) + "% / inner " + Number(InnerPercent) + "%");
            if (Backend == "QVFR" || Backend == "VRS") parts.Add("focus " + Number(FocusWidthPercent) + "×" + Number(FocusHeightPercent) + "%");
            if (Backend != "VRS" && (CropWidthPercent != 100 || CropHeightPercent != 100)) parts.Add("crop " + Number(CropWidthPercent) + "×" + Number(CropHeightPercent) + "%");
            return String.Join(" · ", parts);
        }
        static string Number(decimal value) { return value.ToString("0.##", CultureInfo.InvariantCulture); }
    }

    public sealed class ProfileStore {
        public const string Extension = ".lmuqv";
        public const int FormatVersion = 1;
        public const int MaxNameLength = 48;
        public const int MaxProfiles = 200;
        internal const long MaxFileBytes = 64 * 1024;
        const string RootElement = "LmuQuadViewProfile";
        readonly string directory;

        public ProfileStore(string directory) {
            if (String.IsNullOrWhiteSpace(directory)) throw new ArgumentException("Profile folder is required.");
            this.directory = Path.GetFullPath(directory);
        }
        public string Directory { get { return directory; } }

        public static string CleanName(string name) {
            if (name == null) throw new ArgumentException("Enter a profile name.");
            name = Regex.Replace(name.Normalize(NormalizationForm.FormC), @"\s+", " ").Trim();
            if (name.Length == 0) throw new ArgumentException("Enter a profile name.");
            if (name.Length > MaxNameLength) throw new ArgumentException("Profile names can have at most " + MaxNameLength + " characters.");
            if (name.Any(c => Char.IsControl(c) || c == '�')) throw new ArgumentException("Profile names cannot contain control characters.");
            return name;
        }
        // Stable, Windows-safe file name. The display name stays inside the file.
        internal static string FileStem(string name) {
            var builder = new StringBuilder();
            foreach (char c in name) builder.Append(Path.GetInvalidFileNameChars().Contains(c) || c == '.' || Char.IsWhiteSpace(c) ? '_' : c);
            var stem = builder.ToString().Trim('_');
            if (stem.Length == 0) stem = "profile";
            if (Regex.IsMatch(stem, @"^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$", RegexOptions.IgnoreCase)) stem = "_" + stem;
            return stem;
        }

        public IList<SettingsProfile> List() {
            var result = new List<SettingsProfile>();
            if (!System.IO.Directory.Exists(directory)) return result;
            foreach (var path in System.IO.Directory.GetFiles(directory, "*" + Extension)) {
                try { result.Add(Read(path)); }
                catch (InvalidDataException) { } catch (IOException) { } catch (UnauthorizedAccessException) { } catch (XmlException) { }
            }
            return result.OrderBy(p => p.Name, StringComparer.CurrentCultureIgnoreCase).ToList();
        }
        public bool Exists(string name) { return PathOf(CleanName(name)) != null; }
        public SettingsProfile Load(string name) {
            var path = PathOf(CleanName(name));
            if (path == null) throw new FileNotFoundException("Profile \"" + name + "\" no longer exists.");
            return Read(path);
        }
        public void Save(SettingsProfile profile, bool overwrite) {
            if (profile == null) throw new ArgumentNullException("profile");
            profile.Name = CleanName(profile.Name);
            Validate(profile);
            var existing = PathOf(profile.Name);
            if (existing != null && !overwrite) throw new IOException("A profile named \"" + profile.Name + "\" already exists.");
            if (existing == null && List().Count >= MaxProfiles) throw new IOException("At most " + MaxProfiles + " profiles can be stored.");
            profile.ModifiedUtc = DateTime.UtcNow;
            System.IO.Directory.CreateDirectory(directory);
            Write(existing ?? NewPath(profile.Name), profile);
        }
        public void Rename(string oldName, string newName) {
            oldName = CleanName(oldName); newName = CleanName(newName);
            var source = PathOf(oldName);
            if (source == null) throw new FileNotFoundException("Profile \"" + oldName + "\" no longer exists.");
            var conflict = PathOf(newName);
            if (conflict != null && !String.Equals(conflict, source, StringComparison.OrdinalIgnoreCase))
                throw new IOException("A profile named \"" + newName + "\" already exists.");
            var profile = Read(source); profile.Name = newName; profile.ModifiedUtc = DateTime.UtcNow;
            var target = String.Equals(FileStem(oldName), FileStem(newName), StringComparison.OrdinalIgnoreCase) ? source : NewPath(newName);
            Write(target, profile);
            if (!String.Equals(target, source, StringComparison.OrdinalIgnoreCase)) File.Delete(source);
        }
        public void Delete(string name) {
            var path = PathOf(CleanName(name));
            if (path == null) throw new FileNotFoundException("Profile \"" + name + "\" no longer exists.");
            File.Delete(path);
        }
        public void Export(string name, string destination) {
            if (String.IsNullOrWhiteSpace(destination)) throw new ArgumentException("Choose an export file.");
            var profile = Load(name);
            Write(Path.GetFullPath(destination), profile);
        }
        // Reads and validates a file from another user without storing it yet.
        public static SettingsProfile ReadImport(string path) { return Read(path); }
        // Suggests "Name (2)", "Name (3)" ... for an import that would collide.
        public string AvailableName(string name) {
            name = CleanName(name);
            if (PathOf(name) == null) return name;
            for (int i = 2; i < 1000; i++) {
                var suffix = " (" + i.ToString(CultureInfo.InvariantCulture) + ")";
                var candidate = (name.Length + suffix.Length > MaxNameLength ? name.Substring(0, MaxNameLength - suffix.Length).TrimEnd() : name) + suffix;
                if (PathOf(candidate) == null) return candidate;
            }
            throw new IOException("No free profile name is available.");
        }

        string PathOf(string cleanName) {
            if (!System.IO.Directory.Exists(directory)) return null;
            foreach (var path in System.IO.Directory.GetFiles(directory, "*" + Extension)) {
                try { if (String.Equals(Read(path).Name, cleanName, StringComparison.CurrentCultureIgnoreCase)) return path; }
                catch (InvalidDataException) { } catch (IOException) { } catch (UnauthorizedAccessException) { } catch (XmlException) { }
            }
            return null;
        }
        string NewPath(string name) {
            var stem = FileStem(name);
            var path = Path.Combine(directory, stem + Extension);
            for (int i = 2; File.Exists(path); i++) path = Path.Combine(directory, stem + "-" + i.ToString(CultureInfo.InvariantCulture) + Extension);
            return path;
        }

        static void Validate(SettingsProfile p) {
            // Apply to neutral defaults: the same range checks as saved launcher settings.
            var local = new LauncherSettings { GameDirectory = "profile-validation" };
            p.ApplyTo(local);
        }
        static XElement Scalar(XElement parent, string name) {
            var elements = parent.Elements(name).ToList();
            if (elements.Count != 1) throw new InvalidDataException("The profile must contain exactly one " + name + ".");
            var element = elements[0];
            if (element.HasElements || element.HasAttributes) throw new InvalidDataException("The profile value " + name + " must be plain text.");
            return element;
        }
        static string Text(XElement settings, string name) {
            var element = Scalar(settings, name);
            return element.Value.Trim();
        }
        static decimal Dec(XElement settings, string name) {
            decimal value;
            if (!Decimal.TryParse(Text(settings, name), NumberStyles.AllowLeadingSign | NumberStyles.AllowDecimalPoint, CultureInfo.InvariantCulture, out value))
                throw new InvalidDataException("The profile value " + name + " is not a number.");
            return value;
        }
        static bool Bool(XElement settings, string name) {
            bool value;
            if (!Boolean.TryParse(Text(settings, name), out value)) throw new InvalidDataException("The profile value " + name + " must be true or false.");
            return value;
        }
        static SettingsProfile Read(string path) {
            var info = new FileInfo(path);
            if (!info.Exists) throw new FileNotFoundException("Profile file not found.", path);
            if (info.Length > MaxFileBytes) throw new InvalidDataException("The profile file is too large.");
            var options = new XmlReaderSettings { DtdProcessing = DtdProcessing.Prohibit, XmlResolver = null, MaxCharactersInDocument = MaxFileBytes };
            XElement x;
            try { using (var reader = XmlReader.Create(path, options)) x = XElement.Load(reader); }
            catch (XmlException error) { throw new InvalidDataException("The file is not a valid LMU Quad View profile.", error); }
            if (x.Name != RootElement) throw new InvalidDataException("The file is not an LMU Quad View profile.");
            int format;
            if (!Int32.TryParse((string)x.Attribute("format"), NumberStyles.None, CultureInfo.InvariantCulture, out format) || format < 1)
                throw new InvalidDataException("The profile has no valid format version.");
            if (format > FormatVersion) throw new InvalidDataException("This profile was created by a newer launcher. Update LMU Quad View to import it.");
            var settingsElements = x.Elements("Settings").ToList();
            if (settingsElements.Count != 1) throw new InvalidDataException("The profile must contain exactly one Settings section.");
            var settings = settingsElements[0];
            DateTime modified;
            if (!DateTime.TryParse((string)x.Element("Modified") ?? "", CultureInfo.InvariantCulture, DateTimeStyles.AdjustToUniversal | DateTimeStyles.AssumeUniversal, out modified))
                modified = info.LastWriteTimeUtc;
            int rate;
            if (!Int32.TryParse(Text(settings, "VrsPeripheralRate"), NumberStyles.None, CultureInfo.InvariantCulture, out rate))
                throw new InvalidDataException("The profile value VrsPeripheralRate is invalid.");
            // Only known elements are read; anything else in a shared file is ignored.
            var p = new SettingsProfile {
                Name = NameFrom(x), ModifiedUtc = modified,
                Backend = Text(settings, "Backend"), FoveationMode = Text(settings, "FoveationMode"), VrsPeripheralRate = rate,
                DebugFocus = Bool(settings, "DebugFocus"), NativeOuterResolution = Bool(settings, "NativeOuterResolution"),
                CpuUploadReuse = Bool(settings, "CpuUploadReuse"), CpuUploadBatch = Bool(settings, "CpuUploadBatch"),
                CpuOuterReuse = Bool(settings, "CpuOuterReuse"), OuterSsr = Bool(settings, "OuterSsr"),
                OuterPercent = Dec(settings, "OuterPercent"), InnerPercent = Dec(settings, "InnerPercent"),
                CropWidthPercent = Dec(settings, "CropWidthPercent"), CropHeightPercent = Dec(settings, "CropHeightPercent"),
                FocusWidthPercent = Dec(settings, "FocusWidthPercent"), FocusHeightPercent = Dec(settings, "FocusHeightPercent"),
                FocusOffsetXPercent = Dec(settings, "FocusOffsetXPercent"), FocusOffsetYPercent = Dec(settings, "FocusOffsetYPercent") };
            try { Validate(p); }
            catch (ArgumentException error) { throw new InvalidDataException("The profile contains unsupported values: " + error.Message, error); }
            return p;
        }
        static string NameFrom(XElement x) {
            try { return CleanName((string)Scalar(x, "Name")); }
            catch (ArgumentException error) { throw new InvalidDataException("The profile name is invalid: " + error.Message, error); }
        }
        static void Write(string path, SettingsProfile p) {
            Func<decimal, string> n = v => v.ToString(CultureInfo.InvariantCulture);
            Func<bool, string> b = v => v ? "true" : "false";
            var xml = new XElement(RootElement, new XAttribute("format", FormatVersion),
                new XElement("Name", p.Name),
                new XElement("Modified", p.ModifiedUtc.ToUniversalTime().ToString("o", CultureInfo.InvariantCulture)),
                new XElement("Settings",
                    new XElement("Backend", p.Backend), new XElement("FoveationMode", p.FoveationMode),
                    new XElement("VrsPeripheralRate", p.VrsPeripheralRate.ToString(CultureInfo.InvariantCulture)),
                    new XElement("DebugFocus", b(p.DebugFocus)), new XElement("NativeOuterResolution", b(p.NativeOuterResolution)),
                    new XElement("CpuUploadReuse", b(p.CpuUploadReuse)), new XElement("CpuUploadBatch", b(p.CpuUploadBatch)),
                    new XElement("CpuOuterReuse", b(p.CpuOuterReuse)), new XElement("OuterSsr", b(p.OuterSsr)),
                    new XElement("OuterPercent", n(p.OuterPercent)), new XElement("InnerPercent", n(p.InnerPercent)),
                    new XElement("CropWidthPercent", n(p.CropWidthPercent)), new XElement("CropHeightPercent", n(p.CropHeightPercent)),
                    new XElement("FocusWidthPercent", n(p.FocusWidthPercent)), new XElement("FocusHeightPercent", n(p.FocusHeightPercent)),
                    new XElement("FocusOffsetXPercent", n(p.FocusOffsetXPercent)), new XElement("FocusOffsetYPercent", n(p.FocusOffsetYPercent))));
            var folder = Path.GetDirectoryName(Path.GetFullPath(path)); System.IO.Directory.CreateDirectory(folder);
            var temp = path + ".tmp-" + Guid.NewGuid().ToString("N");
            try {
                File.WriteAllText(temp, xml.ToString(), new UTF8Encoding(false));
                if (File.Exists(path)) File.Replace(temp, path, null); else File.Move(temp, path);
            } finally { if (File.Exists(temp)) File.Delete(temp); }
        }
    }
}
