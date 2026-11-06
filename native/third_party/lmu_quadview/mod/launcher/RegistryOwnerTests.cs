using System;
using System.IO;
using System.Linq;
using System.Security.AccessControl;
using System.Security.Principal;
using Microsoft.Win32;
using LmuQuadViewLauncher;

// Explicit elevated fixture. Only a GUID-named test tree is touched.
class RegistryOwnerTests {
    static void Check(bool b,string m){if(!b)throw new Exception(m);}
    static string Security(RegistryKey key){return key.GetAccessControl().GetSecurityDescriptorSddlForm(AccessControlSections.Owner|AccessControlSections.Group|AccessControlSections.Access);}
    static string ThreadIdentity(){using(var identity=WindowsIdentity.GetCurrent(true))return identity==null?"none":identity.User.Value+":"+identity.ImpersonationLevel;}
    static int Main(string[] args) {
        if(args.Length!=1)throw new ArgumentException("Expected output log path");
        using(var log=new StreamWriter(args[0])) {
            try {Run(log);return 0;}catch(Exception e){log.WriteLine(e);return 1;}
        }
    }
    static void Run(TextWriter log) {
        Check(AutoStart.Elevated,"Run this fixture elevated");
        string path=@"Software\LMUQuadView\OwnerTest-"+Guid.NewGuid().ToString("N");
        string identityBefore=ThreadIdentity();
        try {
            using(var parent=Registry.CurrentUser.CreateSubKey(path)) {
                using(var scope=RegistryRestoreScope.TryEnter()) {
                    Check(scope!=null,"Restore privilege unavailable in elevated fixture");
                    var acl=new RegistrySecurity();acl.SetAccessRuleProtection(true,false);
                    acl.SetOwner(new SecurityIdentifier(WellKnownSidType.LocalSystemSid,null));
                    acl.SetGroup(new SecurityIdentifier(WellKnownSidType.LocalSystemSid,null));
                    foreach(var sid in new[]{WellKnownSidType.LocalSystemSid,WellKnownSidType.BuiltinAdministratorsSid})
                        acl.AddAccessRule(new RegistryAccessRule(new SecurityIdentifier(sid,null),RegistryRights.FullControl,InheritanceFlags.ContainerInherit,PropagationFlags.None,AccessControlType.Allow));
                    acl.AddAccessRule(new RegistryAccessRule(new SecurityIdentifier(WellKnownSidType.BuiltinUsersSid,null),RegistryRights.ReadKey,InheritanceFlags.ContainerInherit,PropagationFlags.None,AccessControlType.Allow));
                    parent.SetAccessControl(acl);
                    using(var key=parent.CreateSubKey("Implicit",RegistryKeyPermissionCheck.ReadWriteSubTree,acl)) {
                        key.SetValue("pimax.json",0);key.SetValue("disabled.json",1);key.SetValue("binary",new byte[]{1,2,3});
                    }
                }
                Check(ThreadIdentity()==identityBefore,"Fixture leaked impersonation");
                string parentSecurity=Security(parent),keySecurity;
                using(var key=parent.OpenSubKey("Implicit"))keySecurity=Security(key);
                AutoStart.RegisterFirst(parent,"Implicit","our.json");
                Check(ThreadIdentity()==identityBefore,"Registration leaked impersonation");
                using(var key=parent.OpenSubKey("Implicit")) {
                    Check(String.Join(";",key.GetValueNames())=="our.json;pimax.json;disabled.json;binary","Wrong layer order");
                    Check(Security(key)==keySecurity,"SYSTEM ownership or original DACL changed");
                    Check((int)key.GetValue("disabled.json")==1&&((byte[])key.GetValue("binary"))[2]==3,"Original values changed");
                }
                bool failed=false;
                try{AutoStart.RegisterFirst(parent,"Implicit","new.json",delegate(RegistryKey key){throw new IOException("Injected rollback");});}catch(IOException){failed=true;}
                Check(failed&&ThreadIdentity()==identityBefore,"Rollback did not unwind token");
                using(var key=parent.OpenSubKey("Implicit"))Check(Security(key)==keySecurity&&key.GetValue("new.json")==null&&key.GetValueNames()[0]=="our.json","Rollback changed owner, values or order");
                Check(Security(parent)==parentSecurity&&parent.GetSubKeyNames().SequenceEqual(new[]{"Implicit"}),"Parent ACL changed or backup leaked");
                log.WriteLine("SYSTEM-owned key install and rollback passed; parent/key permissions and thread identity preserved.");
            }
        }finally{Registry.CurrentUser.DeleteSubKeyTree(path,false);}
    }
}
