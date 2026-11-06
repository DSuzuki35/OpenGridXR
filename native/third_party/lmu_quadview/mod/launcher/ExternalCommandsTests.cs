using System;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Threading;
using LmuQuadViewLauncher;

class ExternalCommandsTests {
    static void Require(bool good,string message) { if(!good)throw new Exception(message); }
    static void Main() {
        string prefix="Local\\LMUQuadView.Tests."+Guid.NewGuid().ToString("N");
        string overlay=prefix+".Overlay", recording=prefix+".Recording";
        int overlays=0, recordings=0;
        using(var commands=new ExternalCommands(overlay,recording)) {
            Require(commands.Available,"command channels unavailable");
            using(var sender=EventWaitHandle.OpenExisting(overlay,EventWaitHandleRights.Modify|EventWaitHandleRights.ReadPermissions)) {
                sender.Set();
                commands.Poll(delegate{overlays++;},delegate{recordings++;});
                commands.Poll(delegate{overlays++;},delegate{recordings++;});
                Require(overlays==1&&recordings==0,"overlay signal was lost, repeated, or misrouted");
                var security=sender.GetAccessControl();
                Require(security.AreAccessRulesProtected,"command ACL inherits wider permissions");
                var owner=WindowsIdentity.GetCurrent().User;
                var system=new SecurityIdentifier(WellKnownSidType.LocalSystemSid,null);
                foreach(EventWaitHandleAccessRule rule in security.GetAccessRules(true,true,typeof(SecurityIdentifier)))
                    Require(rule.AccessControlType!=AccessControlType.Allow||rule.IdentityReference.Equals(owner)||rule.IdentityReference.Equals(system),"command ACL permits another principal");
            }
            using(var sender=EventWaitHandle.OpenExisting(recording,EventWaitHandleRights.Modify))sender.Set();
            commands.Poll(delegate{overlays++;},delegate{recordings++;});
            Require(overlays==1&&recordings==1,"recording signal misrouted");
        }
        // An existing manual-reset event must never become a repeated toggle source.
        using(var foreign=new EventWaitHandle(true,EventResetMode.ManualReset,overlay))
        using(var commands=new ExternalCommands(overlay,recording)) {
            Require(!commands.Available,"pre-created command channel accepted");
            commands.Poll(delegate{overlays++;},delegate{recordings++;});
            Require(overlays==1&&recordings==1,"foreign signal consumed");
        }
        Console.WriteLine("External command tests passed: routing, single consumption, owner-only ACL, pre-created channel rejection.");
    }
}
