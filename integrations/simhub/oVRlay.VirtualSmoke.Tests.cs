using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Drawing;
using System.Threading;
using SimHub.Plugins.Devices;
using SimHub.Plugins.OutputPlugins.GraphicalDash.PSE;
using SimHub.Plugins.OutputPlugins.GraphicalDash.LedModules;
class VirtualSmokeTests
{
    static void Check(bool condition,string message) {if(!condition) throw new Exception(message);}
    [STAThread]
    static int Main(string[] args)
    {
        AppDomain.CurrentDomain.AssemblyResolve += delegate(object sender, ResolveEventArgs e) {
            string path=Path.Combine(args[1],new AssemblyName(e.Name).Name+".dll");
            return File.Exists(path)?Assembly.LoadFrom(path):null;
        };
        try {Run(Assembly.LoadFrom(args[0])); Console.WriteLine("PASS: six hardware-free identities, physical mapping/brightness/overrides, independent modes, disable/reconnect/dispose, provider recovery, nonblocking overlap."); return 0;}
        catch(Exception e) {Console.Error.WriteLine("FAIL: "+e);return 1;}
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    static void Run(Assembly assembly)
    {
        var registryType=assembly.GetType("oVRlay.SimHubLeds.VirtualWheelRegistry");
        Check(registryType!=null,"Six hardware-free virtual wheel identities must be registered.");
        var descriptors=((IDeviceDescriptorsRegistry)Activator.CreateInstance(registryType)).GetDevices().ToArray();
        Check(descriptors.Length==6 && descriptors.Select(d=>d.DeviceTypeID).Distinct().Count()==6,"Virtual devices must have six independent persistent identities.");
        Check(descriptors.All(d=>d.DetectionDescriptors.Count==0),"Virtual identities must never match USB hardware.");
        var host=new SimHub.Plugins.PluginManager();
        typeof(SimHub.Plugins.PluginManager).GetField("_manager",BindingFlags.Instance|BindingFlags.NonPublic).SetValue(host,new IdleGame().GetTransparentProxy());
        typeof(SimHub.Plugins.PluginManager).GetField("Instance",BindingFlags.Static|BindingFlags.NonPublic).SetValue(null,host);
        foreach(var descriptor in descriptors) {
            var device=descriptor.Factory(); device.DeviceDescriptor=descriptor;
            var module=device as LedModuleDevice;
            Check(module!=null && module.ledModuleSettings.DeviceDriver.GetType().Name=="VirtualLedManager","Factory must create only a virtual LED module.");
            Check(device.GetDetectionDescriptor()==null,"Virtual module must not expose USB detection.");
            device.LoadDefaultSettings();
            Check(module.ledModuleSettings.LedsDriver!=null && module.ledModuleSettings.LedsDriver.Settings.CurrentProfile!=null,"Virtual device must load stock effects without physical hardware.");
            module.ledModuleSettings.LEDsBrightness=37;
            module.ledModuleSettings.ButtonsBrightness=23;
            var saved=device.GetSettings(false,false);
            var restored=descriptor.Factory(); restored.DeviceDescriptor=descriptor;
            restored.SetSettings(saved,false);
            var restoredSettings=((LedModuleDevice)restored).ledModuleSettings;
            Check(restoredSettings.LEDsBrightness==37 && restoredSettings.ButtonsBrightness==23,"Independent virtual brightness must survive a settings reload.");
            Check(restoredSettings.LedsDriver.Settings.CurrentProfile.Name==module.ledModuleSettings.LedsDriver.Settings.CurrentProfile.Name,"Virtual effects must survive a settings reload.");
            restored.End();device.End();
        }
        var managerType=assembly.GetType("oVRlay.SimHubLeds.VirtualLedManager");
        var maskSetter=assembly.GetType("oVRlay.SimHubLeds.VirtualWheelModes").GetMethod("SetMask");
        Action<int> setMask=mask=>maskSetter.Invoke(null,new object[]{mask});
        int[] counts={88,86,62,76,66,63}; int[] rpmStarts={68,68,46,52,46,42};
        var managers=Enumerable.Range(1,6).Select(id=>(ILedDeviceManager)Activator.CreateInstance(managerType,new object[]{id})).ToArray();
        Color[] rpm=Enumerable.Range(0,24).Select(i=>Color.FromArgb(200,80+i,40)).ToArray();
        Color[] buttons=Enumerable.Range(0,17).Select(i=>Color.FromArgb(40+i,120,200)).ToArray();
        Action<ILedDeviceManager> display=m=>m.Display(()=>rpm,()=>buttons,null,null,null,null,false,null,.5,.25,.75,1);
        setMask(0);
        foreach(var m in managers) {display(m); Check(!m.IsConnected() && m.LastState==null,"Connected mode must not generate virtual frames.");}
        setMask(63);
        for(int i=0;i<6;++i) {
            var m=managers[i]; int after=0; m.AfterDisplay+=(s,e)=>++after; display(m);
            Check(after==1 && m.IsConnected(),"Virtual display must signal completed effects without hardware.");
            Check(m.GetLedDriver()==null && object.ReferenceEquals(m.GetDriverInstance(),m),"Virtual manager must expose no physical transport.");
            var map=m.GetPhysicalMapper();Check(map.GetPhysicalLedCount()==counts[i],"Each model must retain its physical LED count.");
            Color b=map.GetColor(0,m.LastState,false),r=map.GetColor(rpmStarts[i],m.LastState,false);
            Check(b.R==10 && b.G==30 && b.B==50,"Button brightness/order must be applied once.");
            Check(r.R==100 && r.G==40 && r.B==20,"RPM brightness and physical position must be preserved.");
            Check(map.GetColor(counts[i]-1,m.LastState,false).G==(80+counts[i]-rpmStarts[i]-1)/2,"Last RPM index must be preserved.");
            var raw=Enumerable.Repeat(Color.Transparent,counts[i]).ToArray();raw[0]=Color.FromArgb(200,100,80);
            var overrides=Enumerable.Repeat(Color.Transparent,counts[i]).ToArray();overrides[0]=Color.FromArgb(12,24,36);
            m.Display(()=>rpm,()=>buttons,null,null,()=>raw,()=>overrides,false);
            Check(map.GetColor(0,m.LastState,false).ToArgb()==Color.FromArgb(12,24,36).ToArgb(),"External physical overrides must win over raw and logical colors.");
        }
        // Turning off one model must not affect another; plug/unplug cannot switch identities.
        setMask(2);display(managers[0]);display(managers[1]);
        Check(!managers[0].IsConnected() && managers[0].LastState==null && managers[1].IsConnected(),"Per-model modes must be independent.");
        setMask(1);display(managers[0]);Check(managers[0].IsConnected(),"Re-enabling a virtual model must reconnect.");
        managers[0].Close();Check(!managers[0].IsConnected() && managers[0].LastState==null,"Disabled/removed devices must release state.");
        display(managers[0]);Check(managers[0].IsConnected(),"Enabled module must recover after Close.");
        int errors=0;managers[0].OnError+=(s,e)=>++errors;
        managers[0].Display(()=>{throw new Exception("bad effect");},null,null,null,null,null,false);
        Check(errors==1 && managers[0].LastState==null,"Effect failure must clear stale state and report an error.");
        display(managers[0]);Check(managers[0].LastState!=null,"Effect callbacks must recover on the next frame.");
        using(var entered=new ManualResetEvent(false)) using(var release=new ManualResetEvent(false)) {
            var first=new Thread(()=>managers[0].Display(()=>{entered.Set();release.WaitOne(2000);return rpm;},null,null,null,null,null,false));
            first.Start();Check(entered.WaitOne(2000),"Frame provider did not start.");
            var second=new Thread(()=>display(managers[0]));second.Start();
            try{Check(second.Join(200),"Overlapping virtual frames must not block.");setMask(0);}
            finally{release.Set();first.Join(2000);second.Join(2000);}
            Check(managers[0].LastState==null,"A mode change during effects computation must suppress the pending frame.");
        }
        setMask(1);managerType.GetMethod("Dispose",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(managers[0],null);
        display(managers[0]);Check(!managers[0].IsConnected() && managers[0].LastState==null,"Disposed device cannot reconnect or publish.");
        setMask(0);
        Console.WriteLine("PASS: actual installed SDK virtual driver lifecycle and mapping.");
    }
}




// The external game service is idle; real SimHub profile/settings code remains under test.
class IdleGame : System.Runtime.Remoting.Proxies.RealProxy
{
    public IdleGame() : base(typeof(GameReaderCommon.IGameManager)) {}
    public override System.Runtime.Remoting.Messaging.IMessage Invoke(System.Runtime.Remoting.Messaging.IMessage message)
    {
        var call=(System.Runtime.Remoting.Messaging.IMethodCallMessage)message;
        var method=(MethodInfo)call.MethodBase;
        object value=method.ReturnType==typeof(void)?null:method.ReturnType==typeof(string)?"":method.ReturnType.IsValueType?Activator.CreateInstance(method.ReturnType):null;
        return new System.Runtime.Remoting.Messaging.ReturnMessage(value,null,0,call.LogicalCallContext,call);
    }
}
