using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.AccessControl;
using System.Security.Principal;
using LmuQuadViewLauncher;

class ProcessPathTests {
    [DllImport("advapi32.dll",SetLastError=true)]static extern bool SetKernelObjectSecurity(IntPtr handle,uint information,byte[] descriptor);
    static void Check(bool valid,string message){if(!valid)throw new Exception(message);}
    static int Main(string[] args) {
        if(args.Length==1&&args[0]=="--limited-query-child") {
            // Restrict only this disposable test child: forbid module enumeration,
            // retain the documented limited process-information access.
            var sid=WindowsIdentity.GetCurrent().User.Value;
            var acl=new RawSecurityDescriptor("D:(D;;0x0410;;;WD)(A;;GA;;;"+sid+")");
            var bytes=new byte[acl.BinaryLength];acl.GetBinaryForm(bytes,0);
            if(!SetKernelObjectSecurity(new IntPtr(-1),4,bytes))throw new Win32Exception(Marshal.GetLastWin32Error());
            Console.WriteLine("ready");Console.ReadLine();return 0;
        }
        var root=Path.Combine(Path.GetTempPath(),"lmu-process-path-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);
        var childExe=Path.Combine(root,"Le Mans Ultimate.exe");
        File.Copy(Assembly.GetExecutingAssembly().Location,childExe);
        // The regression can also be compiled against the previous launcher DLL.
        var launcher=typeof(LaunchService).Assembly.Location;
        if(launcher!=Assembly.GetExecutingAssembly().Location)File.Copy(launcher,Path.Combine(root,Path.GetFileName(launcher)));
        var validate=typeof(LaunchService).Assembly.GetType("LmuQuadViewLauncher.AutoStart").GetMethod("ValidateProcess",BindingFlags.NonPublic|BindingFlags.Static);
        var running=typeof(LaunchService).Assembly.GetType("LmuQuadViewLauncher.AutoStart").GetMethod("IsProcessRunning",BindingFlags.NonPublic|BindingFlags.Static);
        try {
            using(var child=Process.Start(new ProcessStartInfo(childExe,"--limited-query-child"){UseShellExecute=false,CreateNoWindow=true,RedirectStandardInput=true,RedirectStandardOutput=true})) {
                try {
                    Check(child.StandardOutput.ReadLine()=="ready","Restricted test process did not initialize");
                    bool denied=false;
                    using(var fresh=Process.GetProcessById(child.Id))try{var unused=fresh.MainModule.FileName;}catch(Win32Exception e){denied=e.NativeErrorCode==5;}
                    Check(denied,"Regression fixture must deny the old module-enumeration access");
                    validate.Invoke(null,new object[]{new LauncherSettings{GameDirectory=root},child.Id});
                    Check((bool)running.Invoke(null,new object[]{child.Id}),"Running protected process not recognized");
                    bool mismatch=false;
                    try{validate.Invoke(null,new object[]{new LauncherSettings{GameDirectory=Path.Combine(root,"wrong")},child.Id});}
                    catch(TargetInvocationException e){mismatch=e.InnerException is InvalidOperationException;}
                    Check(mismatch,"Wrong executable path was accepted");
                }finally{child.StandardInput.WriteLine();Check(child.WaitForExit(5000),"Test child failed to exit");}
                Check(!(bool)running.Invoke(null,new object[]{child.Id}),"Exited process reported as running");
            }
            Console.WriteLine("Limited-rights process-path regression passed: module enumeration denied, exact path accepted, mismatched path rejected");return 0;
        }finally{
            File.Delete(childExe);
            if(launcher!=Assembly.GetExecutingAssembly().Location)File.Delete(Path.Combine(root,Path.GetFileName(launcher)));
            Directory.Delete(root);
        }
    }
}
