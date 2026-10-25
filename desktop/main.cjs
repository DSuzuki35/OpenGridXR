"use strict";
const {
  app,
  BrowserWindow,
  ipcMain,
  dialog,
  shell,
  screen,
} = require("electron");
const path = require("node:path");
const fs = require("node:fs/promises");
const { pathToFileURL } = require("node:url");
const { createStore } = require("./store.cjs");
const { createRuntime } = require("./runtime.cjs");
const { createNativeSettings } = require("./native-settings.cjs");
const { atomicWrite } = require("./store.cjs");
const { createTelemetry } = require("./telemetry.cjs");
const { selectedForGame } = require("./runtime-config.cjs");
const { createSpatialOverlays } = require("./spatial-overlays.cjs");
const { createGameControl } = require("./game-control.cjs");
const { createInputHost } = require("./input-host.cjs");
const { createInputController } = require("./input-controller.cjs");
let win;
if (process.env.OPENGRIDXR_DATA_DIR)
  app.setPath("userData", path.resolve(process.env.OPENGRIDXR_DATA_DIR));
const gotLock = app.requestSingleInstanceLock();
if (!gotLock) app.quit();
else {
  app.on("second-instance", () => {
    if (win) {
      if (win.isMinimized()) win.restore();
      win.focus();
    }
  });
  app
    .whenReady()
    .then(async () => {
      app.setName("OpenGrid XR");
      const model = await import(
        pathToFileURL(path.join(__dirname, "../src/model.mjs")).href
      );
      const inputModel = await import(pathToFileURL(path.join(__dirname, "../src/input-bindings.mjs")).href);
      const data = app.getPath("userData"),
        store = await createStore(data);
      const runtimeDir = process.env.OPENGRIDXR_DATA_DIR
        ? path.join(data, "runtime")
        : path.join(process.env.LOCALAPPDATA || data, "OpenGridXR", "runtime");
      const runtime = createRuntime({
        sourceDir: app.isPackaged
          ? path.join(process.resourcesPath, "native")
          : path.join(__dirname, "../native/build/Release"),
        runtimeDir,
        testMode: process.env.OPENGRIDXR_TEST_MODE === "1",
      });
      const testMode = process.env.OPENGRIDXR_TEST_MODE === "1";
      const telemetry = createTelemetry({
        executable: path.join(app.isPackaged ? path.join(process.resourcesPath, "native") : path.join(__dirname, "../native/build/Release"), "OpenGridXR-Telemetry.exe"),
        ...(testMode && process.env.OPENGRIDXR_TELEMETRY_TEST_NAMESPACE ? { testNamespace: process.env.OPENGRIDXR_TELEMETRY_TEST_NAMESPACE } : {}),
      });
      app.once("before-quit", () => telemetry.close());
      // The high-frequency telemetry IPC reads this in-memory snapshot, not profiles on disk.
      let telemetryDocument = (await store.load()).document;
      const helpersDir = app.isPackaged ? path.join(process.resourcesPath, "native") : path.join(__dirname, "../native/build/Release");
      const spatial = await createSpatialOverlays({ electron: require("electron"), executable: path.join(helpersDir, "OpenGridXR-OverlayHost.exe"), getSessions: runtime.sessionsStatus, model, testMode });
      const gameControl = createGameControl({ executable: path.join(helpersDir, "OpenGridXR-Control.exe") });
      const input = createInputController({
        testMode,
        testFixture: testMode && process.env.OPENGRIDXR_INPUT_TEST_FIXTURE === "1",
        createHost: (callbacks) => {
          const fixture = testMode && process.env.OPENGRIDXR_INPUT_TEST_FIXTURE === "1";
          if (fixture) globalThis.opengridInputFixturePress = callbacks.onPress;
          return createInputHost({ ...callbacks, executable: path.join(helpersDir, "OpenGridXR-Input.exe"), testMode, fixture });
        },
        listActions: inputModel.listToggleActions,
        applyAction: async (profileId, actionId) => {
          const before = await store.load();
          if (before.document.selectedProfileId !== profileId) throw new Error("The selected profile changed. Try the button again.");
          const next = structuredClone(before.document);
          const index = next.profiles.findIndex(p => p.id === profileId);
          if (index < 0) throw new Error("The bound profile no longer exists.");
          next.profiles[index] = inputModel.toggleProfileAction(next.profiles[index], actionId);
          model.validateDocument(next);
          const state = await store.save(next, before.revision);
          telemetryDocument = state.document;
          spatial.update(state.document);
          input.update(state.document);
          let pendingNativeError;
          try { await runtime.writeConfig(state.document); }
          catch (error) { pendingNativeError = `Profile saved, but runtime settings could not be updated: ${error.message}`; }
          if (win && !win.isDestroyed()) win.webContents.send("opengrid:inputProfile", { ...state, ...(pendingNativeError ? { pendingNativeError } : {}) });
        },
      });
      input.update(telemetryDocument);
      spatial.update(telemetryDocument);
      app.once("before-quit", () => { input.close(); spatial.close(); gameControl.close(); });
      const nativeBackups = path.join(data, "native-config-backups", "iracing");
      const nativePathFile = path.join(data, "native-settings-path.json");
      let nativePath = testMode
        ? path.join(data, "fixtures", "rendererDX11OpenXR.ini")
        : path.join(
            app.getPath("documents"),
            "iRacing",
            "rendererDX11OpenXR.ini",
          );
      if (!testMode) {
        try {
          const remembered = JSON.parse(
            await fs.readFile(nativePathFile, "utf8"),
          );
          if (
            typeof remembered.path === "string" &&
            path.isAbsolute(remembered.path) &&
            path.basename(remembered.path).toLowerCase() ===
              "rendererdx11openxr.ini"
          )
            nativePath = remembered.path;
        } catch {}
      }
      const nativeSettings = createNativeSettings({
        defaultPath: nativePath,
        backupDir: nativeBackups,
        ensureStopped: testMode ? async () => {} : runtime.ensureGamesClosed,
      });
      const snapshot = async () => ({
        ...(await store.load()),
        runtime: await runtime.status(),
      });
      const displays = () =>
        screen.getAllDisplays().map((d, i) => {
          const b = screen.dipToScreenRect(null, d.bounds);
          return {
            id: String(d.id),
            label: d.label || `Display ${i + 1}`,
            width: b.width,
            height: b.height,
            primary: d.id === screen.getPrimaryDisplay().id,
            bounds: b,
          };
        });
      function handle(name, fn) {
        ipcMain.handle(`opengrid:${name}`, async (event, ...args) => {
          if (
            !win ||
            event.sender !== win.webContents ||
            event.senderFrame !== win.webContents.mainFrame
          )
            throw new Error("Untrusted application frame.");
          return fn(...args);
        });
      }
      handle("load", snapshot);
      handle("inputStatus", () => input.status());
      handle("inputEditorState", (state) => input.editorState(state));
      handle("inputCaptureStart", (actionId, profileId) => input.captureStart(actionId, profileId));
      handle("inputCaptureStatus", () => input.captureStatus());
      handle("inputCaptureCancel", () => input.captureCancel());
      handle("save", async (doc, revision) => {
        model.validateDocument(doc);
        const selected = doc.profiles.find(
          (p) => p.id === doc.selectedProfileId,
        );
        doc = {
          ...doc,
          activeProfiles: {
            ...doc.activeProfiles,
            [selected.game]: selected.id,
          },
        };
        const state = await store.save(doc, revision);
        telemetryDocument = state.document;
        spatial.update(state.document);
        input.update(state.document);
        let pendingNativeError;
        try {
          await runtime.writeConfig(doc);
        } catch (e) {
          pendingNativeError = `Profiles saved, but the runtime configuration could not be updated: ${e.message}`;
        }
        return {
          ...state,
          runtime: await runtime.status(),
          ...(pendingNativeError ? { pendingNativeError } : {}),
        };
      });
      handle("restoreBackup", async (id, revision) => {
        const state = await store.restore(id, revision);
        telemetryDocument = state.document;
        spatial.update(state.document);
        input.update(state.document);
        let pendingNativeError;
        try {
          await runtime.writeConfig(state.document);
        } catch (e) {
          pendingNativeError = `Profiles restored, but the runtime configuration could not be updated: ${e.message}`;
        }
        return {
          ...state,
          runtime: await runtime.status(),
          ...(pendingNativeError ? { pendingNativeError } : {}),
        };
      });
      handle("runtimeStatus", () => runtime.status());
      handle("telemetryStatus", (game) => {
        if (!["iracing", "lmu"].includes(game)) throw new Error("Unsupported telemetry game.");
        return telemetry.status(game, selectedForGame(telemetryDocument, game)?.telemetry);
      });
      handle("listOverlayWindows", () => spatial.listWindows());
      handle("selectOverlayWindow", (game, id, sourceId) => spatial.selectWindow(game, id, sourceId));
      handle("overlayStatus", (game) => spatial.status(game));
      handle("recenterOverlays", (game) => spatial.recenter(game));
      handle("centerGame", (game) => {
        if (testMode) throw new Error("Sending game input is disabled in test mode.");
        return gameControl.center(game, selectedForGame(telemetryDocument, game)?.centering);
      });
      handle("listDisplays", displays);
      handle("nativeSettings", () => nativeSettings.inspect());
      handle("chooseNativeSettings", async () => {
        const choice = await dialog.showOpenDialog(win, {
          title: "Locate iRacing OpenXR configuration",
          defaultPath: nativePath,
          filters: [{ name: "iRacing configuration", extensions: ["ini"] }],
          properties: ["openFile"],
        });
        if (choice.canceled) return null;
        const selected = path.resolve(choice.filePaths[0]);
        if (testMode) {
          const relative = path.relative(data, selected);
          if (relative.startsWith("..") || path.isAbsolute(relative))
            throw new Error("Test mode only permits isolated fixture files.");
        }
        const state = await nativeSettings.choose(selected);
        nativePath = selected;
        if (!testMode)
          await atomicWrite(nativePathFile, JSON.stringify({ path: selected }));
        return state;
      });
      handle("previewNativeSettings", (values, revision) =>
        nativeSettings.preview(values, revision),
      );
      handle("applyNativeSettings", (token) => nativeSettings.apply(token));
      handle("restoreNativeSettings", (id, revision) =>
        nativeSettings.restore(id, revision),
      );
      handle("openNativeBackups", async () => {
        await fs.mkdir(nativeBackups, { recursive: true });
        return shell.openPath(nativeBackups);
      });
      handle("installRuntime", async () => {
        const state = await store.load();
        if (state.recoveryError) throw new Error(state.recoveryError);
        return runtime.install(state.document);
      });
      handle("removeRuntime", () => runtime.remove());
      handle("launchOutput", async (profile) => {
        model.validateProfile(profile);
        const state = await store.load();
        if (state.recoveryError) throw new Error(state.recoveryError);
        return runtime.launch(state.document, profile, displays());
      });
      handle("openDataFolder", () => shell.openPath(data));
      handle("importProfile", async () => {
        const result = await dialog.showOpenDialog(win, {
          title: "Import OpenGrid XR profile",
          filters: [{ name: "OpenGrid XR profile", extensions: ["json"] }],
          properties: ["openFile"],
        });
        if (result.canceled) return null;
        const file = result.filePaths[0];
        if ((await fs.stat(file)).size > 1024 * 1024)
          throw new Error("Profile exceeds 1 MiB.");
        return model.parseProfile(await fs.readFile(file, "utf8"));
      });
      handle("exportProfile", async (profile) => {
        model.validateProfile(profile);
        const result = await dialog.showSaveDialog(win, {
          title: "Export OpenGrid XR profile",
          defaultPath: `${profile.name.replace(/[^a-z0-9-_ ]/gi, "")}.opengrid.json`,
          filters: [{ name: "OpenGrid XR profile", extensions: ["json"] }],
        });
        if (result.canceled) return false;
        await fs.writeFile(
          result.filePath,
          JSON.stringify({ version: 1, profile }, null, 2) + "\n",
          "utf8",
        );
        return true;
      });
      win = new BrowserWindow({
        width: 1500,
        height: 980,
        minWidth: 1050,
        minHeight: 720,
        backgroundColor: "#0f0f12",
        title: "OpenGrid XR",
        icon: path.join(__dirname, "../dist/app.ico"),
        autoHideMenuBar: true,
        show: false,
        webPreferences: {
          preload: path.join(__dirname, "preload.cjs"),
          contextIsolation: true,
          nodeIntegration: false,
          sandbox: true,
        },
      });
      win.webContents.setWindowOpenHandler(() => ({ action: "deny" }));
      win.webContents.on("will-navigate", (event) => event.preventDefault());
      win.once("ready-to-show", () => win.show());
      await win.loadFile(path.join(__dirname, "../dist/index.html"));
    })
    .catch((error) => {
      dialog.showErrorBox(
        "OpenGrid XR could not start",
        error.stack || error.message,
      );
      app.quit();
    });
  app.on("window-all-closed", () => app.quit());
}
