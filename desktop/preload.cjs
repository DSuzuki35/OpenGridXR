const { contextBridge, ipcRenderer } = require("electron");
const invoke = (name, ...args) =>
  ipcRenderer.invoke(`opengrid:${name}`, ...args);
contextBridge.exposeInMainWorld(
  "openGrid",
  Object.freeze({
    load: () => invoke("load"),
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
    exportProfile: (profile) => invoke("exportProfile", profile),
    restoreBackup: (id, revision) => invoke("restoreBackup", id, revision),
    runtimeStatus: () => invoke("runtimeStatus"),
    telemetryStatus: (game) => invoke("telemetryStatus", game),
    listOverlayWindows: () => invoke("listOverlayWindows"),
    selectOverlayWindow: (game, id, sourceId) => invoke("selectOverlayWindow", game, id, sourceId),
    overlayStatus: (game) => invoke("overlayStatus", game),
    recenterOverlays: (game) => invoke("recenterOverlays", game),
    centerGame: (game) => invoke("centerGame", game),
    listDisplays: () => invoke("listDisplays"),
    installRuntime: () => invoke("installRuntime"),
    removeRuntime: () => invoke("removeRuntime"),
    launchOutput: (profile) => invoke("launchOutput", profile),
    openDataFolder: () => invoke("openDataFolder"),
    nativeSettings: () => invoke("nativeSettings"),
    chooseNativeSettings: () => invoke("chooseNativeSettings"),
    previewNativeSettings: (values, revision) =>
      invoke("previewNativeSettings", values, revision),
    applyNativeSettings: (token) => invoke("applyNativeSettings", token),
    restoreNativeSettings: (id, revision) =>
      invoke("restoreNativeSettings", id, revision),
    openNativeBackups: () => invoke("openNativeBackups"),
  }),
);
