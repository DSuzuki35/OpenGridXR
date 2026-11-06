using System;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;

namespace LmuQuadViewLauncher {
    public static class LaunchDiagnostics {
        static string Finding(string code,string summary,string action){return code+"\r\n\r\n"+summary+"\r\n\r\n"+action;}
        static bool Has(string text,string value){return text.IndexOf(value,StringComparison.OrdinalIgnoreCase)>=0;}
        public static string Explain(string logText){
            string text=logText??"";
            if(String.IsNullOrWhiteSpace(text))return Finding("NO_LOG","No native mod log is available for the latest launch.","Enable diagnostics, launch LMU again and check its OpenXR startup. A started-process summary alone does not prove the mod loaded. Share the actual quad-*.log when available, not only the run JSON.");
            // Judge the last recorded frame before older errors: a recovered startup
            // must not continue to be reported as a broken VR session.
            var frames=Regex.Matches(text,@"[^\r\n]*xrEndFrame[^\r\n]*");
            string last=frames.Count==0?"":frames[frames.Count-1].Value;
            if(Has(last,"result=-17")||Has(last,"result=-13"))return Finding("SESSION_LOST","The runtime reported that the OpenXR session or instance was lost.","Check the headset/runtime connection, then restart LMU. This log does not establish whether hardware, runtime or another layer caused the loss.");
            bool stereo=Has(last,"mode=vrs");
            string projections=stereo?"stereoProjections":"quadProjections";
            if(Regex.IsMatch(last,projections+@"=[1-9]\d*\b")&&Regex.IsMatch(last,@"result=0\b"))return Finding("SUBMITTING","The last recorded frame successfully submitted a "+(stereo?"stereo":"quad-view")+" scene.","This confirms the mod-to-runtime submission, not what the headset displayed. If the image is still frozen, compare extra layers on/off and include runtime logs. Earlier projection errors may have recovered.");
            if(Has(last,"shouldRender=0")&&Has(last,"result=0"))return Finding("NOT_RENDERING","The runtime asked the game not to render the last recorded frame.","Check headset/session activity and resume VR. Zero projections while shouldRender=0 are expected, not proof of a mod failure.");
            if(Has(last,projections+"=0")&&Has(last,"shouldRender=1")){
                int refused=text.LastIndexOf("Projection refused:",StringComparison.OrdinalIgnoreCase);
                string reason=refused<0?"":text.Substring(refused,Math.Min(700,text.Length-refused));
                if(Has(reason,"spaceMatch=0"))return Finding("SPACE_MISMATCH","Rendering continued, but a projection/reference-space check rejected the VR scene.","Use the motion-compensation candidate. Compare compensation off/on with crop at 100/100; keep the actual quad log. Do not change multiple rendering settings during this test.");
                if(Has(reason,"invalid submitted or cached pose"))return Finding("INVALID_POSE","The submitted or located camera pose failed validation.","Check headset tracking and compare motion-compensation layers off/on. The mod rejects invalid poses to avoid incorrect VR placement.");
                if(Has(reason,"native eye identity")||Has(reason,"subImage"))return Finding("EYE_RESOURCE","The submitted eye image could not be matched safely to LMU's expected resource.","Compare extra resolution overrides/upscalers off/on, one at a time. Keep provider and mod resolution settings unchanged for the comparison; share the actual quad log.");
                return Finding("NO_SCENE_SUBMISSION","The runtime requested rendering, but the last frame contained no accepted "+(stereo?"stereo":"quad-view")+" scene.","The desktop image can continue while headset submission is missing. Capture a fresh mod log after reaching the cockpit; compare extra OpenXR layers one at a time. This alone does not prove a process crash.");
            }
            if(Has(text,"result=-26"))return Finding("SWAPCHAIN_FORMAT","A requested OpenXR image format was rejected by the downstream provider.","Check which swapchain failed in the actual quad log. If it is only the benchmark overlay, turn that overlay off and retry; an overlay failure should not stop the game scene.");
            if(Has(text,"build guard")||Has(text,"unsupported game executable")||Has(text,"executable hash")||Has(text,"code anchor mismatch")||Has(text,"ABI anchor mismatch")||Has(text,"is not a function start in this executable"))return Finding("GAME_BUILD","The renderer-signature check (or an older mod's executable-hash check) rejected this installation.","Use a mod build compatible with this LMU version. Current builds accept different executable hashes but still require matching native hook signatures and function boundaries.");
            if(Has(text,"engine_start result=0")||Has(text,"refusing session")||Regex.IsMatch(text,@"xrCreateSession[^\r\n]*result=-"))return Finding("SESSION_START","OpenXR session setup or the renderer hooks failed before normal VR submission.","Verify the selected OpenXR runtime and any provider required by the chosen mode. Stereo VRS requires no quad-view provider. Compare optional layers disabled and inspect the first error in the actual mod log.");
            return Finding("INCOMPLETE_EVIDENCE","The available log does not contain a conclusive final rendering result.","Record a fresh launch with diagnostics enabled, reach the cockpit if possible, then close LMU normally. Share the actual quad-*.log plus its run summary; review personal paths before sharing.");
        }
        public static string ExplainLatest(string root){
            try {
                string folder=Path.Combine(root,"logs");if(!Directory.Exists(folder))return Explain("");
                FileInfo newest=null;
                foreach(var file in new DirectoryInfo(folder).GetFiles("quad-*")){
                    if(file.Extension!=".log"&&!file.Name.EndsWith("-run.json",StringComparison.OrdinalIgnoreCase))continue;
                    if(newest==null||file.LastWriteTimeUtc>newest.LastWriteTimeUtc)newest=file;
                }
                if(newest==null)return Explain("");
                string path=newest.FullName;
                if(newest.Name.EndsWith("-run.json",StringComparison.OrdinalIgnoreCase))path=Path.Combine(folder,newest.Name.Substring(0,newest.Name.Length-9)+".log");
                if(!File.Exists(path))return Explain("");
                using(var stream=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete)){
                    const int limit=128*1024;
                    if(stream.Length<=limit*2)using(var reader=new StreamReader(stream))return Explain(reader.ReadToEnd());
                    var start=new byte[limit];int startCount=stream.Read(start,0,start.Length);
                    stream.Seek(-limit,SeekOrigin.End);var tail=new byte[limit];int tailCount=stream.Read(tail,0,tail.Length);
                    return Explain(Encoding.UTF8.GetString(start,0,startCount)+"\n[older middle omitted]\n"+Encoding.UTF8.GetString(tail,0,tailCount));
                }
            }catch(IOException){return Finding("LOG_BUSY","The latest log could not be read right now.","Retry after LMU has finished writing or closed normally.");}
            catch(UnauthorizedAccessException){return Finding("LOG_ACCESS","Windows denied access to the latest log.","Extract the complete mod into a writable folder and check its file permissions.");}
        }
    }
}
