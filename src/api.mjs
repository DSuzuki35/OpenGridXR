import {
  createDocument,
  validateDocument,
  validateProfile,
  parseProfile,
} from "./model.mjs";
export const isBrowserPreview = !globalThis.openGrid;
const key = "opengrid-xr-v1";
let revision = 0;
const runtime = {
  available: false,
  registered: false,
  active: false,
  message: "Browser preview · native runtime is available in the Windows app.",
};
function load() {
  let document;
  const saved = localStorage.getItem(key);
  if (saved) {
    document = JSON.parse(saved);
    validateDocument(document);
  } else document = createDocument();
  return { document, revision: String(revision), runtime, backups: [] };
}
function download(value, name) {
  const a = document.createElement("a"),
    url = URL.createObjectURL(
      new Blob([JSON.stringify(value, null, 2)], { type: "application/json" }),
    );
  a.href = url;
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
const preview = {
  async runtimeOpticsInspect() { return { available: false, candidates: [], selected: null, message: "Runtime optics require the Windows app.", capabilities: { pimaxNative: { supported: false, message: "Use Pimax Play for native Wide FOV." } } }; },
  async runtimeOpticsChoose() { throw new Error("Runtime optics require the Windows app."); },
  async runtimeOpticsPreview() { throw new Error("Runtime optics require the Windows app."); },
  async runtimeOpticsPreviewRestore() { throw new Error("Runtime optics require the Windows app."); },
  async runtimeOpticsApply() { throw new Error("Runtime optics require the Windows app."); },
  async updateStatus() { return { phase: "unsupported", currentVersion: "Browser preview", prompt: false, message: "Install the Windows setup package to enable updates." }; },
  async checkUpdates() { return this.updateStatus(); },
  async updateChoice() { throw new Error("Updates require the installed Windows app."); },
  async installUpdate() { throw new Error("Updates require the installed Windows app."); },
  onUpdateStatus() { return () => {}; },
  async inputStatus() { return { active:false,running:false,available:false,devices:[],paused:true,message:"Joystick bindings require the Windows app." }; },
  async inputEditorState() { return {}; },
  async inputCaptureStart() { throw new Error("Joystick binding requires the Windows app."); },
  async inputCaptureStatus() { return { state:"idle" }; },
  async inputCaptureCancel() { return { state:"cancelled" }; },
  onInputProfile() { return () => {}; },
  async listOverlayWindows() { return []; },
  async selectOverlayWindow() { throw new Error("Window capture requires the Windows app."); },
  async overlayStatus() { return { running: false, available: false, capacity: 0, activeOverlayCount: 0, items: [], message: "Spatial overlays require the Windows app and an active OpenXR session." }; },
  async recenterOverlays() { throw new Error("Overlay centering requires an active OpenXR session."); },
  async centerGame() { throw new Error("Game centering requires the Windows app."); },
  async telemetryStatus() {
    return { available: false, running: false, enabled: false, requestedHz: 20, record: null, message: "Live telemetry requires the Windows app and a running sim." };
  },
  async nativeSettings() {
    return {
      available: false,
      path: "Windows desktop app required",
      revision: "missing",
      values: {
        outerResolution: null,
        focusWidth: null,
        resolutionScale: null,
      },
      backups: [],
      message: "Native game settings require the Windows app.",
    };
  },
  async chooseNativeSettings() {
    throw new Error("Use the Windows app to select an iRacing configuration.");
  },
  async previewNativeSettings() {
    throw new Error("Use the Windows app to review native game settings.");
  },
  async applyNativeSettings() {
    throw new Error("Use the Windows app to apply native game settings.");
  },
  async restoreNativeSettings() {
    throw new Error("Use the Windows app to restore native game settings.");
  },
  async openNativeBackups() {
    throw new Error("File backups are available in the Windows app.");
  },
  async load() {
    return load();
  },
  async save(document, expected) {
    if (expected !== String(revision))
      throw new Error("Profiles changed. Reload before saving.");
    validateDocument(document);
    localStorage.setItem(key, JSON.stringify(document));
    revision++;
    return load();
  },
  async exportProfile(profile) {
    validateProfile(profile);
    download(
      { version: 1, profile },
      `${profile.name.replace(/[^a-z0-9-_ ]/gi, "")}.opengrid.json`,
    );
    return true;
  },
  async importProfile() {
    return new Promise((resolve, reject) => {
      const input = document.createElement("input");
      input.type = "file";
      input.accept = ".json";
      input.oncancel = () => resolve(null);
      input.onchange = async () => {
        try {
          const f = input.files?.[0];
          if (!f) return resolve(null);
          if (f.size > 1024 * 1024) throw new Error("Profile is too large.");
          resolve(parseProfile(await f.text()));
        } catch (e) {
          reject(e);
        }
      };
      input.click();
    });
  },
  async runtimeStatus() {
    return runtime;
  },
  async listDisplays() {
    return [
      {
        id: "primary",
        label: "Primary display (browser preview)",
        width: 1920,
        height: 1080,
        primary: true,
      },
    ];
  },
  async installRuntime() {
    throw new Error("Use the Windows app to install the OpenXR connection.");
  },
  async removeRuntime() {
    throw new Error("Use the Windows app to manage its connection.");
  },
  async launchOutput() {
    throw new Error("Clean output requires the Windows app.");
  },
  async openDataFolder() {
    throw new Error("Browser profiles are stored in this browser.");
  },
  async restoreBackup() {
    throw new Error("File backups are available in the Windows app.");
  },
};
export const api = globalThis.openGrid || preview;
