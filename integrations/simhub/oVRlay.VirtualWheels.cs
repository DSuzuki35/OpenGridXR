using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Threading;
using BA63Driver.Interfaces;
using BA63Driver.Mapper;
using Newtonsoft.Json.Linq;
using SerialDash;
using SimHub.Plugins.Devices;
using SimHub.Plugins.OutputPlugins.GraphicalDash.LedModules;
using SimHub.Plugins.OutputPlugins.GraphicalDash.PSE;
using SimHub.Plugins.OutputPlugins.GraphicalDash.UI;

namespace oVRlay.SimHubLeds
{
    // A separate identity means an unplugged virtual wheel can never become a USB wheel.
    // No physical device manager or driver is constructed anywhere in this registry.
    public sealed class VirtualWheelRegistry : IDeviceDescriptorsRegistry
    {
        public IEnumerable<DeviceDescriptor> GetDevices()
        {
            foreach (var model in VirtualWheelModel.All)
            {
                var captured = model;
                yield return new DeviceDescriptor {
                    DeviceTypeID = model.VirtualId, Name = "OpenGrid XR virtual " + model.Name,
                    Brand = "OpenGrid XR", MaximumInstances = 1,
                    Factory = delegate { return new VirtualWheelDevice(captured); }
                };
            }
        }
    }

    internal sealed class VirtualWheelModel
    {
        public readonly int Id, Count, RpmCount, ButtonCount;
        public readonly string Name, SourceId;
        public readonly Type ColorEditor;
        public readonly int[] Split;
        public string VirtualId { get { return "oVRlay.VirtualWheel." + Id; } }
        private VirtualWheelModel(int id, string name, int count, int rpm, int buttons, string source, Type editor, params int[] split)
        { Id=id; Name=name; Count=count; RpmCount=rpm; ButtonCount=buttons; SourceId=source; ColorEditor=editor; Split=split; }
        public static readonly VirtualWheelModel[] All = {
            new VirtualWheelModel(1,"Hyper P1",88,20,17,"8562E400-8AC8-4A35-98B2-AD05B17339DD",typeof(DefaultColorsEditorGSIHyperP1),3,14,3),
            new VirtualWheelModel(2,"Hyper SL",86,18,17,"9BFB0C71-BAC9-4442-95E0-A2B3C9914401",typeof(DefaultColorsEditorGSIHyperSL),3,12,3),
            new VirtualWheelModel(3,"GXL V2",62,16,17,"DEC2C70A-4AA1-4E78-A022-2EEFE13C8A9A",typeof(DefaultColorsEditorGSIGXLV2),0,16,0),
            new VirtualWheelModel(4,"GT-MAX 32",76,24,13,"F3D8503D-9234-4A44-B842-802C9A8552ED",typeof(DefaultColorsEditorGSIGTMax),4,16,4),
            new VirtualWheelModel(5,"FPE2",66,20,17,"EFC17674-559A-44DB-8D24-C6CFD203384D",typeof(DefaultColorsEditorGSIFormulaProEliteV2),3,14,3),
            new VirtualWheelModel(6,"Ascher Artura Ultimate",63,21,14,"FB95779F-C045-42C0-9D04-C1B3861796EB",typeof(DefaultColorsEditorAscherRacingMcLarenArturaUltimate),3,15,3)
        };

        public IPhysicalMapper CreateMapper()
        {
            var maps = new List<IMap>();
            if (Id == 6)
            {
                for (int i=0;i<7;++i) maps.Add(new ButtonMap(i));
                maps.Add(new RepeatGroup(new IgnoreMap(),14));
                for (int i=7;i<14;++i) maps.Add(new ButtonMap(i));
                maps.Add(new RepeatGroup(new IgnoreMap(),14));
                // This is SimHub's Artura mapper: RepeatGroup advances the logical index.
                maps.Add(new RepeatGroup(new LedMap(0),21));
            }
            else
            {
                int[] groups = (Id==3 || Id==5) ? new[]{5,4,5,5,5,2,2,1,2,2,2,2,2,1,2,2,2} :
                    Enumerable.Repeat(4,ButtonCount).ToArray();
                for(int i=0;i<groups.Length;++i) maps.Add(new ButtonGroupMap(i,groups[i]));
                maps.Add(new LedRangeMap(0,RpmCount));
            }
            var mapper = new PhysicalMapper(maps.ToArray());
            if(mapper.GetPhysicalLedCount()!=Count) throw new InvalidOperationException("Incompatible virtual wheel LED mapping.");
            return mapper;
        }
    }

    internal static class VirtualWheelModes
    {
        private static int mask;
        public static void SetMask(int value) { Interlocked.Exchange(ref mask,value & 63); }
        public static bool IsEnabled(int model) { return (Volatile.Read(ref mask) & (1 << (model-1))) != 0; }
    }

    public sealed class VirtualLedManager : ILedDeviceManager
    {
        private readonly VirtualWheelModel model;
        private readonly IPhysicalMapper mapper;
        private volatile bool closed = true;
        private volatile bool disposed;
        private readonly object gate = new object();
        private static readonly Color[] Empty = new Color[0];
        public VirtualLedManager(int modelId)
        {
            if(modelId<1 || modelId>6) throw new ArgumentOutOfRangeException("modelId");
            model=VirtualWheelModel.All[modelId-1]; mapper=model.CreateMapper();
        }
        public LedModuleSettings LedModuleSettings { get; set; }
        public LedDeviceState LastState { get; private set; }
        public event EventHandler BeforeDisplay;
        public event EventHandler AfterDisplay;
        public event EventHandler OnConnect;
        public event EventHandler OnError;
        public event EventHandler OnDisconnect;
        public bool IsConnected() { return !closed && !disposed && VirtualWheelModes.IsEnabled(model.Id); }
        public string GetSerialNumber() { return "OVRLAY-VIRTUAL-"+model.Id; }
        public string GetFirmwareVersion() { return "virtual"; }
        public object GetDriverInstance() { return this; }
        public IPhysicalMapper GetPhysicalMapper() { return mapper; }
        public ILedDriverBase GetLedDriver() { return null; }
        public void SerialPortCanBeScanned(object sender, SerialDashController.ScanArgs args) { }
        public void ResetDetection() { }
        public void Close()
        {
            lock(gate) { bool wasOpen=!closed; closed=true; LastState=null; if(wasOpen) Raise(OnDisconnect); }
        }
        internal void Dispose() { lock(gate) { disposed=true; Close(); } }
        private void Raise(EventHandler handler) { if(handler!=null) handler(this,EventArgs.Empty); }
        private static Color[] Read(Func<Color[]> provider) { return provider==null ? Empty : provider() ?? Empty; }
        public void Display(Func<Color[]> leds, Func<Color[]> buttons, Func<Color[]> encoders,
            Func<Color[]> matrix, Func<Color[]> rawState, Func<Color[]> overrideState,
            bool forceRefresh, Func<object> extraData=null, double rpmBrightness=1,
            double buttonsBrightness=1, double encodersBrightness=1, double matrixBrightness=1)
        {
            // Never wait on a competing frame. Only the SimHub effects callbacks are evaluated.
            if(!Monitor.TryEnter(gate)) return;
            try
            {
                if(disposed || !VirtualWheelModes.IsEnabled(model.Id)) { Close(); return; }
                if(closed) { closed=false; Raise(OnConnect); }
                Raise(BeforeDisplay);
                var state=new LedDeviceState(Read(leds),Read(buttons),Read(encoders),Read(matrix),
                    Read(rawState),Read(overrideState),rpmBrightness,buttonsBrightness,encodersBrightness,matrixBrightness);
                state.ExtraData=extraData==null ? null : extraData();
                if(disposed || !VirtualWheelModes.IsEnabled(model.Id)) { Close(); return; }
                LastState=state;
                Raise(AfterDisplay);
            }
            catch(Exception) { LastState=null; Raise(OnError); }
            finally { Monitor.Exit(gate); }
        }
    }

    internal sealed class VirtualWheelSettings : LedModuleSettings
    {
        public VirtualWheelSettings(VirtualWheelModel model)
            : base("-","-","OpenGrid XR virtual "+model.Name,Enumerable.Repeat(Color.White,model.ButtonCount).ToArray(),
                model.ButtonCount,model.RpmCount,-1,null,model.Count)
        {
            DeviceDriver=new VirtualLedManager(model.Id) { LedModuleSettings=this };
            this.SetLedSplit(new LedSplit(model.Split));
            ButtonsColorManager.ColorEditorType=model.ColorEditor;
            EnableBrightnessSection=true;
        }
    }

    internal sealed class VirtualWheelDevice : LedModuleDevice
    {
        private readonly VirtualWheelModel model;
        public VirtualWheelDevice(VirtualWheelModel model) : base(new VirtualWheelSettings(model)) { this.model=model; }
        public override USBRequest GetDetectionDescriptor() { return null; }
        public override void LoadDefaultSettings()
        {
            base.LoadDefaultSettings();
            // Read the matching shipped LED defaults, never a user's physical-device settings.
            string root=Path.GetDirectoryName(typeof(DevicesPlugin).Assembly.Location);
            string path=Path.Combine(root,"DevicesDefaults",model.SourceId+".shdevice");
            if(!File.Exists(path)) return;
            var settings=JObject.Parse(File.ReadAllText(path))["Settings"];
            var leds=settings==null ? null : settings["LEDS"] ?? settings;
            if(leds!=null && leds["ledModuleSettings"]!=null) SetSettings(leds,true);
        }
        public override void End()
        {
            ((VirtualLedManager)ledModuleSettings.DeviceDriver).Dispose();
            base.End();
        }
    }
}
