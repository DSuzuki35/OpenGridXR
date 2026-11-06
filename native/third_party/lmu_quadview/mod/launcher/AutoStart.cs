using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Security.Principal;
using System.Security.AccessControl;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using Microsoft.Win32;
using Microsoft.Win32.SafeHandles;

namespace LmuQuadViewLauncher {
    public static class AutoStart {
        public const string RegistryPath=@"SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit";
        public const string UserPath=@"Software\LMUQuadView\AutoStart";
        const string ManifestName="XR_APILAYER_LMU_autostart.json";
        const string RegistryParent=@"SOFTWARE\Khronos\OpenXR\1\ApiLayers";
        const string Signer="89F7F00A4FD4BD66F5617F1202B8E7B9DD662247";
        const string LauncherName="LMU-QuadView-Launcher.exe";
        const string LauncherHashName="launcher.sha256";
        const string LauncherConfigHashName="launcher-config.sha256";
        static string InstallRoot {get{return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),"LMU QuadView Layer");}}
        public static bool Elevated {get{using(var identity=WindowsIdentity.GetCurrent())return new WindowsPrincipal(identity).IsInRole(WindowsBuiltInRole.Administrator);}}
        static void NoGame() {
            var games=Process.GetProcessesByName("Le Mans Ultimate");bool running=games.Length>0;
            foreach(var game in games)game.Dispose();
            if(running)throw new InvalidOperationException("Close LMU before installing or removing automatic startup.");
        }
        static bool Owned(string path) {
            try{return Path.GetFullPath(path).StartsWith(InstallRoot+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase)&&Path.GetFileName(path)==ManifestName;}
            catch(ArgumentException){return false;}catch(NotSupportedException){return false;}
        }
        public static string[] OrderedManifestNames(string[] names,string manifest) {
            var result=new List<string>{manifest};foreach(var name in names)if(!String.Equals(name,manifest,StringComparison.OrdinalIgnoreCase))result.Add(name);return result.ToArray();
        }
        static void Plain(string value) {if(value==null||value.IndexOfAny(new[]{'\r','\n','\0'})>=0)throw new InvalidDataException("Invalid automatic-start setting.");}
        internal static void SetUserActivationFile(string path,string root,bool enable) {
            if(!enable){if(File.Exists(path))File.Delete(path);return;}
            Plain(root);
            if(String.IsNullOrWhiteSpace(root)||!Path.IsPathRooted(root))throw new InvalidDataException("Automatic-start package path must be absolute.");
            root=Path.GetFullPath(root);Directory.CreateDirectory(Path.GetDirectoryName(path));
            string temporary=path+"."+Guid.NewGuid().ToString("N")+".tmp";
            try {
                File.WriteAllText(temporary,"[AutoStart]\r\nPackageRoot="+root+"\r\n",Encoding.Unicode);
                if(File.Exists(path))File.Replace(temporary,path,null);else File.Move(temporary,path);
            }finally{if(File.Exists(temporary))File.Delete(temporary);}
        }
        public static string BuildConfiguration(LauncherSettings settings,ProcessStartInfo info,string providerName,string library) {
            Plain(settings.Backend);Plain(providerName);Plain(library);
            var result=new StringBuilder("[Meta]\r\nBackend="+settings.Backend+"\r\nProviderName="+providerName+"\r\nProviderLibrary="+library+"\r\n[Environment]\r\n");
            foreach(string key in info.EnvironmentVariables.Keys)if(key.StartsWith("LMU_QV_",StringComparison.Ordinal)&&key!="LMU_QV_DISABLE_AUTOLOAD") {
                var value=info.EnvironmentVariables[key];Plain(value);
                if(!Regex.IsMatch(key,"^LMU_QV_[A-Z0-9_]+$"))throw new InvalidDataException("Invalid environment key.");
                result.Append(key).Append('=').Append(value).Append("\r\n");
            }
            if(result.Length>24000)throw new InvalidDataException("Automatic-start configuration is too large.");
            return result.ToString();
        }
        internal static LauncherSettings Settings(string root) {
            string path=Path.Combine(root,"launcher-settings.xml");
            if(!File.Exists(path))throw new FileNotFoundException("Save launcher settings before enabling automatic startup.",path);
            var settings=QvfrSettings.Load(path);QvfrSettings.Validate(settings);return settings;
        }
        // MainModule and .NET Framework's HasExited request stronger rights than
        // this validation needs. Protected game processes can deny those requests.
        [DllImport("kernel32.dll",SetLastError=true)]static extern SafeProcessHandle OpenProcess(uint access,bool inherit,int pid);
        [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern bool QueryFullProcessImageName(SafeProcessHandle process,uint flags,StringBuilder path,ref uint length);
        [DllImport("kernel32.dll",SetLastError=true)]static extern bool GetExitCodeProcess(SafeProcessHandle process,out uint exitCode);
        internal static bool IsProcessRunning(int pid) {
            using(var process=OpenProcess(0x1000,false,pid)) { // PROCESS_QUERY_LIMITED_INFORMATION
                if(process.IsInvalid){int error=Marshal.GetLastWin32Error();if(error==87)return false;throw new System.ComponentModel.Win32Exception(error);}
                uint code;if(!GetExitCodeProcess(process,out code))throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());return code==259;
            }
        }
        internal static void ValidateProcess(LauncherSettings settings,int pid) {
            using(var process=OpenProcess(0x1000,false,pid)) {
                if(process.IsInvalid)throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(),"Cannot query the LMU executable path.");
                uint code;if(!GetExitCodeProcess(process,out code))throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
                var path=new StringBuilder(32768);uint length=(uint)path.Capacity;
                if(!QueryFullProcessImageName(process,0,path,ref length))throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(),"Cannot query the LMU executable path.");
                string expected=Path.GetFullPath(Path.Combine(settings.GameDirectory,"Le Mans Ultimate.exe"));
                if(code!=259||!String.Equals(path.ToString(),expected,StringComparison.OrdinalIgnoreCase))
                    throw new InvalidOperationException("Automatic startup must belong to the configured LMU process.");
            }
        }
        public static void Prepare(string root,int pid,string nonce) {
            if(Elevated)throw new InvalidOperationException("Run LMU without administrator privileges for automatic startup.");
            if(!Regex.IsMatch(nonce,"^[a-fA-F0-9]{32}$"))throw new ArgumentException("Invalid automatic-start request.");
            var settings=Settings(root);ValidateProcess(settings,pid);ValidateRegistration(root);
            var info=LaunchService.Prepare(root,settings,null,pid);
            // The adapter is loaded from protected Program Files; reject a stale installation.
            var provider=LaunchService.ResolveProvider(settings);
            if(settings.Backend=="QVFR")QvfrSettings.SaveProfile(settings);
            LaunchService.MarkStarted(info,settings,pid,"implicit");
            var directory=Path.Combine(root,"runtime");Directory.CreateDirectory(directory);
            File.WriteAllText(Path.Combine(directory,"autoload-"+nonce+".ini"),BuildConfiguration(settings,info,
                settings.Backend=="Native"||settings.Backend=="VRS"?"":provider.Name,
                settings.Backend=="Native"||settings.Backend=="VRS"?"":provider.Library),Encoding.Unicode);
        }
        static string Hash(string path) {using(var file=File.OpenRead(path))using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(file)).Replace("-","").ToLowerInvariant();}
        internal static void CheckSigned(IEnumerable<string> paths) {
            var script=new StringBuilder("$ErrorActionPreference='Stop';");
            foreach(var path in paths)script.Append("$s=Get-AuthenticodeSignature -LiteralPath '").Append(path.Replace("'","''")).Append("';if($s.Status -ne 'Valid' -or $s.SignerCertificate.Thumbprint -ne '").Append(Signer).Append("' -or !$s.TimeStamperCertificate){exit 1};");
            script.Append("exit 0");
            var start=new ProcessStartInfo(Path.Combine(Environment.SystemDirectory,@"WindowsPowerShell\v1.0\powershell.exe"),"-NoProfile -NonInteractive -EncodedCommand "+Convert.ToBase64String(Encoding.Unicode.GetBytes(script.ToString()))) {UseShellExecute=false,CreateNoWindow=true};
            using(var process=Process.Start(start)) {
                if(!process.WaitForExit(90000))throw new InvalidOperationException("Signature verification timed out.");
                if(process.ExitCode!=0)throw new InvalidDataException("Installation requires valid, timestamped DLLs signed by Daniel Sosulski.");
            }
        }
        static void CheckProtectedPath(string path) {
            for(var current=new DirectoryInfo(path);current!=null;current=current.Parent)
                if(current.Exists&&(current.Attributes&FileAttributes.ReparsePoint)!=0)throw new IOException("The installation path must not contain a directory link.");
        }
        sealed class Value {public string Name;public object Data;public RegistryValueKind Kind;}
        static List<Value> Snapshot(RegistryKey key) {return key.GetValueNames().Select(n=>new Value{Name=n,Data=key.GetValue(n,null,RegistryValueOptions.DoNotExpandEnvironmentNames),Kind=key.GetValueKind(n)}).ToList();}
        static void Populate(RegistryKey key,List<Value> values) {
            foreach(var value in values)key.SetValue(value.Name,value.Data,value.Kind);
            key.Flush();
        }
        static bool EqualData(object left,object right) {
            var a=left as Array;var b=right as Array;
            return a!=null&&b!=null?a.Cast<object>().SequenceEqual(b.Cast<object>()):Object.Equals(left,right);
        }
        static bool EqualValues(List<Value> actual,List<Value> expected) {
            return actual.Count==expected.Count && actual.Select((v,i)=>
                String.Equals(v.Name,expected[i].Name,StringComparison.OrdinalIgnoreCase)&&
                v.Kind==expected[i].Kind&&EqualData(v.Data,expected[i].Data)).All(v=>v);
        }
        static string SecurityText(RegistrySecurity security) {
            var descriptor=new RawSecurityDescriptor(security.GetSecurityDescriptorBinaryForm(),0);
            // Creation can clear the AI bookkeeping bit even for a protected DACL.
            // Compare owner, group, protection and every ACE, not inheritance history.
            descriptor.SetFlags(descriptor.ControlFlags&~ControlFlags.DiscretionaryAclAutoInherited);
            return descriptor.GetSddlForm(AccessControlSections.Access|AccessControlSections.Owner|AccessControlSections.Group);
        }
        static void WriteKey(RegistryKey parent,string name,List<Value> values,RegistrySecurity security) {
            using(var key=parent.CreateSubKey(name,RegistryKeyPermissionCheck.ReadWriteSubTree,security))Populate(key,values);
            using(var key=parent.OpenSubKey(name))
                if(!EqualValues(Snapshot(key),values)||SecurityText(key.GetAccessControl())!=SecurityText(security))
                    throw new IOException("Registry values, order or permissions could not be preserved.");
        }
        static void DeleteEmptyKey(RegistryKey parent,string name) {
            using(var key=parent.OpenSubKey(name,true)){if(key==null)return;foreach(var value in key.GetValueNames())key.DeleteValue(value);}
            parent.DeleteSubKey(name);
        }
        internal static void RegisterFirst(RegistryKey parent,string keyName,string manifest,Action<RegistryKey> beforeVerify=null) {
            // A normal elevated token cannot assign SYSTEM as the owner of a
            // newly created backup. Restore privilege preserves the original
            // owner without taking ownership or weakening either key's DACL.
            using(var restore=RegistryRestoreScope.TryEnter())RegisterFirstCore(parent,keyName,manifest,beforeVerify);
        }
        static void RegisterFirstCore(RegistryKey parent,string keyName,string manifest,Action<RegistryKey> beforeVerify) {
            List<Value> original;RegistrySecurity security;
            using(var key=parent.CreateSubKey(keyName)) {
                if(key.SubKeyCount!=0)throw new IOException("Unexpected subkeys in OpenXR layer registration; no changes made.");
                original=Snapshot(key);security=key.GetAccessControl();
            }
            var retained=original.Where(v=>!Owned(v.Name)&&!String.Equals(v.Name,manifest,StringComparison.OrdinalIgnoreCase)).ToList();
            var desired=new List<Value>{new Value{Name=manifest,Data=0,Kind=RegistryValueKind.DWord}};desired.AddRange(retained);
            if(EqualValues(original,desired))return;
            string backup=keyName+"-LMU-Previous-"+Guid.NewGuid().ToString("N");
            // Never use RegRenameKey/NtRenameKey: the diagnosed kernel crash occurs
            // in that path. Save and verify a persistent copy before replacement.
            try{WriteKey(parent,backup,original,security);parent.Flush();}
            catch(Exception error) {
                throw new IOException("Cannot create a verified registry backup with the original permissions. The original layer list was not replaced. Windows reported: "+error.Message+" Use the Steam launch wrapper if system policy prevents registration. Backup location: "+parent.Name+"\\"+backup+".",error);
            }
            bool removed=false;
            try {
                using(var key=parent.OpenSubKey(keyName))
                    if(key==null||key.SubKeyCount!=0||!EqualValues(Snapshot(key),original)||SecurityText(key.GetAccessControl())!=SecurityText(security))
                        throw new IOException("OpenXR registration changed during installation; retry when other installers are closed.");
                // Non-recursive deletion refuses unexpected subkeys. Recreating a
                // fresh value list avoids the stale enumeration slots left by
                // deleting/reinserting values in an existing key.
                parent.DeleteSubKey(keyName);removed=true;
                WriteKey(parent,keyName,desired,security);
                using(var key=parent.OpenSubKey(keyName,true)) {
                    if(beforeVerify!=null)beforeVerify(key);var actual=Snapshot(key);
                    if(!EqualValues(actual,desired)||SecurityText(key.GetAccessControl())!=SecurityText(security))
                        throw new IOException("Windows did not preserve the required OpenXR layer order, values and permissions.");
                }
                parent.Flush();
            }catch(Exception updateError) {
                string conflict=null;
                if(removed)try {
                    using(var current=parent.OpenSubKey(keyName))if(current!=null) {
                        if(current.SubKeyCount!=0)throw new IOException("Unexpected subkeys appeared; preserving them and the original backup.");
                        var currentValues=Snapshot(current);var currentSecurity=current.GetAccessControl();
                        if(!EqualValues(currentValues,desired)||SecurityText(currentSecurity)!=SecurityText(security)) {
                            conflict=keyName+"-LMU-Conflict-"+Guid.NewGuid().ToString("N");
                            WriteKey(parent,conflict,currentValues,currentSecurity);parent.Flush();
                        }
                    }
                    parent.DeleteSubKey(keyName,false);
                    WriteKey(parent,keyName,original,security);parent.Flush();
                }catch(Exception restoreError) {
                    throw new IOException("OpenXR registration could not be restored. Its original values remain in "+parent.Name+"\\"+backup+".",new AggregateException(updateError,restoreError));
                }
                DeleteEmptyKey(parent,backup);
                if(conflict!=null)throw new IOException("OpenXR registration was restored. Conflicting changes were preserved in "+parent.Name+"\\"+conflict+". Close other OpenXR installers before retrying.",updateError);
                throw;
            }
            DeleteEmptyKey(parent,backup);
        }
        public static void Install(string root) {
            if(!Elevated)throw new UnauthorizedAccessException("Automatic OpenXR installation needs administrator rights.");
            WithRegistrationLock(delegate{InstallCore(root);});
        }
        static void WithRegistrationLock(Action action) {
            using(var mutex=new Mutex(false,@"Global\LMUQuadView-Registration")) {
                bool held=false;
                try {
                    try{held=mutex.WaitOne(30000);}catch(AbandonedMutexException){held=true;}
                    if(!held)throw new IOException("Another LMU QuadView installation is still running.");
                    action();
                }finally{if(held)mutex.ReleaseMutex();}
            }
        }
        static void InstallCore(string root) {
            NoGame();
            var files=new[]{Path.Combine(root,@"build\lmu_autostart.dll"),Path.Combine(root,@"build\lmu_quadview.dll")};CheckSigned(files);
            var launcher=Path.Combine(root,LauncherName);
            var launcherHash=Hash(launcher);var configHash=Hash(launcher+".config");
            var version=Hash(files[0]).Substring(0,16)+"-"+Hash(files[1]).Substring(0,16)+"-"+launcherHash.Substring(0,16)+"-"+configHash.Substring(0,16);
            var directory=Path.Combine(InstallRoot,version);CheckProtectedPath(directory);Directory.CreateDirectory(directory);
            for(int i=0;i<files.Length;i++) {
                var target=Path.Combine(directory,Path.GetFileName(files[i]));
                if(!File.Exists(target))File.Copy(files[i],target);
                if(Hash(files[i])!=Hash(target))throw new IOException("Installed DLL differs from the signed package.");
            }
            CheckSigned(files.Select(f=>Path.Combine(directory,Path.GetFileName(f))));
            // 0.3.3: approve exactly this package launcher. The protected layer refuses
            // to start a different user-writable launcher until automatic startup is reinstalled.
            File.WriteAllText(Path.Combine(directory,LauncherHashName),launcherHash+"\r\n",Encoding.ASCII);
            File.WriteAllText(Path.Combine(directory,LauncherConfigHashName),configHash+"\r\n",Encoding.ASCII);
            var manifest=Path.Combine(directory,ManifestName);
            File.WriteAllText(manifest,"{\"file_format_version\":\"1.0.0\",\"api_layer\":{\"name\":\"XR_APILAYER_LMU_autostart\",\"library_path\":\".\\\\lmu_autostart.dll\",\"api_version\":\"1.0\",\"implementation_version\":\"1\",\"description\":\"LMU Quad View automatic startup\",\"disable_environment\":\"LMU_QV_DISABLE_AUTOLOAD\"}}",new UTF8Encoding(false));
            using(var machine=RegistryKey.OpenBaseKey(RegistryHive.LocalMachine,RegistryView.Registry64))
            using(var parent=machine.CreateSubKey(RegistryParent)) {
                List<Value> original;using(var key=parent.CreateSubKey("Implicit"))original=Snapshot(key);
                // Keep a recoverable backup before recreating the childless layer key.
                using(var backup=machine.CreateSubKey(@"SOFTWARE\LMUQuadView\LayerOrderBackup")) {
                    foreach(var name in backup.GetValueNames())backup.DeleteValue(name);
                    foreach(var value in original)backup.SetValue(value.Name,value.Data,value.Kind);
                    backup.Flush();
                }
                RegisterFirst(parent,"Implicit",manifest);
            }
        }
        public static void Remove() {
            if(!Elevated)throw new UnauthorizedAccessException("Removing automatic OpenXR startup needs administrator rights.");
            WithRegistrationLock(RemoveCore);
        }
        static void RemoveCore() {
            NoGame();
            using(var machine=RegistryKey.OpenBaseKey(RegistryHive.LocalMachine,RegistryView.Registry64)) {
                using(var key=machine.OpenSubKey(RegistryPath,true))if(key!=null)foreach(var name in key.GetValueNames())if(Owned(name))key.DeleteValue(name);
                // 0.3.3: the pre-install layer-order backup is only needed while our layer is registered.
                RemoveLayerOrderBackup(machine,@"SOFTWARE\LMUQuadView");
            }
            // Old protected DLLs remain so an already-loaded foreign process can finish safely.
        }
        internal static void RemoveLayerOrderBackup(RegistryKey hive,string productPath) {
            bool empty=false;
            using(var product=hive.OpenSubKey(productPath,true))if(product!=null) {
                // Never recursively remove unexpected subkeys or unrelated data.
                using(var backup=product.OpenSubKey("LayerOrderBackup"))if(backup!=null&&backup.SubKeyCount!=0)return;
                product.DeleteSubKey("LayerOrderBackup",false);
                empty=product.SubKeyCount==0&&product.ValueCount==0;
            }
            if(empty)hive.DeleteSubKey(productPath,false);
        }
        public static void Enable(string root,bool enable) {
            NoGame();
            if(enable){var settings=Settings(root);LaunchService.ValidateExecutable(Path.Combine(settings.GameDirectory,"Le Mans Ultimate.exe"));}
            var start=new ProcessStartInfo(Path.Combine(root,"LMU-QuadView-Launcher.exe"),enable?"--install-autoload":"--uninstall-autoload") {UseShellExecute=true,Verb="runas",WorkingDirectory=root};
            using(var process=Process.Start(start)){process.WaitForExit();if(process.ExitCode!=0)throw new InvalidOperationException("Automatic-start installation was not completed.");}
            using(var key=Registry.CurrentUser.CreateSubKey(UserPath)) {
                if(enable)key.SetValue("PackageRoot",Path.GetFullPath(root),RegistryValueKind.String);else key.DeleteValue("PackageRoot",false);
            }
            if(enable)ValidateRegistration(root);
            SetUserActivationFile(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),@"LMUQuadView\autoload.ini"),root,enable);
        }
        internal static void ValidateRegistration(string root) {
            using(var machine=RegistryKey.OpenBaseKey(RegistryHive.LocalMachine,RegistryView.Registry64))
            using(var key=machine.OpenSubKey(RegistryPath)) {
                if(key==null)throw new InvalidOperationException("Install automatic startup from Settings first.");
                // The loader reads HKLM before HKCU, in registry enumeration order.
                var first=key.GetValueNames().FirstOrDefault(n=>key.GetValue(n) is int&&(int)key.GetValue(n)==0);
                if(first==null||!Owned(first))throw new InvalidOperationException("Reinstall automatic startup from Settings to restore the required layer order.");
                foreach(var name in new[]{"lmu_autostart.dll","lmu_quadview.dll"})
                    if(Hash(Path.Combine(Path.GetDirectoryName(first),name))!=Hash(Path.Combine(root,"build",name)))
                        throw new InvalidOperationException("The installed layer belongs to a different package. Reinstall automatic startup from this launcher.");
                foreach(var pair in new[]{new[]{LauncherHashName,LauncherName},new[]{LauncherConfigHashName,LauncherName+".config"}}) {
                    var approved=Path.Combine(Path.GetDirectoryName(first),pair[0]);
                    if(!File.Exists(approved)||File.ReadAllText(approved).Trim()!=Hash(Path.Combine(root,pair[1])))
                        throw new InvalidOperationException("The launcher or its configuration changed since automatic startup was installed. Reinstall automatic startup from this launcher.");
                }
            }
        }
    }
}
