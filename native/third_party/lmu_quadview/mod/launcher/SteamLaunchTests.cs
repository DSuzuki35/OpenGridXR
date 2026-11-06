using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using LmuQuadViewLauncher;

class SteamLaunchTests {
    static void Check(bool value,string message) { if(!value)throw new Exception(message); }
    static void Reject(Action action) { bool rejected=false;try {action();}catch(ArgumentException){rejected=true;}Check(rejected,"Invalid Steam request accepted"); }
    static int Main(string[] args) {
        if(args.Length>0&&args[0]=="--echo") {
            for(int i=1;i<args.Length;i++)Console.WriteLine(Convert.ToBase64String(Encoding.UTF8.GetBytes(args[i])));
            return 0;
        }
        var method=typeof(LaunchService).Assembly.GetType("LmuQuadViewLauncher.SteamLaunch");
        Check(method!=null,"Steam launch support is missing");
        return Run();
    }
    static int Run() {
        // Reflected calls let the regression fail at runtime before implementation.
        var type=typeof(LaunchService).Assembly.GetType("LmuQuadViewLauncher.SteamLaunch");
        Func<string,string> quote=value=>(string)type.GetMethod("QuoteArgument").Invoke(null,new object[]{value});
        string[] values={"", "plain", "two words", "C:\\with spaces\\", "embedded\"quote", "a\\\\\"b", "ä&%PATH%|<>^", "line\nfeed"};
        var command="--echo";foreach(var value in values)command+=" "+quote(value);
        using(var process=Process.Start(new ProcessStartInfo {FileName=System.Reflection.Assembly.GetExecutingAssembly().Location,Arguments=command,UseShellExecute=false,RedirectStandardOutput=true,CreateNoWindow=true})) {
            foreach(var value in values)Check(process.StandardOutput.ReadLine()==Convert.ToBase64String(Encoding.UTF8.GetBytes(value)),"Windows argument round trip failed");
            process.WaitForExit();Check(process.ExitCode==0,"Argument child failed");
        }
        var settings=new LauncherSettings {GameDirectory=Path.Combine(Path.GetTempPath(),"LMU Steam test")};
        var exe=Path.Combine(settings.GameDirectory,"Le Mans Ultimate.exe");
        Func<string[],string> parse=input=> {
            try {return (string)type.GetMethod("GameArguments").Invoke(null,new object[]{input,settings});}
            catch(System.Reflection.TargetInvocationException error){throw error.InnerException;}
        };
        Check(parse(new[]{"--steam",exe})=="\"+XR\"","Missing +XR not added");
        Check(parse(new[]{"--steam",exe,"+XR","+foo","hello world"})=="\"+XR\" \"+foo\" \"hello world\"","Steam arguments not preserved");
        Check(parse(new[]{"--steam",exe,"+xr"})=="\"+xr\"","Existing XR flag duplicated");
        Reject(()=>parse(new string[0]));Reject(()=>parse(new[]{"--steam"}));
        Reject(()=>parse(new[]{"--other",exe}));Reject(()=>parse(new[]{"--steam","%command%"}));
        Reject(()=>parse(new[]{"--steam",Path.Combine(settings.GameDirectory,"other.exe")}));
        Reject(()=>parse(new[]{"--steam",Path.Combine(settings.GameDirectory,"other","Le Mans Ultimate.exe")}));
        var option=(string)type.GetMethod("LaunchOptions").Invoke(null,new object[]{@"C:\Mod with spaces\LMU-QuadView-Launcher.exe"});
        Check(option=="\"C:\\Mod with spaces\\LMU-QuadView-Launcher.exe\" --steam %command%","Steam option invalid");
        var scratch=Path.Combine(Path.GetTempPath(),"lmu-steam-settings-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(scratch);
        try {
            bool failed=false;try {type.GetMethod("LoadArguments").Invoke(null,new object[]{scratch,new[]{"--steam",exe}});}catch(System.Reflection.TargetInvocationException){failed=true;}
            Check(failed,"Missing settings did not stop Steam launch");
            QvfrSettings.Save(Path.Combine(scratch,"launcher-settings.xml"),settings);
            Check((string)type.GetMethod("LoadArguments").Invoke(null,new object[]{scratch,new[]{"--steam",exe}})=="\"+XR\"","Saved Steam settings not loaded");
            File.WriteAllText(Path.Combine(scratch,"launcher-settings.xml"),"<invalid>");failed=false;
            try {type.GetMethod("LoadArguments").Invoke(null,new object[]{scratch,new[]{"--steam",exe}});}catch(System.Reflection.TargetInvocationException){failed=true;}
            Check(failed,"Malformed settings did not stop Steam launch");
        } finally {Directory.Delete(scratch,true);}
        Console.WriteLine("Steam launch tests passed: validation, XR and real Windows argument round trips");
        return 0;
    }
}
