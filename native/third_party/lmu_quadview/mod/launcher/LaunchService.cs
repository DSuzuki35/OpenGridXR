using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;
using Microsoft.Win32;

namespace LmuQuadViewLauncher {
    public static class LaunchService {
        public const string SupportedExecutableHash="66942337813B301B87D12EDFA330F33354A2A4B8CD9B048885CBF77EB6F940CF";
        const string ProviderName="XR_APILAYER_MBUCCHIA_quad_views_foveated";
        internal const string VectorXrProviderName="XR_APILAYER_DIENERTECH_VECTORXR";
        const string AdapterName="XR_APILAYER_LMU_quadview";
        internal sealed class ProviderManifest {
            public string Path,Library,DisableVariable,Name;
            public Dictionary<string,object> Document;
        }
        internal sealed class LayerPlan {
            public string Paths,Layers,DisableVariable,ProviderPath,ExplicitManifest,Library,ProviderName;
            public readonly List<string> DisableVariables=new List<string> {
                "DISABLE_XR_APILAYER_MBUCCHIA_quad_views_foveated", "DISABLE_QUAD_VIEWS_FOVEATED",
                "DISABLE_XR_APILAYER_DIENERTECH_VECTORXR"
            };
        }
        static string Value(Dictionary<string,object> dictionary,string key) {
            object value;return dictionary.TryGetValue(key,out value)?value as string:null;
        }
        static Dictionary<string,object> ReadJson(string path) {
            try { return new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(File.ReadAllText(path)); }
            catch(Exception error) { throw new InvalidDataException("Cannot read OpenXR manifest: "+path,error); }
        }
        static Dictionary<string,object> Layer(Dictionary<string,object> document) {
            object raw;return document!=null&&document.TryGetValue("api_layer",out raw)?raw as Dictionary<string,object>:null;
        }
        static string NameForBackend(string backend) {
            if(backend=="QVFR")return ProviderName;
            if(backend=="VectorXR")return VectorXrProviderName;
            throw new ArgumentException("A software quad-view provider must be QVFR or VectorXR.");
        }
        static bool ValidDisable(string name,string value) {
            string marker=name==VectorXrProviderName?"vectorxr":name==ProviderName?"quad":"toolkit";
            return !String.IsNullOrEmpty(value)&&Regex.IsMatch(value,"^[A-Za-z_][A-Za-z0-9_]*$")&&
                value.IndexOf("disable",StringComparison.OrdinalIgnoreCase)>=0&&value.IndexOf(marker,StringComparison.OrdinalIgnoreCase)>=0;
        }
        internal static ProviderManifest ReadProviderManifest(string path,bool requireLibrary=true,string backend="QVFR") {
            path=System.IO.Path.GetFullPath(path);
            var document=ReadJson(path);var layer=Layer(document);
            var expectedName=NameForBackend(backend);
            if(layer==null||Value(layer,"name")!=expectedName) throw new InvalidDataException("Select the "+backend+" JSON manifest ("+expectedName+").");
            var disable=Value(layer,"disable_environment");
            if(!ValidDisable(expectedName,disable))
                throw new InvalidDataException(backend+" manifest has no recognizable disable_environment switch; safe layer ordering is unavailable.");
            var library=Value(layer,"library_path");
            if(String.IsNullOrWhiteSpace(library)) throw new InvalidDataException(backend+" manifest has no library_path.");
            if(!System.IO.Path.IsPathRooted(library)) library=System.IO.Path.Combine(System.IO.Path.GetDirectoryName(path),library);
            library=System.IO.Path.GetFullPath(library);
            if(requireLibrary&&!File.Exists(library)) throw new FileNotFoundException(backend+" library was not found. Repair its installation.",library);
            return new ProviderManifest {Path=path,Library=library,DisableVariable=disable,Name=expectedName,Document=document};
        }
        static List<string> RegisteredManifests(bool onlyEnabled=false) {
            var candidates=new List<string>();
            foreach(var hive in new[]{RegistryHive.LocalMachine,RegistryHive.CurrentUser}) {
                try {
                    using(var baseKey=RegistryKey.OpenBaseKey(hive,RegistryView.Registry64))
                    using(var key=baseKey.OpenSubKey(@"SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit",false)) {
                        if(key!=null) foreach(var name in key.GetValueNames()) if(name.EndsWith(".json",StringComparison.OrdinalIgnoreCase)) {
                            var value=key.GetValue(name);
                            if(!onlyEnabled||(value is int&&(int)value==0))candidates.Add(name);
                        }
                    }
                } catch(System.Security.SecurityException) { } catch(UnauthorizedAccessException) { }
            }
            return candidates;
        }
        static void AddManifestDirectory(List<string> candidates,string path,int depth) {
            if(!Directory.Exists(path))return;
            try {
                candidates.AddRange(Directory.GetFiles(path,"*.json"));
                if(depth>0)foreach(var child in Directory.GetDirectories(path))AddManifestDirectory(candidates,child,depth-1);
            } catch(IOException) { } catch(UnauthorizedAccessException) { }
        }
        internal static string FindProviderInCandidates(IEnumerable<string> candidates,string backend) {
            var expectedName=NameForBackend(backend);
            foreach(var path in candidates) {
                if(!File.Exists(path))continue;
                try { var layer=Layer(ReadJson(path));if(layer!=null&&Value(layer,"name")==expectedName)return path; }
                catch(InvalidDataException) { } catch(IOException) { } catch(UnauthorizedAccessException) { }
            }
            return null;
        }
        static string FindProviderManifest(string backend="QVFR") {
            var candidates=RegisteredManifests();
            foreach(var directory in backend=="VectorXR"?new[]{"VectorXR"}:new[]{"OpenXR-Quad-Views-Foveated","Quad-Views-Foveated"}) {
                var path=System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),directory);
                AddManifestDirectory(candidates,path,2);
            }
            if(backend=="VectorXR") {
                var local=Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
                AddManifestDirectory(candidates,System.IO.Path.Combine(local,@"Programs\VectorXR"),2);
                AddManifestDirectory(candidates,System.IO.Path.Combine(local,"VectorXR"),1);
            }
            return FindProviderInCandidates(candidates,backend);
        }
        internal static void AddImplicitDisables(LayerPlan plan,IEnumerable<string> manifests,string backend) {
            // Explicit copies ignore activation switches; originals remain disabled
            // only in this child. Never rewrite registry values or installed files.
            if(backend=="VectorXR"||backend=="VRS")plan.DisableVariables.Add("DISABLE_XR_APILAYER_MBUCCHIA_toolkit");
            foreach(var path in manifests) {
                if(!File.Exists(path))continue;
                Dictionary<string,object> layer;
                try {
                    layer=Layer(ReadJson(path));
                } catch(InvalidDataException) { continue; } catch(IOException) { continue; } catch(UnauthorizedAccessException) { continue; }
                if(layer==null)continue;
                var name=Value(layer,"name");
                // QVFR can coexist with VectorXR's independently configured OSD.
                // Leave its registered layer and activation preferences alone.
                if(backend=="QVFR"&&name==VectorXrProviderName)continue;
                if(name!=ProviderName&&name!=VectorXrProviderName&&!((backend=="VectorXR"||backend=="VRS")&&name=="XR_APILAYER_MBUCCHIA_toolkit"))continue;
                var disable=Value(layer,"disable_environment");
                if(!ValidDisable(name,disable))throw new InvalidDataException("Cannot isolate installed layer "+name+": missing recognizable disable switch.");
                if(!plan.DisableVariables.Contains(disable))plan.DisableVariables.Add(disable);
            }
        }
        static void CheckSearchPath(string path) {
            if(path.IndexOf(';')>=0) throw new ArgumentException("Package/run paths cannot contain ';', the OpenXR search-path separator.");
        }
        internal static LayerPlan MakeLayerPlan(string root,string backend,ProviderManifest provider,string runDirectory) {
            root=System.IO.Path.GetFullPath(root);CheckSearchPath(root);
            var plan=new LayerPlan {Paths=root,Layers=AdapterName,DisableVariable=provider==null?null:provider.DisableVariable};
            if(backend=="QVFR")plan.DisableVariables.Remove("DISABLE_XR_APILAYER_DIENERTECH_VECTORXR");
            if(provider!=null&&!plan.DisableVariables.Contains(provider.DisableVariable))plan.DisableVariables.Add(provider.DisableVariable);
            if(backend=="Native"||backend=="VRS") return plan;
            var expectedName=NameForBackend(backend);
            if(provider==null||provider.Name!=expectedName) throw new InvalidOperationException("Install "+backend+" or select its matching JSON manifest.");
            if(String.IsNullOrWhiteSpace(runDirectory)) throw new ArgumentException("Unique layer run directory is required.");
            runDirectory=System.IO.Path.GetFullPath(runDirectory);CheckSearchPath(runDirectory);
            if(Directory.Exists(runDirectory)||File.Exists(runDirectory)) throw new IOException("Layer run directory already exists; refusing to overwrite it.");
            var document=new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(new JavaScriptSerializer().Serialize(provider.Document));
            var layer=Layer(document);layer["library_path"]=provider.Library;
            layer.Remove("disable_environment");layer.Remove("enable_environment");
            Directory.CreateDirectory(runDirectory);
            plan.ExplicitManifest=System.IO.Path.Combine(runDirectory,expectedName+".json");
            WriteJson(plan.ExplicitManifest,document);
            plan.Paths=root+";"+runDirectory;plan.Layers=AdapterName+";"+expectedName;
            plan.ProviderPath=provider.Path;plan.Library=provider.Library;plan.ProviderName=expectedName;
            return plan;
        }
        internal static ProcessStartInfo MakeStartInfo(string root,LauncherSettings settings,LayerPlan plan,string log) {
            var game=System.IO.Path.GetFullPath(settings.GameDirectory);
            var info=new ProcessStartInfo {FileName=System.IO.Path.Combine(game,"Le Mans Ultimate.exe"),Arguments="+XR",WorkingDirectory=game,UseShellExecute=false,WindowStyle=ProcessWindowStyle.Normal};
            // Never inherit experiments or stale capture/benchmark triggers from another launch.
            var inherited=new List<string>();foreach(string key in info.EnvironmentVariables.Keys) if(key.StartsWith("LMU_QV_",StringComparison.OrdinalIgnoreCase)) inherited.Add(key);
            foreach(var key in inherited) info.EnvironmentVariables.Remove(key);
            var environment=info.EnvironmentVariables;
            environment["XR_API_LAYER_PATH"]=plan.Paths;environment["XR_ENABLE_API_LAYERS"]=plan.Layers;
            if(!String.IsNullOrEmpty(plan.DisableVariable)) environment[plan.DisableVariable]="1";
            foreach(var variable in plan.DisableVariables)environment[variable]="1";
            bool vrs=settings.Backend=="VRS";
            // OpenGridXR crops the stereo FOV above this adapter. The native
            // outer scene then uses that crop while focus rendering/submission
            // uses the provider FOV, producing misaligned geometry. Isolate
            // quad launches only; installed profiles and native stereo stay intact.
            if(!vrs)environment["DISABLE_OPENGRIDXR"]="1";
            if(settings.Backend=="VectorXR"||vrs)environment["DISABLE_XR_APILAYER_MBUCCHIA_toolkit"]="1";
            environment["LMU_QV_MODE"]=vrs?"vrs":"quad";environment["LMU_QV_USE_RUNTIME_SETTINGS"]=vrs?"0":"1";
            environment["LMU_QV_FOVEATION_MODE"]=settings.FoveationMode;environment["LMU_QV_LOG"]=settings.DiagnosticsEnabled?log:"";
            environment["LMU_QV_DIAGNOSTICS"]=settings.DiagnosticsEnabled?"1":"0";
            environment["LMU_QV_BENCHMARK"]=settings.DiagnosticsEnabled&&settings.BenchmarkEnabled?"1":"0";
            bool outerReuse=!vrs&&settings.CpuOuterReuse&&!settings.CpuUploadBatch;
            bool audit=!vrs&&settings.DiagnosticsEnabled&&settings.StereoDrawAudit&&!settings.CpuUploadBatch&&!outerReuse;
            environment["LMU_QV_STEREO_AUDIT"]=audit?"1":"0";
            environment["LMU_QV_STEREO_AUDIT_DIR"]=audit?System.IO.Path.GetDirectoryName(log):"";
            environment["LMU_QV_STEREO_SHADER_CAPTURE"]=audit&&settings.StereoShaderCapture?"1":"0";
            environment["LMU_QV_STEREO_DRAW_PROOF"]=audit&&settings.StereoPairProof?"1":"0";
            environment["LMU_QV_CPU_UPLOAD_REUSE"]=!vrs&&(settings.CpuUploadReuse||outerReuse)&&!settings.CpuUploadBatch?"1":"0";
            environment["LMU_QV_CPU_UPLOAD_BATCH"]=!vrs&&settings.CpuUploadBatch?"1":"0";
            environment["LMU_QV_CPU_OUTER_REUSE"]=outerReuse?"1":"0";
            environment["LMU_QV_OUTER_SSR"]=!vrs&&settings.NativeOuterResolution&&settings.OuterSsr?"1":"0";
            environment["LMU_QV_DEBUG_FOCUS"]=settings.DebugFocus?"1":"0";
            environment["LMU_QV_PROFILE"]=settings.DiagnosticsEnabled&&settings.RenderProfiling?"1":"0";
            environment["LMU_QV_PROFILE_SECONDS"]=settings.RenderProfileSeconds.ToString(CultureInfo.InvariantCulture);
            environment["LMU_QV_VECTORXR_DIRECT_INPUTS"]=settings.Backend=="VectorXR"?"1":"0";
            environment["LMU_QV_DYNAMIC_FOCUS"]=!vrs&&settings.DynamicFocusEnabled?"1":"0";
            environment["LMU_QV_DYNAMIC_FOCUS_MIN"]=settings.DynamicFocusMinPercent.ToString(CultureInfo.InvariantCulture);
            environment["LMU_QV_DYNAMIC_FOCUS_MAX"]=settings.DynamicFocusMaxPercent.ToString(CultureInfo.InvariantCulture);
            environment["LMU_QV_DYNAMIC_FOCUS_RESERVE"]=settings.DynamicFocusReservePercent.ToString(CultureInfo.InvariantCulture);
            environment["LMU_QV_OUTER_BLUR"]=vrs?"0":settings.OuterBlur.ToString(CultureInfo.InvariantCulture);
            environment["LMU_QV_CROP_WIDTH_PERCENT"]=vrs?"100":settings.CropWidthPercent.ToString(CultureInfo.InvariantCulture);
            environment["LMU_QV_CROP_HEIGHT_PERCENT"]=vrs?"100":settings.CropHeightPercent.ToString(CultureInfo.InvariantCulture);
            environment["LMU_QV_FOCUS_OFFSET_X_PERCENT"]=settings.FocusOffsetXPercent.ToString(CultureInfo.InvariantCulture);
            environment["LMU_QV_FOCUS_OFFSET_Y_PERCENT"]=settings.FocusOffsetYPercent.ToString(CultureInfo.InvariantCulture);
            environment["LMU_QV_NATIVE_OUTER"]=!vrs&&settings.NativeOuterResolution?"1":"0";
            environment["LMU_QV_PIXEL_STATS"]="0";environment["LMU_QV_VRS_EXCLUSIVE"]=vrs?"1":"0";environment["LMU_QV_OUTER_SHADING_RATE"]=vrs?settings.VrsPeripheralRate.ToString(CultureInfo.InvariantCulture):"2";
            foreach(var key in new[]{"CAPTURE_DIR","CAPTURE_TRIGGER","GPU_TIMING_TRIGGER","BENCH_CONTROL"}) environment["LMU_QV_"+key]="";
            foreach(var key in new[]{"OUTER_PIXELS_PERCENT","FOCUS_PIXELS_PERCENT","FOCUS_BOX_PERCENT","FOCUS_WIDTH_PERCENT","FOCUS_HEIGHT_PERCENT"}) environment["LMU_QV_"+key]="100";
            if(vrs) {
                environment["LMU_QV_FOCUS_WIDTH_PERCENT"]=settings.FocusWidthPercent.ToString(CultureInfo.InvariantCulture);
                environment["LMU_QV_FOCUS_HEIGHT_PERCENT"]=settings.FocusHeightPercent.ToString(CultureInfo.InvariantCulture);
            }
            return info;
        }
        static string HashFile(string path) {
            using(var input=File.OpenRead(path)) using(var sha=SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(input)).Replace("-","");
        }
        internal static void ValidateExecutable(string executable) {
            if(!File.Exists(executable)) throw new FileNotFoundException("Select the folder containing Le Mans Ultimate.exe.",executable);
            if(!String.Equals(HashFile(executable),SupportedExecutableHash,StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("LMU executable differs from the supported build. This mod needs validation for that game version.");
        }
        static void WriteJson(string path,object value) {
            File.WriteAllText(path,new JavaScriptSerializer().Serialize(value),new UTF8Encoding(false));
        }
        static string MetadataPath(ProcessStartInfo info) { return System.IO.Path.ChangeExtension(info.EnvironmentVariables["LMU_QV_LOG"],null)+"-run.json"; }
        public static ProcessStartInfo Prepare(string root,LauncherSettings settings) {
            QvfrSettings.Validate(settings);
            root=System.IO.Path.GetFullPath(root);CheckSearchPath(root);
            ValidateExecutable(System.IO.Path.Combine(System.IO.Path.GetFullPath(settings.GameDirectory),"Le Mans Ultimate.exe"));
            var processes=Process.GetProcessesByName("Le Mans Ultimate");
            bool running=processes.Length!=0;foreach(var process in processes) process.Dispose();
            if(running) throw new InvalidOperationException("LMU is already running. Close it before launching again.");
            foreach(var relative in new[]{"XR_APILAYER_LMU_quadview.json",@"build\lmu_quadview.dll"}) if(!File.Exists(System.IO.Path.Combine(root,relative))) throw new FileNotFoundException("The mod package is incomplete: "+relative);
            var ownLayer=Layer(ReadJson(System.IO.Path.Combine(root,"XR_APILAYER_LMU_quadview.json")));
            if(ownLayer==null||Value(ownLayer,"name")!=AdapterName) throw new InvalidDataException("The LMU layer manifest is invalid.");
            var ownLibrary=Value(ownLayer,"library_path");
            if(String.IsNullOrWhiteSpace(ownLibrary)||!String.Equals(System.IO.Path.GetFullPath(System.IO.Path.Combine(root,ownLibrary)),System.IO.Path.Combine(root,@"build\lmu_quadview.dll"),StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("The LMU manifest must reference this package's build/lmu_quadview.dll.");
            bool vrs=settings.Backend=="VRS";
            var providerBackend=settings.Backend=="Native"||vrs?"QVFR":settings.Backend;
            var custom=settings.Backend=="VectorXR"?settings.VectorXrManifest:settings.QuadViewsManifest;
            string providerPath=vrs?null:settings.Backend!="Native"&&!String.IsNullOrWhiteSpace(custom)?custom:FindProviderManifest(providerBackend);
            ProviderManifest provider=String.IsNullOrWhiteSpace(providerPath)?null:ReadProviderManifest(providerPath,settings.Backend!="Native",providerBackend);
            if(settings.Backend!="Native"&&!vrs&&provider==null) throw new FileNotFoundException(settings.Backend+" is not installed. Install it from the Setup page or select its JSON manifest.");
            var logDirectory=System.IO.Path.Combine(root,"logs");
            var stamp=DateTime.Now.ToString("yyyyMMdd-HHmmss",CultureInfo.InvariantCulture)+"-"+Guid.NewGuid().ToString("N").Substring(0,8);
            var runDirectory=System.IO.Path.Combine(root,settings.DiagnosticsEnabled?"logs":"runtime");
            var plan=MakeLayerPlan(root,settings.Backend,provider,System.IO.Path.Combine(runDirectory,"layers-"+stamp));
            AddImplicitDisables(plan,RegisteredManifests(true),settings.Backend);
            if(settings.DiagnosticsEnabled)Directory.CreateDirectory(logDirectory);
            var log=System.IO.Path.Combine(logDirectory,"quad-"+stamp+".log");
            var info=MakeStartInfo(root,settings,plan,log);
            if(settings.DiagnosticsEnabled)WriteJson(MetadataPath(info),PreparedMetadata(root,settings,plan,info,log));
            return info;
        }
        internal static object PreparedMetadata(string root,LauncherSettings settings,LayerPlan plan,ProcessStartInfo info,string log) {
            return new {Status="prepared",Pid=(int?)null,Mode=settings.Backend=="VRS"?"vrs":"quad",Backend=settings.Backend,FoveationMode=settings.FoveationMode,VrsPeripheralRate=settings.VrsPeripheralRate,
                Exe=info.FileName,ExeSHA256=SupportedExecutableHash,LayerSHA256=HashFile(System.IO.Path.Combine(root,@"build\lmu_quadview.dll")),
                ProviderManifest=plan.ProviderPath,ProviderName=plan.ProviderName,ProviderSHA256=plan.Library==null?null:HashFile(plan.Library),
                ExplicitManifest=plan.ExplicitManifest,EnabledLayers=plan.Layers,DisabledImplicitSwitches=plan.DisableVariables,OpenGridXrDisabled=info.EnvironmentVariables["DISABLE_OPENGRIDXR"]=="1",Log=log,
                DiagnosticsEnabled=settings.DiagnosticsEnabled,BenchmarkEnabled=settings.DiagnosticsEnabled&&settings.BenchmarkEnabled,BenchmarkRequested=settings.BenchmarkEnabled,BenchmarkTemplate=settings.BenchmarkTemplate,
                DebugFocus=settings.DebugFocus,RenderProfiling=settings.RenderProfiling,StereoDrawAudit=info.EnvironmentVariables["LMU_QV_STEREO_AUDIT"]=="1",StereoShaderCapture=info.EnvironmentVariables["LMU_QV_STEREO_SHADER_CAPTURE"]=="1",StereoPairProof=info.EnvironmentVariables["LMU_QV_STEREO_DRAW_PROOF"]=="1",CpuUploadReuse=info.EnvironmentVariables["LMU_QV_CPU_UPLOAD_REUSE"]=="1",CpuUploadBatch=info.EnvironmentVariables["LMU_QV_CPU_UPLOAD_BATCH"]=="1",RenderProfileSeconds=settings.RenderProfileSeconds,NativeOuterResolution=info.EnvironmentVariables["LMU_QV_NATIVE_OUTER"]=="1",UseRuntimeSettings=settings.Backend!="VRS",
                CpuOuterReuse=info.EnvironmentVariables["LMU_QV_CPU_OUTER_REUSE"]=="1",OuterSsr=info.EnvironmentVariables["LMU_QV_OUTER_SSR"]=="1",CpuOuterReuseRequested=settings.CpuOuterReuse,OuterSsrRequested=settings.OuterSsr,
                VectorXrDirectInputs=settings.Backend=="VectorXR",OuterBlur=settings.OuterBlur,
                DynamicFocusEnabled=settings.Backend!="VRS"&&settings.DynamicFocusEnabled,DynamicFocusMinPercent=settings.DynamicFocusMinPercent,
                DynamicFocusMaxPercent=settings.DynamicFocusMaxPercent,DynamicFocusReservePercent=settings.DynamicFocusReservePercent,
                CropWidthPercent=settings.CropWidthPercent,CropHeightPercent=settings.CropHeightPercent,
                FocusOffsetXPercent=settings.FocusOffsetXPercent,FocusOffsetYPercent=settings.FocusOffsetYPercent,
                OuterPercent=settings.OuterPercent,InnerPercent=settings.InnerPercent,FocusWidthPercent=settings.FocusWidthPercent,FocusHeightPercent=settings.FocusHeightPercent,
                PreparedUTC=DateTime.UtcNow.ToString("o",CultureInfo.InvariantCulture)};
        }
        public static int Launch(string root,LauncherSettings settings) {
            var info=Prepare(root,settings);
            try {
            // Preparation is testable without touching the user's QVFR profile.
            // Only the actual launch commits this configuration, after all guards.
            if(settings.Backend=="QVFR") QvfrSettings.SaveProfile(settings);
            using(var process=Process.Start(info)) {
                if(process==null) throw new InvalidOperationException("Windows did not start LMU.");
                int pid=process.Id;
                if(!settings.DiagnosticsEnabled)return pid;
                try {
                    var path=MetadataPath(info);var metadata=ReadJson(path);
                    metadata["Status"]="started";metadata["Pid"]=pid;metadata["StartedUTC"]=DateTime.UtcNow.ToString("o",CultureInfo.InvariantCulture);WriteJson(path,metadata);
                } catch(IOException) { } catch(UnauthorizedAccessException) { } // Game already started; do not report a false launch failure.
                return pid;
            }
            } catch(Exception error) {
                if(settings.DiagnosticsEnabled)try {
                    var path=MetadataPath(info);var metadata=ReadJson(path);
                    metadata["Status"]="failed";metadata["Error"]=error.Message;WriteJson(path,metadata);
                } catch(IOException) { } catch(UnauthorizedAccessException) { }
                throw;
            }
        }
    }
}
