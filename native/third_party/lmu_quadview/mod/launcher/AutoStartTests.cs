using System;
using System.IO;
using System.Linq;
using System.Security.AccessControl;
using System.Security.Principal;
using Microsoft.Win32;
using LmuQuadViewLauncher;
class AutoStartTests {
    static void Check(bool b,string m){if(!b)throw new Exception(m);}
    static void SameSecurity(RegistrySecurity actual,RegistrySecurity expected) {
        Check(actual.GetOwner(typeof(SecurityIdentifier)).Equals(expected.GetOwner(typeof(SecurityIdentifier)))&&
            actual.GetGroup(typeof(SecurityIdentifier)).Equals(expected.GetGroup(typeof(SecurityIdentifier)))&&
            actual.AreAccessRulesProtected==expected.AreAccessRulesProtected,"Registry ownership/protection changed");
        var a=actual.GetAccessRules(true,true,typeof(SecurityIdentifier));var e=expected.GetAccessRules(true,true,typeof(SecurityIdentifier));
        Check(a.Count==e.Count,"Registry access rule count changed");
        for(int i=0;i<a.Count;i++) {
            var x=(RegistryAccessRule)a[i];var y=(RegistryAccessRule)e[i];
            Check(x.IdentityReference.Equals(y.IdentityReference)&&x.RegistryRights==y.RegistryRights&&x.AccessControlType==y.AccessControlType&&
                x.InheritanceFlags==y.InheritanceFlags&&x.PropagationFlags==y.PropagationFlags&&x.IsInherited==y.IsInherited,"Registry access rule changed");
        }
    }
    static int Main(){
        string activation=Path.Combine(Path.GetTempPath(),"lmu-activation-"+Guid.NewGuid().ToString("N")+".ini");
        try {
            string root=@"C:\User Data\Grüße\LMU QuadView";
            AutoStart.SetUserActivationFile(activation,root,true);
            Check(File.Exists(activation)&&File.ReadAllText(activation).Contains("PackageRoot="+root),"User activation file not written");
            AutoStart.SetUserActivationFile(activation,@"C:\Other package",true);
            Check(!File.ReadAllText(activation).Contains(root)&&File.ReadAllText(activation).Contains(@"C:\Other package"),"Activation update not replaced");
            bool invalid=false;try{AutoStart.SetUserActivationFile(activation,"C:\\bad\r\nInjected=1",true);}catch(InvalidDataException){invalid=true;}
            Check(invalid,"Activation permits extra INI keys");
            AutoStart.SetUserActivationFile(activation,"",false);Check(!File.Exists(activation),"Disable leaves file activation active");
        }finally{if(File.Exists(activation))File.Delete(activation);}
        var type=typeof(LaunchService).Assembly.GetType("LmuQuadViewLauncher.AutoStart");
        Check(type!=null,"Automatic OpenXR startup is missing");
        var method=type.GetMethod("BuildConfiguration");
        var s=new LauncherSettings {Backend="QVFR"};
        var info=new System.Diagnostics.ProcessStartInfo {UseShellExecute=false};
        info.EnvironmentVariables["LMU_QV_MODE"]="quad";
        info.EnvironmentVariables["LMU_QV_CPU_AFFINITY"]="x3d";
        info.EnvironmentVariables["LMU_QV_DISABLE_AUTOLOAD"]="1";
        info.EnvironmentVariables["LMU_QV_LOG"]=@"C:\my package\logs\test.log";
        info.EnvironmentVariables["SECRET_TEST_IGNORE"]="not for output";
        var text=(string)method.Invoke(null,new object[]{s,info,"XR_APILAYER_MBUCCHIA_quad_views_foveated",@"C:\provider\layer.dll"});
        Check(text.Contains("Backend=QVFR")&&text.Contains("LMU_QV_MODE=quad"),"Settings not exported");
        Check(text.Contains("LMU_QV_CPU_AFFINITY=x3d"),"CPU preference omitted from Steam autostart");
        info.EnvironmentVariables["LMU_QV_CPU_AFFINITY"]="off";
        var withoutAffinity=(string)method.Invoke(null,new object[]{s,info,"name","library"});
        Check(withoutAffinity.Contains("LMU_QV_CPU_AFFINITY=off"),"CPU opt-out omitted from Steam autostart");
        Check(!text.Contains("SECRET_TEST_IGNORE"),"Unrelated environment leaked");
        Check(!text.Contains("LMU_QV_DISABLE_AUTOLOAD"),"Automatic startup disables itself during loader retries");
        info.EnvironmentVariables["LMU_QV_LOG"]="x\r\nInjected=1";
        bool failed=false;try{method.Invoke(null,new object[]{s,info,"name","library"});}catch(System.Reflection.TargetInvocationException){failed=true;}
        Check(failed,"Multiline response value accepted");
        var order=type.GetMethod("OrderedManifestNames");
        var result=(string[])order.Invoke(null,new object[]{new[]{"qvfr.json","our.json","other.json"},"our.json"});
        Check(String.Join(";",result)=="our.json;qvfr.json;other.json","Third-party order changed");
        string testKey=@"Software\LMUQuadView\Tests-"+Guid.NewGuid().ToString("N");
        try {
            string product=testKey+@"\Product";
            using(var backup=Registry.CurrentUser.CreateSubKey(product+@"\LayerOrderBackup"))backup.SetValue("original.json",0);
            AutoStart.RemoveLayerOrderBackup(Registry.CurrentUser,product);
            using(var removed=Registry.CurrentUser.OpenSubKey(product))Check(removed==null,"Empty product key retained");
            using(var key=Registry.CurrentUser.CreateSubKey(product))key.SetValue("foreign",1);
            using(var key=Registry.CurrentUser.CreateSubKey(product+@"\Other"))key.SetValue("foreign",2);
            using(var key=Registry.CurrentUser.CreateSubKey(product+@"\LayerOrderBackup\Unexpected"))key.SetValue("foreign",3);
            AutoStart.RemoveLayerOrderBackup(Registry.CurrentUser,product);
            using(var key=Registry.CurrentUser.OpenSubKey(product+@"\LayerOrderBackup\Unexpected"))Check(key!=null&&(int)key.GetValue("foreign")==3,"Unexpected backup subtree removed");
            using(var key=Registry.CurrentUser.OpenSubKey(product+@"\LayerOrderBackup",true))key.DeleteSubKey("Unexpected");
            AutoStart.RemoveLayerOrderBackup(Registry.CurrentUser,product);
            using(var key=Registry.CurrentUser.OpenSubKey(product))Check((int)key.GetValue("foreign")==1&&key.GetSubKeyNames().SequenceEqual(new[]{"Other"}),"Unrelated registry data removed");
            using(var key=Registry.CurrentUser.OpenSubKey(product,true)){key.DeleteSubKey("Other");key.DeleteValue("foreign");}
            AutoStart.RemoveLayerOrderBackup(Registry.CurrentUser,product);
            using(var parent=Registry.CurrentUser.CreateSubKey(testKey)) {
              RegistrySecurity security;
              using(var key=parent.CreateSubKey("Implicit")) {
                key.SetValue("qvfr.json",0,RegistryValueKind.DWord);key.SetValue("disabled.json",1,RegistryValueKind.DWord);
                key.SetValue("expand",@"%USERPROFILE%\some path",RegistryValueKind.ExpandString);
                key.SetValue("binary",new byte[]{1,3,7},RegistryValueKind.Binary);
                key.SetValue("none",new byte[]{9,8},RegistryValueKind.None);
                key.SetValue("multi",new[]{"alpha","beta"},RegistryValueKind.MultiString);
                key.SetValue("long",0x123456789L,RegistryValueKind.QWord);
                key.SetValue("", "default value",RegistryValueKind.String);
                var acl=key.GetAccessControl();acl.SetAccessRuleProtection(true,true);key.SetAccessControl(acl);
                security=key.GetAccessControl();
              }
              AutoStart.RegisterFirst(parent,"Implicit","our.json");
              AutoStart.RegisterFirst(parent,"Implicit","our.json"); // Idempotent update must preserve foreign values.
              using(var key=parent.OpenSubKey("Implicit")) {
                Check(String.Join(";",key.GetValueNames())=="our.json;qvfr.json;disabled.json;expand;binary;none;multi;long;","Real registry order incorrect");
                Check((int)key.GetValue("disabled.json")==1,"Disabled layer enabled");
                Check(key.GetValueKind("expand")==RegistryValueKind.ExpandString&&(string)key.GetValue("expand",null,RegistryValueOptions.DoNotExpandEnvironmentNames)==@"%USERPROFILE%\some path","Expandable data changed");
                Check(String.Join(";",(string[])key.GetValue("multi"))=="alpha;beta"&&(long)key.GetValue("long")==0x123456789L&&(string)key.GetValue("")=="default value","Registry value types changed");
                Check(key.GetValueKind("none")==RegistryValueKind.None&&((byte[])key.GetValue("none"))[0]==9,"Untyped registry data changed");
                SameSecurity(key.GetAccessControl(),security);
              }
                bool rollback=false;
                try{AutoStart.RegisterFirst(parent,"Implicit","new.json",delegate(RegistryKey changed) {changed.SetValue("qvfr.json",9);});}catch(IOException){rollback=true;}
              string conflict=parent.GetSubKeyNames().SingleOrDefault(n=>n.StartsWith("Implicit-LMU-Conflict-",StringComparison.Ordinal));
              Check(conflict!=null,"Conflicting third-party changes lost during rollback");
              using(var saved=parent.OpenSubKey(conflict))Check((int)saved.GetValue("qvfr.json")==9,"Conflicting value not preserved");
              parent.DeleteSubKey(conflict);
              using(var key=parent.OpenSubKey("Implicit",true)) {
                Check(rollback&&key.GetValue("new.json")==null&&(int)key.GetValue("qvfr.json")==0&&((byte[])key.GetValue("binary"))[2]==7,"Failed registration did not restore original values");
                Check(key.GetValueNames()[0]=="our.json","Rollback did not restore order");
                SameSecurity(key.GetAccessControl(),security);
                using(var child=key.CreateSubKey("foreign-child"))child.SetValue("keep","untouched");
              }
              bool refused=false;try{AutoStart.RegisterFirst(parent,"Implicit","new.json");}catch(IOException){refused=true;}
              using(var key=parent.OpenSubKey("Implicit",true)) {
                using(var child=key.OpenSubKey("foreign-child"))Check(refused&&(string)child.GetValue("keep")=="untouched"&&key.GetValue("new.json")==null,"Unexpected subkey was modified");
                key.DeleteSubKey("foreign-child");
                foreach(var name in key.GetValueNames())key.DeleteValue(name);
              }
              parent.DeleteSubKey("Implicit");Check(parent.SubKeyCount==0,"Staging/rollback keys were left behind");
              // Default inherited permissions and a failed rollback must retain
              // the original backup without deleting unexpected descendants.
              RegistrySecurity inherited;
              using(var key=parent.CreateSubKey("Implicit")){key.SetValue("original",17);inherited=key.GetAccessControl();}
              AutoStart.RegisterFirst(parent,"Implicit","our.json");
              using(var key=parent.OpenSubKey("Implicit"))SameSecurity(key.GetAccessControl(),inherited);
              string failure="";
              try{AutoStart.RegisterFirst(parent,"Implicit","new.json",delegate(RegistryKey changed){using(var child=changed.CreateSubKey("foreign-child"))child.SetValue("keep",42);throw new IOException("Injected failure with an unexpected child");});}
              catch(IOException e){failure=e.Message;}
              var backups=parent.GetSubKeyNames().Where(n=>n.StartsWith("Implicit-LMU-Previous-",StringComparison.Ordinal)).ToArray();
              Check(backups.Length==1&&failure.Contains(backups[0]),"Failed restoration did not retain/report its backup");
              using(var saved=parent.OpenSubKey(backups[0]))Check((int)saved.GetValue("original")==17&&saved.GetValue("new.json")==null,"Original backup was changed");
              using(var child=parent.OpenSubKey(@"Implicit\foreign-child"))Check((int)child.GetValue("keep")==42,"Rollback deleted unexpected child");
            }
        }finally{Registry.CurrentUser.DeleteSubKeyTree(testKey,false);}
        Console.WriteLine("Autostart settings, real registry order, value preservation and failure rollback tests passed");return 0;
    }
}
