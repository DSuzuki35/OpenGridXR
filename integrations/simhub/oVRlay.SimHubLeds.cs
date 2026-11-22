using System;
using System.Diagnostics;
using System.Drawing;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;
using BA63Driver.Mapper;
using GameReaderCommon;
using SimHub.Plugins;
using SimHub.Plugins.Devices;
using SimHub.Plugins.OutputPlugins.GraphicalDash.LedModules;
using SimHub.Plugins.OutputPlugins.GraphicalDash.PSE;

[assembly: AssemblyTitle("oVRlay SimHub LED Observer")]
[assembly: AssemblyVersion("1.2.0.0")]
[assembly: AssemblyFileVersion("1.2.0.0")]

namespace oVRlay.SimHubLeds
{
    public sealed class ObserverSettings
    {
        public bool Enabled { get; set; }
        public ObserverSettings() { Enabled = true; }
    }

    [PluginName("oVRlay lights")]
    [PluginAuthor("oVRlay")]
    [PluginDescription("Mirrors each supported wheel's existing LED output to its own oVRlay model.")]
    public sealed class LedObserverPlugin : IPlugin, IDataPlugin, IWPFSettingsV2
    {
        private sealed class ModelSpec
        {
            public readonly int Id, Count;
            public readonly string Name;
            public readonly string[] TypeIds;
            public ModelSpec(int id,string name,int count,params string[] typeIds)
            { Id=id;Name=name;Count=count;TypeIds=typeIds; }
            public bool Matches(string typeId)
            { return TypeIds.Any(id=>string.Equals(id,typeId,StringComparison.OrdinalIgnoreCase)); }
        }
        private sealed class ActiveDevice
        {
            public ModelSpec Spec;
            public LedModuleDevice Module;
            public ILedDeviceManager Driver;
            public LedForwarder Forwarder;
            public long LastFrame;
        }
        // Verified against the installed GSI/Ascher registries and physical mappers.
        private static readonly ModelSpec[] Models={
            new ModelSpec(1,"Hyper P1",88,"8562E400-8AC8-4A35-98B2-AD05B17339DD","66D962E7-17D7-41EF-98F3-6444689CCB57"),
            new ModelSpec(2,"Hyper SL",86,"9BFB0C71-BAC9-4442-95E0-A2B3C9914401"),
            new ModelSpec(3,"GXL V2",62,"DEC2C70A-4AA1-4E78-A022-2EEFE13C8A9A"),
            new ModelSpec(4,"GT-MAX 32",76,"F3D8503D-9234-4A44-B842-802C9A8552ED","3805B7BC-0587-4458-9AB9-0B9BDCD25377"),
            new ModelSpec(5,"FPE2",66,"EFC17674-559A-44DB-8D24-C6CFD203384D"),
            new ModelSpec(6,"Ascher Artura Ultimate",63,"FB95779F-C045-42C0-9D04-C1B3861796EB")
        };
        private ObserverSettings settings;
        private volatile bool stopped = true;
        private volatile bool enabled;
        private volatile string status = "Not started.";
        private volatile string startupError;
        private volatile ActiveDevice[] activeDevices=new ActiveDevice[0];
        private LocalUdpSender sender;
        private volatile LedForwarder[] forwarders;
        private long nextBind;
        private readonly object lifecycleGate = new object();

        public PluginManager PluginManager { get; set; }
        public string LeftMenuTitle { get { return "oVRlay lights"; } }
        public ImageSource PictureIcon { get { return null; } }

        public void Init(PluginManager pluginManager)
        {
            PluginManager = pluginManager;
            settings = this.ReadCommonSettings<ObserverSettings>("GeneralSettings", delegate { return new ObserverSettings(); });
            enabled = settings.Enabled;
            stopped = false;
            status = enabled ? "Waiting for supported wheels." : "Light mirroring is off.";
            try
            {
                string error = CompatibilityError();
                if (error != null) { startupError = status = error; return; }
                sender = new LocalUdpSender();
                forwarders=Models.Select(spec=>new LedForwarder(Stopwatch.GetTimestamp,Stopwatch.Frequency,
                    sender.Queue,spec.Id,spec.Count)).ToArray();
            }
            catch (Exception ex) { startupError = status = "Observer unavailable: " + ex.Message; }
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        private static string CompatibilityError()
        {
            if (typeof(LedModuleDevice).GetMethod("GetLastState", Type.EmptyTypes) == null ||
                typeof(LedModuleDevice).GetMethod("GetMapper", Type.EmptyTypes) == null ||
                typeof(ILedDeviceManager).GetEvent("AfterDisplay") == null ||
                typeof(ILedDeviceManager).GetProperty("LastState") == null ||
                typeof(ILedDeviceManager).GetMethod("GetPhysicalMapper", Type.EmptyTypes) == null ||
                typeof(IPhysicalMapper).GetMethod("GetColor", new[] { typeof(int), typeof(LedDeviceState), typeof(bool) }) == null)
                return "This SimHub LED API is incompatible. Rebuild the observer for your installed SimHub version.";
            return null;
        }

        public void DataUpdate(PluginManager pluginManager, ref GameData data)
        {
            if (stopped) return;
            if (!enabled)
            {
                lock (lifecycleGate)
                {
                    if (!enabled)
                    {
                        Unbind();
                        status = "Light mirroring is off.";
                    }
                }
                return;
            }
            if (forwarders == null) { if (startupError != null) status = startupError; return; }
            long now = Stopwatch.GetTimestamp();
            if (now < System.Threading.Interlocked.Read(ref nextBind)) return;
            lock (lifecycleGate)
            {
                if (stopped || !enabled || forwarders == null) return;
                System.Threading.Interlocked.Exchange(ref nextBind, now + Stopwatch.Frequency);
                try
                {
                    BindExistingDevices(pluginManager);
                    if (sender.LastError != null) status = sender.LastError;
                    else
                    {
                        var active=activeDevices;
                        status=active.Length==0 ? "Waiting for a connected wheel in SimHub Devices." :
                            string.Join("; ",active.Select(binding=>binding.Spec.Name+
                                (now-System.Threading.Interlocked.Read(ref binding.LastFrame)<=Stopwatch.Frequency ?
                                " - "+binding.Spec.Count+" live LEDs" : " - waiting for LED output")));
                    }
                }
                catch (Exception ex)
                {
                    Unbind();
                    status = "Waiting for compatible wheels: " + ex.Message;
                }
            }
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        private void BindExistingDevices(PluginManager pluginManager)
        {
            DevicesPlugin devices=pluginManager.GetPlugin<DevicesPlugin>();
            if(devices==null) {Unbind();status="Enable SimHub's Devices feature to mirror wheel lights.";return;}
            var previous=activeDevices;
            var candidates=devices.GetDevices()
                .Where(d=>d.Enabled && !d.Suspended && d.DeviceDescriptor!=null)
                .SelectMany(d=>d.GetInstances<LedModuleDevice>().Select(m=>new{Device=d,Module=m}))
                .Where(x=>x.Module.ledModuleSettings!=null && x.Module.ledModuleSettings.IsEnabled &&
                    x.Module.ledModuleSettings.DeviceDriver!=null && x.Module.ledModuleSettings.DeviceDriver.IsConnected())
                .ToList();
            var next=new System.Collections.Generic.List<ActiveDevice>();
            foreach(var spec in Models)
            {
                var old=previous.FirstOrDefault(binding=>binding.Spec.Id==spec.Id);
                var selected=candidates.Where(x=>spec.Matches(x.Device.DeviceDescriptor.DeviceTypeID))
                    .OrderByDescending(x=>old!=null && object.ReferenceEquals(x.Module,old.Module))
                    .ThenBy(x=>x.Device.InstanceId).FirstOrDefault();
                if(selected==null) continue;
                ILedDeviceManager manager=selected.Module.ledModuleSettings.DeviceDriver;
                IPhysicalMapper mapper=manager.GetPhysicalMapper();
                if(mapper==null || mapper.GetPhysicalLedCount()!=spec.Count) continue;
                if(old!=null && object.ReferenceEquals(old.Driver,manager) && object.ReferenceEquals(old.Module,selected.Module))
                    next.Add(old);
                else next.Add(new ActiveDevice{Spec=spec,Module=selected.Module,Driver=manager,Forwarder=forwarders[spec.Id-1]});
            }
            // Publish a complete immutable binding snapshot before accepting new callbacks.
            activeDevices=next.ToArray();
            foreach(var old in previous) if(!next.Contains(old)) old.Driver.AfterDisplay-=OnAfterDisplay;
            foreach(var binding in next) if(Array.IndexOf(previous,binding)<0) binding.Driver.AfterDisplay+=OnAfterDisplay;
        }

        private void OnAfterDisplay(object eventSender,EventArgs args)
        {
            if(stopped || !enabled) return;
            var binding=Array.Find(activeDevices,b=>object.ReferenceEquals(b.Driver,eventSender));
            if(binding==null) return;
            try
            {
                if(binding.Forwarder.TryPublish(delegate{return ReadPhysicalColors(binding);}))
                    System.Threading.Interlocked.Exchange(ref binding.LastFrame,Stopwatch.GetTimestamp());
            }
            catch(Exception ex) {status="LED observer paused: "+ex.Message;}
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        private System.Drawing.Color[] ReadPhysicalColors(ActiveDevice binding)
        {
            if(stopped || !enabled || Array.IndexOf(activeDevices,binding)<0 || !binding.Driver.IsConnected()) return null;
            // The completed state and mapping belong to this same physical device.
            LedDeviceState state=binding.Driver.LastState;
            IPhysicalMapper mapper=binding.Driver.GetPhysicalMapper();
            if(state==null || mapper==null || mapper.GetPhysicalLedCount()!=binding.Spec.Count) return null;
            var colors=new System.Drawing.Color[binding.Spec.Count];
            for(int i=0;i<colors.Length;++i) colors[i]=mapper.GetColor(i,state,false);
            return colors;
        }

        private void Unbind()
        {
            var previous=activeDevices;
            activeDevices=new ActiveDevice[0];
            foreach(var binding in previous)
                try{binding.Driver.AfterDisplay-=OnAfterDisplay;}catch(Exception){}
        }

        public Control GetWPFSettingsControl(PluginManager pluginManager)
        {
            var panel = new StackPanel { Margin = new Thickness(20), MaxWidth = 680 };
            panel.Children.Add(new TextBlock { Text = "oVRlay lights", FontSize = 24, Margin = new Thickness(0, 0, 0, 16) });
            var toggle = new CheckBox { Content = "Mirror my wheel lights in oVRlay", IsChecked = enabled, Margin = new Thickness(0, 0, 0, 16) };
            panel.Children.Add(toggle);
            panel.Children.Add(new TextBlock { Text = "Each virtual wheel uses its own matching SimHub device: Hyper P1, Hyper SL, GXL V2, GT-MAX 32, FPE2 or Ascher Artura Ultimate. Keep those devices connected and enabled. Existing effects, button colors and brightness are preserved; missing devices do not borrow another wheel's colors.", TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 0, 0, 16) });
            var statusText = new TextBlock { Text = status, TextWrapping = TextWrapping.Wrap };
            panel.Children.Add(statusText);
            RoutedEventHandler changed = delegate
            {
                lock (lifecycleGate)
                {
                    enabled = toggle.IsChecked == true;
                    settings.Enabled = enabled;
                    System.Threading.Interlocked.Exchange(ref nextBind, 0);
                    if (!enabled) Unbind();
                    status = enabled ? "Waiting for supported wheels." : "Light mirroring is off.";
                }
                try { this.SaveCommonSettings("GeneralSettings", settings); }
                catch (Exception) { status = "Setting changed for this session; it could not be saved."; }
                statusText.Text = status;
            };
            toggle.Checked += changed;
            toggle.Unchecked += changed;
            var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
            timer.Tick += delegate { statusText.Text = status; };
            var control = new UserControl { Content = panel };
            control.Loaded += delegate { timer.Start(); };
            control.Unloaded += delegate { timer.Stop(); };
            return control;
        }

        public void End(PluginManager pluginManager)
        {
            LocalUdpSender previous;
            lock (lifecycleGate)
            {
                stopped = true;
                Unbind();
                forwarders = null;
                previous = sender;
                sender = null;
            }
            if (previous != null) previous.Dispose();
            if (settings != null)
            {
                try { this.SaveCommonSettings("GeneralSettings", settings); } catch (Exception) { }
            }
            status = "Stopped.";
        }
    }
}
