using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace LmuQuadViewLauncher {
    public static class SteamLaunch {
        // Windows CommandLineToArgvW/CRT escaping. The game is started directly,
        // never via cmd.exe, so shell metacharacters remain literal arguments.
        public static string QuoteArgument(string value) {
            if(value==null)throw new ArgumentNullException("value");
            var result=new StringBuilder("\"");int slashes=0;
            foreach(char c in value) {
                if(c=='\\'){slashes++;continue;}
                if(c=='\"')result.Append('\\',slashes*2+1);
                else result.Append('\\',slashes);
                result.Append(c);slashes=0;
            }
            return result.Append('\\',slashes*2).Append('"').ToString();
        }
        public static string LaunchOptions(string launcherPath) {
            return QuoteArgument(Path.GetFullPath(launcherPath))+" --steam %command%";
        }
        public static string GameArguments(string[] args,LauncherSettings settings) {
            if(args==null||args.Length<2||args[0]!="--steam")
                throw new ArgumentException("Use the Steam launch option from Settings: launcher --steam %command%.");
            var expected=Path.GetFullPath(Path.Combine(settings.GameDirectory,"Le Mans Ultimate.exe"));
            if(!Path.IsPathRooted(args[1])||!String.Equals(Path.GetFullPath(args[1]),expected,StringComparison.OrdinalIgnoreCase))
                throw new ArgumentException("Steam did not pass the configured Le Mans Ultimate.exe. Check the game folder in Settings and keep %command% in Steam's launch options.");
            var quoted=new List<string>();bool xr=false;
            for(int i=2;i<args.Length;i++) {
                if(String.Equals(args[i],"+XR",StringComparison.OrdinalIgnoreCase))xr=true;
                quoted.Add(QuoteArgument(args[i]));
            }
            if(!xr)quoted.Insert(0,QuoteArgument("+XR"));
            return String.Join(" ",quoted);
        }
        public static string LoadArguments(string root,string[] args) {
            var path=Path.Combine(root,"launcher-settings.xml");
            if(!File.Exists(path))throw new ArgumentException("Open the launcher, choose your game folder and provider, then save settings before starting through Steam.");
            var settings=QvfrSettings.Load(path);QvfrSettings.Validate(settings);
            return GameArguments(args,settings);
        }
    }
}
