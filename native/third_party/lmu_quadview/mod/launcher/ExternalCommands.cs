using System;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Threading;

namespace LmuQuadViewLauncher {
    // 0.3.5: signals from external tools (the SimHub plugin) for actions the launcher
    // already offers on its own joystick bindings. A signal carries no data; the
    // launcher remains the only writer of the benchmark overlay configuration.
    // Names match simhub/QuadViewTelemetry.cs (QuadViewCommands).
    public sealed class ExternalCommands : IDisposable {
        public const string ToggleOverlayName = "Local\\LMUQuadView.Command.ToggleOverlay";
        public const string ToggleRecordingName = "Local\\LMUQuadView.Command.ToggleRecording";
        readonly EventWaitHandle overlay, recording;
        public ExternalCommands() : this(ToggleOverlayName, ToggleRecordingName) { }
        internal ExternalCommands(string overlayName, string recordingName) {
            overlay = Create(overlayName); recording = Create(recordingName);
        }
        static EventWaitHandle Create(string name) {
            try {
                var security = new EventWaitHandleSecurity();
                security.SetAccessRuleProtection(true, false);
                using (var identity = WindowsIdentity.GetCurrent())
                    security.AddAccessRule(new EventWaitHandleAccessRule(identity.User, EventWaitHandleRights.FullControl, AccessControlType.Allow));
                security.AddAccessRule(new EventWaitHandleAccessRule(new SecurityIdentifier(WellKnownSidType.LocalSystemSid, null), EventWaitHandleRights.FullControl, AccessControlType.Allow));
                bool created;
                var handle = new EventWaitHandle(false, EventResetMode.AutoReset, name, out created, security);
                // Existing objects ignore both the requested reset mode and ACL.
                // Never consume a channel created before this launcher owned it.
                if (!created) { handle.Dispose(); return null; }
                return handle;
            }
            catch (UnauthorizedAccessException) { return null; } catch (System.IO.IOException) { return null; }
            catch (WaitHandleCannotBeOpenedException) { return null; } catch (PlatformNotSupportedException) { return null; }
        }
        public bool Available { get { return overlay != null && recording != null; } }
        // Non-blocking; called from the launcher's UI timer.
        public void Poll(Action toggleOverlay, Action toggleRecording) {
            if (overlay != null && overlay.WaitOne(0)) toggleOverlay();
            if (recording != null && recording.WaitOne(0)) toggleRecording();
        }
        public void Dispose() { if (overlay != null) overlay.Dispose(); if (recording != null) recording.Dispose(); }
    }
}
