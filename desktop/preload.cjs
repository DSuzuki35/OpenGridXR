const { contextBridge, ipcRenderer } = require("electron");
const invoke = (name, ...args) =>
  ipcRenderer.invoke(`opengrid:${name}`, ...args);
contextBridge.exposeInMainWorld(
  "openGrid",
  Object.freeze({
    load: () => invoke("load"),
    listSystemFonts: options => invoke('listSystemFonts',options),
    downloadVisorTemplate: request => invoke('downloadVisorTemplate',request),
    layersInventory: () => invoke('layersInventory'),
    layersSetEnabled: request => invoke('layersSetEnabled',request),
    layersRestore: request => invoke('layersRestore',request),
    getModuleSettings: () => invoke("getModuleSettings"),
    setModuleSettings: settings => invoke("setModuleSettings", settings),
    updateStatus: () => invoke("updateStatus"),
    checkUpdates: () => invoke("checkUpdates"),
    updateChoice: (choice) => invoke("updateChoice", choice),
    installUpdate: () => invoke("installUpdate"),
    onUpdateStatus: (callback) => {
      const listener = (_event, result) => callback(result);
      ipcRenderer.on("opengrid:updateStatus", listener);
      return () => ipcRenderer.removeListener("opengrid:updateStatus", listener);
    },
    inputStatus: () => invoke("inputStatus"),
    inputEditorState: (state) => invoke("inputEditorState", state),
    inputCaptureStart: (actionId, profileId) => invoke("inputCaptureStart", actionId, profileId),
    inputCaptureStatus: () => invoke("inputCaptureStatus"),
    inputCaptureCancel: () => invoke("inputCaptureCancel"),
    onInputProfile: (callback) => {
      const listener = (_event, result) => callback(result);
      ipcRenderer.on("opengrid:inputProfile", listener);
      return () => ipcRenderer.removeListener("opengrid:inputProfile", listener);
    },
    save: (doc, revision) => invoke("save", doc, revision),
    importProfile: () => invoke("importProfile"),
    importSteadyXr: (game) => invoke("importSteadyXr", game),
    exportProfile: (profile) => invoke("exportProfile", profile),
    restoreBackup: (id, revision) => invoke("restoreBackup", id, revision),
    runtimeStatus: () => invoke("runtimeStatus"),
    benchmarkStatus: () => invoke('benchmarkStatus'),
    benchmarkStart: options => invoke('benchmarkStart',options),
    benchmarkStop: () => invoke('benchmarkStop'),
    benchmarkReport: () => invoke('benchmarkReport'),
    benchmarkExport: format => invoke('benchmarkExport',format),
    telemetryStatus: (game) => invoke("telemetryStatus", game),
    colorCorrectionStatus: game => invoke('colorCorrectionStatus',game),
    cycleColorCorrection: game => invoke('cycleColorCorrection',game),
    listOverlayWindows: () => invoke("listOverlayWindows"),
    listOverlayDisplays: () => invoke('listOverlayDisplays'),
    selectOverlayDisplay: (game,id,displayId) => invoke('selectOverlayDisplay',game,id,displayId),
    desktopOverlayAction: (game,id,action) => invoke('desktopOverlayAction',game,id,action),
    selectOverlayWindow: (game, id, sourceId) => invoke("selectOverlayWindow", game, id, sourceId),
    overlayStatus: (game) => invoke("overlayStatus", game),
    recenterOverlays: (game) => invoke("recenterOverlays", game),
    centerGame: (game) => invoke("centerGame", game),
    listDisplays: () => invoke("listDisplays"),
    installRuntime: () => invoke("installRuntime"),
    removeRuntime: () => invoke("removeRuntime"),
    launchOutput: (profile) => invoke("launchOutput", profile),
    launchLmuOffline: (profileId, revision, options) => invoke("launchLmuOffline", profileId, revision, options),
    lmuStatus: () => invoke('lmuStatus'),
    chooseLmuExecutable: () => invoke('chooseLmuExecutable'),
    chooseLmuProvider: () => invoke('chooseLmuProvider'),
    previewLmuLaunch: (profileId, revision) => invoke('previewLmuLaunch', profileId, revision),
    copyLmuSteamLaunchOption: () => invoke('copyLmuSteamLaunchOption'),
    triggerLmuCapture: runId => invoke('triggerLmuCapture', runId),
    openDataFolder: () => invoke("openDataFolder"),
    openSimQVFR: () => invoke("openSimQVFR"),
    openVectorXR: () => invoke("openVectorXR"),
    openBuyMeACoffee: () => invoke("openBuyMeACoffee"),
    openAffiliateLinks: () => invoke("openAffiliateLinks"),
  }),
);
