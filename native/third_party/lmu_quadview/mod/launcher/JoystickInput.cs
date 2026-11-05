using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;

namespace LmuQuadViewLauncher
{
    public sealed class JoystickBinding
    {
        public Guid Device { get; set; }
        public string Name { get; set; }
        public int Button { get; set; }
        public override string ToString() { return (String.IsNullOrWhiteSpace(Name) ? "Joystick" : Name) + " / button " + (Button+1) + " / " + Device.ToString("N").Substring(24); }
    }
    public sealed class JoystickButtons
    {
        public Guid Device;
        public string Name;
        public bool[] Buttons;
        public int ConnectionEpoch;
    }
    public interface IJoystickButtonSource : IDisposable { IList<JoystickButtons> Read(); }
    // Edge detector is independent of DirectInput so reconnect/listen behavior is testable.
    public sealed class JoystickInput : IDisposable
    {
        readonly IJoystickButtonSource source;
        Dictionary<Guid, bool[]> previous = new Dictionary<Guid, bool[]>();
        Dictionary<Guid,int> epochs = new Dictionary<Guid,int>();
        Action<JoystickBinding> listening;
        bool released;
        public JoystickBinding Visibility { get; set; }
        public JoystickBinding Recording { get; set; }
        public event Action VisibilityPressed;
        public event Action RecordingPressed;
        public bool Listening { get { return listening != null; } }
        public int DeviceCount {get;private set;}
        public string ListenStatus { get { return !Listening ? "" : DeviceCount==0 ? "No joystick connected. Connect a device, then release its buttons. Esc cancels." : released ? "Press the joystick button to assign. Esc cancels." : "Release all held joystick buttons first. Esc cancels."; } }
        public JoystickInput(IJoystickButtonSource source) { this.source = source; }
        public void Listen(Action<JoystickBinding> assigned) { listening = assigned; released = false; }
        public void CancelListen() { listening = null; }
        static bool Matches(JoystickBinding binding, Guid device, int button) { return binding != null && binding.Device == device && binding.Button == button; }
        public void Tick()
        {
            var states = source.Read();
            DeviceCount=states.Count;
            var next = new Dictionary<Guid, bool[]>();
            var nextEpochs = new Dictionary<Guid,int>();
            bool anyHeld = false;
            foreach (var state in states) foreach (bool down in state.Buttons) anyHeld |= down;
            bool capture = Listening;
            foreach (var state in states) {
                bool[] old;
                int epoch;
                bool known = previous.TryGetValue(state.Device, out old) && epochs.TryGetValue(state.Device,out epoch) && epoch==state.ConnectionEpoch;
                nextEpochs[state.Device]=state.ConnectionEpoch;
                var buttons = (bool[])state.Buttons.Clone(); next[state.Device] = buttons;
                if (!known) continue; // Seed initial/reconnected held buttons; never synthesize a press.
                for (int i = 0; i < Math.Min(128, buttons.Length); ++i) {
                    if (!buttons[i] || (i < old.Length && old[i])) continue;
                    if (capture) {
                        if (Listening && released) { var callback = listening; listening = null; callback(new JoystickBinding { Device = state.Device, Name = state.Name, Button = i }); }
                    } else {
                        if (Matches(Visibility, state.Device, i) && VisibilityPressed != null) VisibilityPressed();
                        if (Matches(Recording, state.Device, i) && RecordingPressed != null) RecordingPressed();
                    }
                }
            }
            if (Listening && !anyHeld) released = true;
            previous = next; epochs=nextEpochs;
        }
        public void Dispose() { source.Dispose(); }
    }

    // DirectInput 8 COM vtables; no managed DirectX or third-party runtime dependency.
    // All native calls live on one background thread; the UI only consumes snapshots.
    public sealed class DirectInputButtonSource : IJoystickButtonSource
    {
        readonly object gate = new object();
        readonly IntPtr window;
        readonly Thread worker;
        volatile bool stopping;
        IList<JoystickButtons> snapshot = new List<JoystickButtons>();
        IntPtr input;
        int connectionEpoch;
        readonly Dictionary<Guid, Device> devices = new Dictionary<Guid, Device>();
        sealed class Device { public IntPtr Pointer; public string Name; public DateTime RetryAfter; public int Epoch; }
        [DllImport("dinput8.dll", CallingConvention = CallingConvention.StdCall)] static extern int DirectInput8Create(IntPtr instance, uint version, ref Guid iid, out IntPtr output, IntPtr outer);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern IntPtr GetModuleHandle(string name);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int Simple(IntPtr self);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate uint Release(IntPtr self);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int CreateDevice(IntPtr self, ref Guid instance, out IntPtr device, IntPtr outer);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int EnumDevices(IntPtr self, uint type, EnumCallback callback, IntPtr context, uint flags);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int EnumCallback(IntPtr instance, IntPtr context);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int SetFormat(IntPtr self, ref DataFormat format);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int SetCooperative(IntPtr self, IntPtr window, uint flags);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int GetState(IntPtr self, int size, [Out] byte[] state);
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] struct Instance {
            public uint Size; public Guid InstanceGuid, ProductGuid; public uint Type;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)] public string InstanceName;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)] public string ProductName;
            public Guid ForceFeedback; public ushort UsagePage, Usage;
        }
        [StructLayout(LayoutKind.Sequential)] struct DataFormat { public uint Size, ObjectSize, Flags, DataSize, Count; public IntPtr Objects; }
        [StructLayout(LayoutKind.Sequential)] struct ObjectFormat { public IntPtr Guid; public uint Offset, Type, Flags; }
        static T Method<T>(IntPtr self, int slot) where T : class {
            return (T)(object)Marshal.GetDelegateForFunctionPointer(Marshal.ReadIntPtr(Marshal.ReadIntPtr(self), slot * IntPtr.Size), typeof(T));
        }
        public DirectInputButtonSource(IntPtr window) {
            this.window = window; worker = new Thread(Run) { IsBackground = true, Name = "Benchmark joystick" }; worker.Start();
        }
        public IList<JoystickButtons> Read() { lock (gate) return snapshot; }
        void Close(Device device) { Method<Simple>(device.Pointer, 8)(device.Pointer); Method<Release>(device.Pointer, 2)(device.Pointer); }
        bool Configure(IntPtr device) {
            int size = Marshal.SizeOf(typeof(ObjectFormat));
            IntPtr objects = Marshal.AllocHGlobal(size * 128), guid = Marshal.AllocHGlobal(16);
            try {
                Marshal.StructureToPtr(new Guid("A36D02F0-C9F3-11CF-BFC7-444553540000"), guid, false);
                for (int i = 0; i < 128; ++i) Marshal.StructureToPtr(new ObjectFormat { Guid = guid, Offset = (uint)i, Type = 0x8000000C | ((uint)i << 8) }, IntPtr.Add(objects, size * i), false);
                var format = new DataFormat { Size = (uint)Marshal.SizeOf(typeof(DataFormat)), ObjectSize = (uint)size, Flags = 2, DataSize = 128, Count = 128, Objects = objects };
                return Method<SetFormat>(device, 11)(device, ref format) >= 0 && Method<SetCooperative>(device, 13)(device, window, 10) >= 0;
            } finally { Marshal.FreeHGlobal(objects); Marshal.FreeHGlobal(guid); }
        }
        void Discover() {
            var present = new HashSet<Guid>();
            EnumCallback callback = delegate(IntPtr pointer, IntPtr context) {
                var instance = (Instance)Marshal.PtrToStructure(pointer, typeof(Instance)); present.Add(instance.InstanceGuid);
                if (!devices.ContainsKey(instance.InstanceGuid)) {
                    IntPtr device;
                    if (Method<CreateDevice>(input, 3)(input, ref instance.InstanceGuid, out device, IntPtr.Zero) >= 0) {
                        if (Configure(device)) devices.Add(instance.InstanceGuid, new Device { Pointer = device, Name = instance.InstanceName,Epoch=++connectionEpoch });
                        else Method<Release>(device, 2)(device);
                    }
                }
                return 1;
            };
            int result = Method<EnumDevices>(input, 4)(input, 4, callback, IntPtr.Zero, 1);
            GC.KeepAlive(callback);
            if (result < 0) return;
            foreach (var id in new List<Guid>(devices.Keys)) if (!present.Contains(id)) { Close(devices[id]); devices.Remove(id); }
        }
        void Run() {
            try {
                var iid = new Guid("BF798031-483A-4DA2-AA99-5D64ED369700");
                if (DirectInput8Create(GetModuleHandle(null), 0x0800, ref iid, out input, IntPtr.Zero) < 0) return;
                DateTime discover = DateTime.MinValue;
                while (!stopping) {
                    if (DateTime.UtcNow >= discover) { Discover(); discover = DateTime.UtcNow.AddSeconds(2); }
                    var states = new List<JoystickButtons>();
                    foreach (var pair in devices) {
                        if(DateTime.UtcNow < pair.Value.RetryAfter)continue;
                        var pointer = pair.Value.Pointer; var bytes = new byte[128];
                        int poll=Method<Simple>(pointer, 25)(pointer);
                        if (poll < 0 || Method<GetState>(pointer, 9)(pointer, 128, bytes) < 0) {
                            pair.Value.Epoch=++connectionEpoch;
                            pair.Value.RetryAfter=DateTime.UtcNow.AddMilliseconds(150);
                            Method<Simple>(pointer, 7)(pointer); // Retry on the next poll; absence resets the edge detector.
                            continue;
                        }
                        var buttons = new bool[128]; for (int i = 0; i < 128; ++i) buttons[i] = (bytes[i] & 128) != 0;
                        states.Add(new JoystickButtons { Device = pair.Key, Name = pair.Value.Name, Buttons = buttons,ConnectionEpoch=pair.Value.Epoch });
                    }
                    lock (gate) snapshot = states;
                    Thread.Sleep(20);
                }
            } catch (Exception) { /* Missing DirectInput/device loss must not interrupt the launcher. */ }
            finally {
                lock (gate) snapshot = new List<JoystickButtons>();
                foreach (var device in devices.Values) Close(device);
                if (input != IntPtr.Zero) Method<Release>(input, 2)(input);
            }
        }
        public void Dispose() { stopping = true; worker.Join(250); }
    }
}
