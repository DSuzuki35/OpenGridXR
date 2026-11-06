import {
  createDocument,
  validateDocument,
  validateProfile,
  parseProfile,
  normalizeDocument,
  normalizeProfile,
} from "./model.mjs";
export const isBrowserPreview = !globalThis.openGrid;
import {moduleDefaults, validateModuleSettings} from "./modules.mjs";
import visorTemplates from './visor-template-manifest.json' with { type: 'json' };
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
    document = normalizeDocument(document);
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
  async listSystemFonts(){return {available:false,families:[],error:'Installed font selection requires the Windows app.'};},
  async downloadVisorTemplate(request){
    const format=visorTemplates.formats[request?.format];
    if(!visorTemplates.styles.includes(request?.style)||!format)throw Error('Unsupported visor template.');
    const filename=format.file.replace('{style}',request.style),response=await fetch('templates/visor/'+filename);
    if(!response.ok)throw Error('Template unavailable. Build the application assets first.');
    const url=URL.createObjectURL(await response.blob()),anchor=document.createElement('a');anchor.href=url;anchor.download=filename;anchor.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
    return {saved:true,fileName:filename};
  },
  async layersInventory() { return { entries: [], backups: [], errors: ['Open the Windows app to inspect and edit OpenXR layer registrations. Browser preview is read only.'] }; },
  async layersSetEnabled() { throw new Error('Editing OpenXR layers requires the Windows app.'); },
  async layersRestore() { throw new Error('Restoring OpenXR layers requires the Windows app.'); },
  async getModuleSettings() {
    try {
      const saved = localStorage.getItem("opengrid-xr-modules-v1");
      return saved ? validateModuleSettings(JSON.parse(saved)) : {version:1,enabled:moduleDefaults()};
    } catch (error) {
      return {version:1,enabled:Object.fromEntries(Object.keys(moduleDefaults()).map(id=>[id,false])),error:`Module preferences could not be loaded: ${error.message}`};
    }
  },
  async setModuleSettings(value) {
    const settings = validateModuleSettings(value);
    localStorage.setItem("opengrid-xr-modules-v1", JSON.stringify(settings));
    return {...settings,restartRequired:true};
  },
  async launchLmuOffline() { throw new Error("LMU offline testing requires the Windows app."); },
  async setLmuPreferences() { throw new Error('Machine preferences require the Windows app.'); },
  async lmuStatus() { return {paths:{executable:'',qvfrManifest:''},compatibility:{supported:false,message:'Executable verification requires the Windows app.'},providers:{qvfr:{available:false},vectorxr:{available:false,embedded:true}},capture:{state:'inactive'},validation:'Integrated headset/provider validation is pending.'}; },
  async chooseLmuExecutable() { throw new Error('Executable selection requires the Windows app.'); },
  async chooseLmuProvider() { throw new Error('Provider selection requires the Windows app.'); },
  async previewLmuLaunch() { throw new Error('Launch validation requires the Windows app.'); },
  async copyLmuSteamLaunchOption() { throw new Error('Steam launch setup requires the installed Windows app.'); },
  async triggerLmuCapture() { throw new Error('Frame capture requires the Windows app.'); },
  async benchmarkStatus(){return {state:'idle',running:false,frameCount:0,lapSegments:0,message:'Recording requires the Windows app and a running sim.'};},
  async benchmarkStart(){throw new Error('Recording requires the Windows app and a running sim.');},
  async benchmarkStop(){return this.benchmarkStatus();},
  async benchmarkReport(){return null;},
  async benchmarkExport(){throw new Error('Recording requires the Windows app.');},
  async openSimQVFR() { window.open("https://github.com/Williem3/Sim-QVFR", "_blank", "noopener,noreferrer"); },
  async openVectorXR() { window.open("https://github.com/DienerTech/vectorxr", "_blank", "noopener,noreferrer"); },
  async openBuyMeACoffee() { window.open("https://buymeacoffee.com/dansuzuki", "_blank", "noopener,noreferrer"); },
  async openAffiliateLinks() { window.open("https://dansuzuki.com/discounts/", "_blank", "noopener,noreferrer"); },
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
  async colorCorrectionStatus() { return {available:false,supported:false,enabled:false,activePreset:null,effectActive:false,error:'Color correction requires the Windows app and a supported VR session.'}; },
  async cycleColorCorrection() { throw new Error('Live color presets require a supported VR session.'); },
  async listOverlayDisplays() { return []; },
  async selectOverlayDisplay() { throw new Error('Desktop capture requires the Windows app.'); },
  async desktopOverlayAction() { throw new Error('Desktop interaction requires the Windows app.'); },
  async selectOverlayWindow() { throw new Error("Window capture requires the Windows app."); },
  async overlayStatus() { return { running: false, available: false, capacity: 0, activeOverlayCount: 0, items: [], message: "Spatial overlays require the Windows app and an active OpenXR session." }; },
  async recenterOverlays() { throw new Error("Overlay centering requires an active OpenXR session."); },
  async centerGame() { throw new Error("Game centering requires the Windows app."); },
  async telemetryStatus() {
    return { available: false, running: false, enabled: false, requestedHz: 20, record: null, message: "Live telemetry requires the Windows app and a running sim." };
  },
  async load() {
    return load();
  },
  async save(document, expected) {
    if (expected !== String(revision))
      throw new Error("Profiles changed. Reload before saving.");
    validateDocument(document);
    localStorage.setItem(key, JSON.stringify(normalizeDocument(document)));
    revision++;
    return load();
  },
  async exportProfile(profile) {
    validateProfile(profile);
    download(
      { version: 1, profile:normalizeProfile(profile) },
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
  async importSteadyXr(game) {
    return new Promise((resolve, reject) => {
      const input = document.createElement("input");
      input.type = "file"; input.accept = ".ini";
      input.oncancel = () => resolve(null);
      input.onchange = async () => {
        try {
          const file = input.files?.[0];
          if (!file) return resolve(null);
          if (file.size > 1024 * 1024) throw new Error("Preset exceeds 1 MiB.");
          const {decodeSteadyXrPreset, importSteadyXrPreset} = await import("./steadyxr-import.mjs");
          resolve(importSteadyXrPreset(decodeSteadyXrPreset(new Uint8Array(await file.arrayBuffer())), {game, name:file.name.replace(/\.ini$/i, "")}));
        } catch (error) { reject(error); }
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
  previewOutputFraming() {},
  async openDataFolder() {
    throw new Error("Browser profiles are stored in this browser.");
  },
  async restoreBackup() {
    throw new Error("File backups are available in the Windows app.");
  },
};
export const api = globalThis.openGrid || preview;
