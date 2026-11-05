using System;
using System.IO;
class LaunchDiagnosticsTests {
    static void Has(string text,string expected){if(!text.Contains(expected))throw new Exception("Missing diagnosis "+expected+": "+text);}
    static int Main(){
        try {
            Has(LmuQuadViewLauncher.LaunchDiagnostics.Explain(""),"NO_LOG");
            Has(LmuQuadViewLauncher.LaunchDiagnostics.Explain("xrEndFrame mode=vrs frame=4 stereoProjections=1 shouldRender=1 result=0"),"SUBMITTING");
            Has(LmuQuadViewLauncher.LaunchDiagnostics.Explain("xrEndFrame mode=vrs frame=4 stereoProjections=1 shouldRender=1 result=0"),"stereo");
            Has(LmuQuadViewLauncher.LaunchDiagnostics.Explain("xrEndFrame mode=vrs frame=4 stereoProjections=0 shouldRender=1 result=0"),"NO_SCENE_SUBMISSION");
            Has(LmuQuadViewLauncher.LaunchDiagnostics.Explain("xrEndFrame mode=vrs frame=4 stereoProjections=0 shouldRender=0 result=0"),"NOT_RENDERING");
            Has(LmuQuadViewLauncher.LaunchDiagnostics.Explain("xrEndFrame mode=vrs frame=4 stereoProjections=1 shouldRender=1 result=-17"),"SESSION_LOST");
            Has(LmuQuadViewLauncher.LaunchDiagnostics.Explain("xrBeginFrame shouldRender=0\nxrEndFrame frame=2 quadProjections=0 shouldRender=0 result=0"),"NOT_RENDERING");
            Has(LmuQuadViewLauncher.LaunchDiagnostics.Explain("Projection refused: captures=1/1 spaceMatch=0\nxrEndFrame frame=2 quadProjections=0 shouldRender=1 result=0"),"SPACE_MISMATCH");
            Has(LmuQuadViewLauncher.LaunchDiagnostics.Explain("Projection refused: invalid submitted or cached pose\nxrEndFrame frame=2 quadProjections=0 shouldRender=1 result=0"),"INVALID_POSE");
            Has(LmuQuadViewLauncher.LaunchDiagnostics.Explain("Projection refused: native eye identity, format, or subImage bounds invalid\nxrEndFrame frame=2 quadProjections=0 shouldRender=1 result=0"),"EYE_RESOURCE");
            Has(LmuQuadViewLauncher.LaunchDiagnostics.Explain("xrCreateSession result=-6"),"SESSION_START");
            Has(LmuQuadViewLauncher.LaunchDiagnostics.Explain("create outer 0 result=-26"),"SWAPCHAIN_FORMAT");
            Has(LmuQuadViewLauncher.LaunchDiagnostics.Explain("xrEndFrame frame=2 quadProjections=1 shouldRender=1 result=-17"),"SESSION_LOST");
            Has(LmuQuadViewLauncher.LaunchDiagnostics.Explain("Projection refused: spaceMatch=0\nxrEndFrame frame=5 quadProjections=1 shouldRender=1 result=0"),"SUBMITTING");
            Has(LmuQuadViewLauncher.LaunchDiagnostics.Explain("unsupported game executable hash C:\\Users\\PrivatePerson\\secret.exe"),"GAME_BUILD");
            var directory=Path.Combine(Path.GetTempPath(),"lmu-diagnosis-"+Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(Path.Combine(directory,"logs"));
            try {
                File.WriteAllText(Path.Combine(directory,"logs","quad-a.log"),"xrEndFrame frame=5 quadProjections=1 shouldRender=1 result=0");
                File.WriteAllText(Path.Combine(directory,"logs","quad-b-run.json"),"{\"Status\":\"started\",\"Exe\":\"private secret\"}");
                File.SetLastWriteTimeUtc(Path.Combine(directory,"logs","quad-b-run.json"),DateTime.UtcNow.AddSeconds(2));
                var diagnosis=LmuQuadViewLauncher.LaunchDiagnostics.ExplainLatest(directory);
                Has(diagnosis,"NO_LOG");if(diagnosis.Contains("private secret"))throw new Exception("Raw private metadata leaked");
            }finally{Directory.Delete(directory,true);}
            Console.WriteLine("Launch diagnosis tests PASS: missing log, suppressed frame, stale errors, resource/space/pose/runtime failure, metadata selection/privacy");return 0;
        }catch(Exception e){Console.Error.WriteLine(e);return 1;}
    }
}
