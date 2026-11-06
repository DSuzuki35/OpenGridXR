"use strict";
const {
  app,
  BrowserWindow,
  ipcMain,
  dialog,
  shell,
  screen,
  clipboard,
} = require("electron");
const path = require("node:path");
const fs = require("node:fs/promises");
const { pathToFileURL } = require("node:url");
const { createStore } = require("./store.cjs");
const { createRuntime } = require("./runtime.cjs");
const { createModuleSettings } = require("./module-settings.cjs");
const { createModuleServices } = require("./module-services.cjs");
const { atomicWrite } = require("./store.cjs");
const { selectedForGame } = require("./runtime-config.cjs");
const { createGameControl } = require("./game-control.cjs");
const { createLiveControls, isLiveAction } = require("./live-controls.cjs");
const { createUpdateController, ensureUpdateInstall } = require("./update-controller.cjs");
const { autoUpdater } = require("electron-updater");
const { launchLmuOffline } = require("./lmu-offline.cjs");
const { createLmuRendering } = require('./lmu-rendering.cjs');
let win;
if (process.env.OPENGRIDXR_DATA_DIR)
  app.setPath("userData", path.resolve(process.env.OPENGRIDXR_DATA_DIR));
const steamEntry = process.argv.indexOf('--lmu-steam-launch');
if (steamEntry !== -1) {
  // This headless handoff must run even while the profile editor owns its lock.
  app.whenReady().then(() => require('./lmu-steam-entry.cjs').runSteamEntry({app,args:process.argv.slice(steamEntry+1)}))
    .then(() => app.exit(0)).catch(error => {
      if (process.env.OPENGRIDXR_TEST_MODE === '1') console.error(error.message);
      else dialog.showErrorBox('OpenGrid Steam launch stopped', error.message);
      app.exit(1);
    });
} else {
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
      const data = app.getPath("userData");
      const moduleSettings = await createModuleSettings(data);
      const store = await createStore(data,{helmetModuleEnabled:()=>moduleSettings.get().enabled.helmet === true});
      const { effectiveDocument, moduleForAction } = await import(pathToFileURL(path.join(__dirname, "../src/modules.mjs")).href);
      const enabled = id => moduleSettings.get().enabled[id] === true;
      const effective = doc => effectiveDocument(doc, moduleSettings.get().enabled);
      const requireModule = id => { if (!enabled(id)) throw new Error(`The ${id} module is disabled in Settings.`); };
      let operationQueue = Promise.resolve(), modulesChanging = false;
      const serialize = operation => {
        const result = operationQueue.then(operation);
        operationQueue = result.catch(() => {});
        return result;
      };
      const runtimeDir = process.env.OPENGRIDXR_DATA_DIR
        ? path.join(data, "runtime")
        : path.join(process.env.LOCALAPPDATA || data, "OpenGridXR", "runtime");
      const testMode = process.env.OPENGRIDXR_TEST_MODE === "1";
      const lmuRendering = createLmuRendering({dataDir:data,runtimeDir,...(testMode?{discover:async()=>({executables:[],providers:[]}),registered:async()=>[]}: {})});
      const runtime = createRuntime({
        dataDir: data,
        sourceDir: app.isPackaged
          ? path.join(process.resourcesPath, "native")
          : path.join(__dirname, "../native/build/Release"),
        runtimeDir,
        testMode: process.env.OPENGRIDXR_TEST_MODE === "1",
        transformDocument: effective,
        outputEnabled: () => enabled('stream'),
        getLmuPreferences: () => lmuRendering.preferences(),
        prepareAssets: async doc => {
          if (["iracing", "lmu"].some(game => {
            const profile = selectedForGame(doc, game);
            return profile?.helmet?.enabled || profile?.stream?.helmet?.enabled;
          }))
            return require('./helmet-assets.cjs').prepareHelmetAssets(doc, { runtimeDir, assetDir: path.join(__dirname, "../dist/helmets"), BrowserWindow });
          // Disable old sidecars without importing the raster preparation module.
          for (const game of ["iracing", "lmu"])
            for (const kind of ["helmet", "stream-helmet"])
              await atomicWrite(path.join(runtimeDir, `OpenGridXR-${kind}-${game}.json`), JSON.stringify({ version: 1, enabled: false }));
        },
      });
      let editorStatus = { dirty: true, busy: true };
      let editorGeneration = 0;
      const updatePreferences = path.join(data, "update-preferences.json");
      const installed = app.isPackaged && await fs.access(path.join(path.dirname(app.getPath("exe")), "opengrid-installed")).then(() => true, () => false);
      const updates = createUpdateController({
        updater: autoUpdater, enabled: installed && !testMode, currentVersion: app.getVersion(),
        readPreferences: async () => { try { return JSON.parse(await fs.readFile(updatePreferences, "utf8")); } catch { return {}; } },
        writePreferences: value => atomicWrite(updatePreferences, JSON.stringify(value)),
        onChange: value => { if (win && !win.isDestroyed()) win.webContents.send("opengrid:updateStatus", value); },
        ensureCanInstall: () => ensureUpdateInstall({
          isReady: () => win && !win.isDestroyed() && !win.webContents.isDestroyed() && !editorStatus.dirty && !editorStatus.busy,
          ensureStopped: runtime.ensureGamesClosed,
          confirm: async () => {
            const result = await dialog.showMessageBox(win, { type: "question", title: "Install OpenGrid XR update", message: "Install the downloaded update and restart OpenGrid XR?", detail: "Your saved profiles and backups are preserved. After restarting, use Setup to update the runtime layer with both sims closed.", buttons: ["Cancel", "Install and restart"], defaultId: 0, cancelId: 0, noLink: true });
            return result.response === 1;
          },
        }),
      });
      app.once("before-quit", () => updates.close());
      // The high-frequency telemetry IPC reads this in-memory snapshot, not profiles on disk.
      let telemetryDocument = (await store.load()).document;
      const helpersDir = app.isPackaged ? path.join(process.resourcesPath, "native") : path.join(__dirname, "../native/build/Release");
      // Load the editor only when its page is opened; startup never enumerates or
      // changes third-party registrations. Test mode cannot reach the real helper.
      let layerEditor;
      const layers = () => layerEditor ||= require('./layers.cjs').createLayerEditor({
        dataDir:data, executable:path.join(helpersDir,'OpenGridXR-Layers.exe'),
        ensureGamesClosed:runtime.ensureGamesClosed,
        ...(testMode ? {run:async args=>{if(args[0]==='--inventory')return {entries:[],errors:[]};throw Error('Layer changes are disabled in test mode.');}} : {}),
      });
      const gameControl = createGameControl({ executable: path.join(helpersDir, "OpenGridXR-Control.exe") });
      const liveControls = createLiveControls({
        root: path.join(runtimeDir, 'enhancements-data', 'runtime'),
        getEditorGeneration: () => editorGeneration,
        canApply: (profileId, generation) => !modulesChanging && !editorStatus.dirty && !editorStatus.busy && editorStatus.profileId === profileId && editorGeneration === generation,
      });
      const menuRuntime=new Map();
      function menuRuntimeStatus(profile){
        let entry=menuRuntime.get(profile.game);
        if(!entry){entry={at:0,pending:false};menuRuntime.set(profile.game,entry);}
        if(!entry.pending&&Date.now()-entry.at>=1500){
          entry.pending=true;
          Promise.allSettled([liveControls.status(profile.game),enabled('colorCorrection')&&profile.colorCorrection?.enabled?services.get('colorCorrection').then(c=>c.status(profile.game)):Promise.resolve({available:false})]).then(([live,color])=>{
            entry.live=live.status==='fulfilled'?live.value:{available:false};entry.color=color.status==='fulfilled'?color.value:{available:false};entry.at=Date.now();
          }).finally(()=>{entry.pending=false;services.peek('spatial')?.refreshMenus();});
        }
        return Date.now()-entry.at<=3000?{live:entry.live,color:entry.color}:{};
      }
      const services = createModuleServices({ isEnabled: enabled, factories: {
        colorCorrection: () => require('./color-correction.cjs').createColorCorrection({
          executable:path.join(helpersDir,'OpenGridXR-Control.exe'),testMode,
        }),
        telemetry: () => require('./telemetry.cjs').createTelemetry({
          executable: path.join(helpersDir, 'OpenGridXR-Telemetry.exe'),
          ...(testMode && process.env.OPENGRIDXR_TELEMETRY_TEST_NAMESPACE ? { testNamespace: process.env.OPENGRIDXR_TELEMETRY_TEST_NAMESPACE } : {}),
        }),
        benchmark: () => require('./benchmark-controller.cjs').createBenchmarkController({
          logDirectory: path.join(data,'benchmarks'),
          root: path.join(runtimeDir, 'enhancements-data', 'runtime'),
          telemetry: async game => (await services.get('telemetry')).status(game, {enabled:true,updateHz:20}),
          onReport: report => atomicWrite(path.join(data,'benchmarks',`benchmark-${new Date(report.startedAtMs).toISOString().replace(/[:.]/g,'-')}.json`),JSON.stringify(report)),
        }),
        spatial: async () => {
          const controller = await require('./spatial-overlays.cjs').createSpatialOverlays({ electron: require('electron'), executable: path.join(helpersDir, 'OpenGridXR-OverlayHost.exe'), getSessions: runtime.sessionsStatus, model, testMode,
            getMenuState:({profileId})=>{
              const profile=effective(telemetryDocument).profiles.find(p=>p.id===profileId);
              const blocked=modulesChanging||editorStatus.dirty||editorStatus.busy||editorStatus.profileId!==profileId||telemetryDocument.selectedProfileId!==profileId;
              const actions=profile?require('./quick-menu.cjs').menuActions(profile,moduleSettings.get().enabled,menuRuntimeStatus(profile)):[];
              return {profileName:profile?.name||'Unavailable profile',game:profile?.game||'',busy:blocked,message:blocked?'Save or discard editor changes to use the menu.':'Select with your bound key or controller button.',actions};
            },
            onMenuAction:({game,profileId,id,actionId,canApply})=>{
              if(actionId==='recenter')return serialize(()=>{
                if(!canApply()||modulesChanging||editorStatus.dirty||editorStatus.busy||editorStatus.profileId!==profileId||!enabled('spatial'))throw Error('The menu source changed. Select again.');
                return services.peek('spatial').recenter(game);
              });
              return applyProfileAction(profileId,actionId==='close'?`spatialOverlays.${id}.enabled`:actionId,{requireBindings:false,guard:canApply});
            },
          });
          controller.update(effective(telemetryDocument));
          return controller;
        },
        bindings: () => {
          const controller = require('./input-controller.cjs').createInputController({
            testMode,
            testFixture: testMode && process.env.OPENGRIDXR_INPUT_TEST_FIXTURE === "1",
            createHost: (callbacks) => {
              const fixture = testMode && process.env.OPENGRIDXR_INPUT_TEST_FIXTURE === "1";
              if (fixture) globalThis.opengridInputFixturePress = callbacks.onPress;
              return require('./input-host.cjs').createInputHost({ ...callbacks, executable: path.join(helpersDir, "OpenGridXR-Input.exe"), testMode, fixture });
            },
            listActions: profile => inputModel.listToggleActions(profile).map(action => ({...action, disabled: action.disabled || !enabled(moduleForAction(action.id))})),
            applyAction: applyProfileAction,
            applyHoldAction: (profileId, actionId, held, pressId) => desktopPrecision.apply(profileId, actionId, held, pressId),
          });
          controller.update(effective(telemetryDocument));
          if (typeof editorStatus.profileId === 'string') controller.editorState(editorStatus);
          return controller;
        },
      }});
      const desktopPrecision = require('./desktop-precision.cjs').createDesktopPrecision({
        getDocument: () => telemetryDocument,
        getEditor: () => ({...editorStatus, busy:editorStatus.busy || modulesChanging}),
        isEnabled: enabled,
        getSpatial: () => services.peek('spatial'),
        testMode,
      });
      function applyProfileAction(profileId, actionId, {requireBindings=true,guard=()=>true}={}) {
        const actionEditorGeneration=editorGeneration;
        let menuSelection;
        const select=/^spatialOverlays\.([0-9a-f-]{36})\.leftClick$/i.exec(actionId);
        const selectedProfile=telemetryDocument.profiles.find(p=>p.id===profileId);
        if(select&&selectedProfile?.spatialOverlays?.items.some(item=>item.id===select[1]&&item.kind==='menu')){
          try{
            const spatial=services.peek('spatial');if(!spatial)throw Error('Show the headset menu first.');
            menuSelection=spatial.prepareMenuSelect(selectedProfile.game,select[1]);
          }catch(error){return Promise.reject(error);}
        }
        return serialize(async()=>{
              if(!guard())throw Error('The menu source changed. Select again.');
              if(requireBindings) requireModule('bindings');
              const actionModule = moduleForAction(actionId);
              if (actionModule) requireModule(actionModule);
              if (modulesChanging || editorStatus.dirty || editorStatus.busy) throw new Error('Save or discard profile edits before using bindings.');
              const before = await store.load();
              if(!guard()||editorGeneration!==actionEditorGeneration||editorStatus.dirty||editorStatus.busy||modulesChanging) throw new Error('The editor or menu source changed. Try the action again.');
              if (before.document.selectedProfileId !== profileId) throw new Error("The selected profile changed. Try the button again.");
              if (actionId === 'colorCorrection.cycle') {
                const selected = effective(before.document).profiles.find(p => p.id === profileId);
                if (!selected?.colorCorrection?.enabled) throw new Error('Enable color correction and save before starting the game.');
                const controller = await services.get('colorCorrection');
                return controller.cycle(selected.game,{canApply:()=>guard() && enabled('colorCorrection') && (!requireBindings || enabled('bindings')) &&
                  !modulesChanging && !editorStatus.dirty && !editorStatus.busy && editorStatus.profileId === profileId && editorGeneration === actionEditorGeneration});
              }
              if (actionId === 'centering.game') {
                if (testMode) throw new Error("Sending game input is disabled in test mode.");
                const selected = before.document.profiles.find(p => p.id === profileId);
                if (!selected) throw new Error("The bound profile no longer exists.");
                const canApply = () => guard() && enabled('bindings') && !modulesChanging && !editorStatus.dirty && !editorStatus.busy && editorStatus.profileId === profileId && editorGeneration === actionEditorGeneration;
                if (!canApply()) throw new Error('Save or discard profile edits before using bindings.');
                return gameControl.center(selected.game, selected.centering, { canApply });
              }
              const desktopClick = /^spatialOverlays\.([0-9a-f-]{36})\.(leftClick|rightClick|resume)$/i.exec(actionId);
              if (desktopClick) {
                if(editorGeneration!==actionEditorGeneration||editorStatus.dirty||editorStatus.busy) throw new Error('The editor changed. Try the action again.');
                const selected=effective(before.document).profiles.find(p=>p.id===profileId);
                const item=selected?.spatialOverlays?.items.find(o=>o.id===desktopClick[1]);
                if(testMode) throw new Error('Native source interaction is disabled in test mode.');
                const spatial=await services.get('spatial');
                if(!guard()||editorGeneration!==actionEditorGeneration||editorStatus.dirty||editorStatus.busy||modulesChanging||!enabled('spatial')) throw new Error('The editor changed. Try the action again.');
                if(item?.kind==='menu'){
                  if(desktopClick[2]!=='leftClick')throw Error('Menu supports selection only.');
                  return spatial.menuSelect(selected.game,desktopClick[1],menuSelection);
                }
                return spatial.desktopAction(selected.game,desktopClick[1],desktopClick[2]);
              }
              if (isLiveAction(actionId)) {
                const selected = before.document.profiles.find(p => p.id === profileId);
                if (!selected) throw new Error("The bound profile no longer exists.");
                return liveControls.execute(effective(before.document).profiles.find(p => p.id === profileId), actionId, actionEditorGeneration,{canApply:()=>guard()&&(!requireBindings||enabled('bindings'))});
              }
              const next = structuredClone(before.document);
              const index = next.profiles.findIndex(p => p.id === profileId);
              if (index < 0) throw new Error("The bound profile no longer exists.");
              next.profiles[index] = inputModel.toggleProfileAction(next.profiles[index], actionId);
              model.validateDocument(next);
              if(!guard())throw Error('The menu source changed. Select again.');
              const state = await store.save(next, before.revision,{beforeReplace:()=>{
                if(!guard()||editorGeneration!==actionEditorGeneration||editorStatus.dirty||editorStatus.busy||modulesChanging)throw Error('The editor or menu source changed before applying the action.');
              }});
              telemetryDocument = state.document;
              await synchronizeServices();
              let pendingNativeError;
              try { await runtime.writeConfig(state.document); }
              catch (error) { pendingNativeError = `Profile saved, but runtime settings could not be updated: ${error.message}`; }
              if (win && !win.isDestroyed()) win.webContents.send("opengrid:inputProfile", { ...state, ...(pendingNativeError ? { pendingNativeError } : {}) });
        });
      }
      const desktopShortcuts = require('./desktop-shortcuts.cjs').createDesktopShortcuts({
        globalShortcut: require('electron').globalShortcut,
        editorState: () => ({...editorStatus,busy:editorStatus.busy||modulesChanging}),
        applyAction: (profileId,actionId) => applyProfileAction(profileId,actionId,{requireBindings:false}),
      });
      async function synchronizeServices() {
        const doc = effective(telemetryDocument);
        const selected = doc.profiles.find(p => p.id === doc.selectedProfileId);
        const spatialNeeded = enabled('spatial') && ['iracing', 'lmu'].some(game => {
          const settings = selectedForGame(doc, game)?.spatialOverlays;
          return settings?.enabled && settings.items.some(item => item.enabled);
        });
        if (!enabled('benchmark')) await services.close('benchmark');
        if (!enabled('colorCorrection')) await services.close('colorCorrection');
        if (!enabled('telemetry')) await services.close('telemetry');
        else if (services.peek('telemetry') && !services.peek('benchmark')?.status().running) {
          // Profile switches and disabling a profile stop its helper immediately.
          for (const game of ['iracing', 'lmu']) if (!selectedForGame(doc, game)?.telemetry?.enabled)
            await services.peek('telemetry').status(game, {enabled:false,updateHz:20});
        }
        if (spatialNeeded) (await services.get('spatial')).update(doc);
        else if (enabled('spatial')) services.peek('spatial')?.update(doc);
        else await services.close('spatial');
        const inputNeeded = enabled('bindings') && selected?.inputBindings?.enabled && selected.inputBindings.bindings.some(binding => enabled(moduleForAction(binding.actionId)));
        if (inputNeeded) (await services.get('bindings')).update(doc);
        else await services.close('bindings');
        desktopShortcuts.update(selected,{enabled:!testMode&&enabled('spatial')});
        if (!enabled('stream')) runtime.closeOutput();
      }
      await synchronizeServices();
      let closing = false;
      app.on('before-quit', event => {
        if (closing) return;
        event.preventDefault(); closing = true;
        desktopShortcuts.close(); desktopPrecision.close(); gameControl.close(); runtime.closeOutput();
        void services.closeAll().finally(() => app.quit());
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
        const mutations = new Set(['setLmuPreferences', 'setModuleSettings', 'save', 'restoreBackup', 'installRuntime', 'removeRuntime', 'launchOutput', 'benchmarkStart', 'benchmarkStop', 'launchLmuOffline', 'chooseLmuExecutable', 'chooseLmuProvider', 'triggerLmuCapture', 'previewLmuLaunch', 'layersSetEnabled', 'layersRestore']);
        ipcMain.handle(`opengrid:${name}`, async (event, ...args) => {
          if (
            !win ||
            event.sender !== win.webContents ||
            event.senderFrame !== win.webContents.mainFrame
          )
            throw new Error("Untrusted application frame.");
          return mutations.has(name) ? serialize(() => fn(...args)) : fn(...args);
        });
      }
      ipcMain.handle('opengrid:quickMenuAction',(event,actionId)=>{
        requireModule('spatial');
        const spatial=services.peek('spatial');
        if(!spatial)throw Error('No active headset menu.');
        return spatial.menuAction(event,actionId);
      });
      handle("load", snapshot);
      let systemFonts,visorTemplates;
      handle('listSystemFonts',options=>(systemFonts ||= require('./system-fonts.cjs').createSystemFonts()).list(options));
      handle('downloadVisorTemplate',request=>(visorTemplates ||= require('./visor-templates.cjs').createVisorTemplates({
        assetDirectory:path.join(__dirname,'../dist/templates/visor'),showSaveDialog:options=>dialog.showSaveDialog(win,options),
      })).download(request));
      handle('layersInventory',()=>layers().inventory());
      handle('layersSetEnabled',request=>layers().setEnabled(request));
      handle('layersRestore',request=>layers().restore(request));
      handle('getModuleSettings', () => moduleSettings.get());
      handle('setModuleSettings', async value => {
        const generation = editorGeneration;
        const checkEditor = () => {
          if (editorStatus.dirty || editorStatus.busy || generation !== editorGeneration)
            throw new Error('Save or discard profile edits and finish the current operation before changing modules.');
        };
        checkEditor();
        modulesChanging = true;
        try {
          const settings = await moduleSettings.save(value, checkEditor);
          await synchronizeServices();
          let pendingNativeError;
          try { await runtime.writeConfig(telemetryDocument); }
          catch (error) { pendingNativeError = `Modules saved, but runtime settings could not be updated: ${error.message}`; }
          return {...settings, restartRequired:true, ...(pendingNativeError ? {pendingNativeError} : {})};
        } finally { modulesChanging = false; }
      });
      let lmuLaunchPending = false;
      let lmuExecutable;
      handle('lmuStatus', async () => {
        const [status,sessions,durableDiagnostics]=await Promise.all([lmuRendering.status(),runtime.sessionsStatus(),runtime.lmuDiagnosticsStatus()]);
        const session=sessions.find(s=>s.game==='lmu'&&s.active)?.lmuRendering||null;
        // An active session owns its hook state; a prior failed launch cannot
        // replace that session's snapshot even if its file is newer.
        const diagnostics=session ? session.diagnostics||null : durableDiagnostics;
        return {...status,session,diagnostics};
      });
      handle('setLmuPreferences',async value=>{
        requireModule('lmuRendering');
        const result=await lmuRendering.setPreferences(value);
        await runtime.writeConfig((await store.load()).document);
        return {...result,message:'CPU preference saved for this PC. Restart LMU to apply.'};
      });
      const chooseLmuPath = async kind => {
        if(testMode)throw new Error('LMU file selection is disabled in test mode.');
        requireModule('lmuRendering');
        const state=await lmuRendering.status();
        const choice=await dialog.showOpenDialog(win,{title:kind==='executable'?'Choose Le Mans Ultimate.exe':'Choose the QVFR OpenXR manifest',defaultPath:state.paths[kind]||undefined,properties:['openFile'],filters:[{name:kind==='executable'?'LMU executable':'QVFR manifest',extensions:[kind==='executable'?'exe':'json']}]});
        if(choice.canceled||!choice.filePaths.length)return null;
        return lmuRendering.setPath(kind,choice.filePaths[0]);
      };
      handle('chooseLmuExecutable',()=>chooseLmuPath('executable'));
      handle('chooseLmuProvider',()=>chooseLmuPath('qvfrManifest'));
      const savedLmuProfile=async (profileId,revision)=>{
        if(editorStatus.dirty)throw new Error('Save your profile before preparing an LMU launch.');
        const state=await store.load();
        if(revision!==state.revision)throw new Error('Profiles changed. Reload before launching LMU.');
        const profile=effective(state.document).profiles.find(p=>p.id===profileId);
        if(!profile||profile.game!=='lmu'||selectedForGame(state.document,'lmu')?.id!==profile.id)throw new Error('Select and save the LMU profile before launching.');
        return profile;
      };
      handle('previewLmuLaunch',async(profileId,revision)=>{requireModule('lmuRendering');return lmuRendering.preview(await savedLmuProfile(profileId,revision));});
      handle('copyLmuSteamLaunchOption',async()=>{
        requireModule('lmuRendering');
        if(!app.isPackaged)throw new Error('Install the packaged OpenGrid app before configuring Steam.');
        const option=require('./lmu-steam.cjs').steamLaunchOption(app.getPath('exe'));
        clipboard.writeText(option);
        return {option,message:'Copied. Paste into Steam > Le Mans Ultimate > Properties > General > Launch Options. Keep the normal EAC-enabled launch selected.'};
      });
      handle('triggerLmuCapture',async runId=>{if(testMode)throw new Error('LMU capture is disabled in test mode.');requireModule('lmuRendering');return lmuRendering.triggerCapture(runId);});
      handle("launchLmuOffline", async (profileId, revision, options = {}) => {
        if (testMode) throw new Error("Launching LMU is disabled in test mode.");
        if (lmuLaunchPending) throw new Error("An LMU launch is already pending.");
        lmuLaunchPending = true;
        try {
          const checkReady = async () => {
            if (editorStatus.dirty) throw new Error("Save your profile before testing in LMU.");
            if (!(await runtime.status()).registered) throw new Error("Install the runtime layer before testing in LMU.");
            await runtime.ensureGamesClosed();
            if (editorStatus.dirty) throw new Error("Save your profile before testing in LMU.");
          };
          await checkReady();
          if(profileId!==undefined){
            requireModule('lmuRendering');
            if(!options||typeof options!=='object'||Array.isArray(options)||Object.keys(options).some(k=>k!=='capture'))throw new Error('Invalid LMU launch options.');
            const profile=await savedLmuProfile(profileId,revision);
            const state=await store.load();await runtime.writeConfig(state.document);
            return await lmuRendering.launch(profile,{capture:options.capture??false,ensureStopped:checkReady});
          }
          // The existing Setup action also honors the saved, enabled LMU mode.
          const state=await store.load(),profile=selectedForGame(effective(state.document),'lmu');
          if(profile?.lmuRendering?.mode&&profile.lmuRendering.mode!=='off'){
            await runtime.writeConfig(state.document);
            return await lmuRendering.launch(profile,{ensureStopped:checkReady});
          }
          const choice = await dialog.showOpenDialog(win, {
            title: "Choose the LMU game executable for offline testing",
            defaultPath: lmuExecutable,
            filters: [{ name: "LMU game executable", extensions: ["exe"] }],
            properties: ["openFile"],
          });
          if (choice.canceled || !choice.filePaths.length) return null;
          const executable = choice.filePaths[0];
          const result = await launchLmuOffline({ executable, ensureStopped: checkReady });
          lmuExecutable = executable;
          return result;
        } finally { lmuLaunchPending = false; }
      });
      handle("updateStatus", () => updates.status());
      handle("checkUpdates", () => updates.check(true));
      handle("updateChoice", choice => updates.decide(choice));
      handle("installUpdate", () => updates.install());
      const idleInput = () => ({active:false,running:false,available:false,devices:[],paused:true,captureState:'idle',message:enabled('bindings')?'No active input bindings.':'Input bindings are disabled in Settings.'});
      handle("inputStatus", () => services.peek('bindings')?.status() || idleInput());
      handle("inputEditorState", (state) => {
        if (!state || typeof state.dirty !== 'boolean' || typeof state.busy !== 'boolean' || typeof state.profileId !== 'string' || state.profileId.length > 128) throw new Error('Invalid input editor state.');
        editorGeneration++;
        editorStatus = { dirty: state?.dirty !== false, busy: state?.busy !== false, profileId: state?.profileId };
        gameControl.cancelIfInvalid();
        return services.peek('bindings')?.editorState(state) || idleInput();
      });
      handle("inputCaptureStart", async (actionId, profileId) => {
        requireModule('bindings');
        const id = moduleForAction(actionId); if (id) requireModule(id);
        return (await services.get('bindings')).captureStart(actionId, profileId);
      });
      handle("inputCaptureStatus", () => services.peek('bindings')?.captureStatus() || {state:'idle'});
      handle("inputCaptureCancel", () => services.peek('bindings')?.captureCancel() || {state:'cancelled'});
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
        await synchronizeServices();
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
        await synchronizeServices();
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
      const benchmarkStatus = () => services.peek('benchmark')?.status() || {running:false,game:null,sessionId:null,message:enabled('benchmark')?'Start a recording in a supported game session.':'Benchmark is disabled in Settings.'};
      handle('benchmarkStatus', benchmarkStatus);
      handle('benchmarkStart',async(options={})=>{
        requireModule('benchmark'); requireModule('telemetry');
        if(editorStatus.dirty||editorStatus.busy)throw new Error('Save or discard profile edits before recording.');
        const current=(await store.load()).document;
        const profile=effective(current).profiles.find(p=>p.id===current.selectedProfileId);
        return (await services.get('benchmark')).start(profile,options);
      });
      handle('benchmarkStop',async()=>{await services.peek('benchmark')?.stop();return benchmarkStatus();});
      handle('benchmarkReport',()=>{
        return services.peek('benchmark')?.report({includeSamples:false}) || null;
      });
      handle('benchmarkExport',async format=>{
        if(!['html','csv'].includes(format))throw new Error('Unsupported benchmark export.');
        const report=services.peek('benchmark')?.report();if(!report)throw new Error('Finish a recording before exporting.');
        const result=await dialog.showSaveDialog(win,{title:'Export benchmark report',defaultPath:`OpenGridXR-${report.game}-benchmark.${format}`,filters:[{name:format==='html'?'HTML report':'CSV data',extensions:[format]}]});
        if(result.canceled||!result.filePath)return {canceled:true};
        const {exportHtml,exportCsv}=require('./benchmark.cjs');
        await atomicWrite(result.filePath,format==='html'?exportHtml(report):exportCsv(report));return {canceled:false};
      });
      handle("telemetryStatus", async (game) => {
        if (!["iracing", "lmu"].includes(game)) throw new Error("Unsupported telemetry game.");
        const benchmark = benchmarkStatus();
        const settings = benchmark.running && benchmark.game===game ? {enabled:true,updateHz:20} : selectedForGame(telemetryDocument,game)?.telemetry;
        if (!enabled('telemetry') || !settings?.enabled) return {available:false,running:false,enabled:false,requestedHz:settings?.updateHz || 20,record:null,message:enabled('telemetry')?'Telemetry is off. Enable and save the profile to connect.':'Telemetry is disabled in Settings.'};
        return (await services.get('telemetry')).status(game,settings);
      });
      handle('listOverlayDisplays', async()=>{requireModule('spatial');return (await services.get('spatial')).listDisplays();});
      handle('colorCorrectionStatus',async game=>{
        if(!['lmu','iracing'].includes(game))throw new Error('Unsupported color-correction game.');
        if(!enabled('colorCorrection'))return {available:false,supported:false,enabled:false,activePreset:null,effectActive:false,error:'Color correction is disabled in Settings.'};
        return (await services.get('colorCorrection')).status(game);
      });
      handle('cycleColorCorrection',async game=>{
        requireModule('colorCorrection');
        if(!['lmu','iracing'].includes(game))throw new Error('Unsupported color-correction game.');
        const state=(await store.load()).document,selected=state.profiles.find(p=>p.id===state.selectedProfileId);
        if(selected?.game!==game)throw new Error('Select the saved game profile before switching color presets.');
        return applyProfileAction(selected.id,'colorCorrection.cycle',{requireBindings:false});
      });
      handle('selectOverlayDisplay', async(game,id,displayId)=>{requireModule('spatial');return (await services.get('spatial')).selectDisplay(game,id,displayId);});
      handle('desktopOverlayAction', async(game,id,action)=>{
        requireModule('spatial');
        if(testMode) throw new Error('Desktop interaction is disabled in test mode.');
        if(!['leftClick','rightClick','resume'].includes(action)) throw new Error('Unknown desktop action.');
        if(editorStatus.dirty||editorStatus.busy||modulesChanging) throw new Error('Save or discard profile edits before desktop interaction.');
        const state=(await store.load()).document;
        const selected=effective(state).profiles.find(p=>p.id===state.selectedProfileId);
        if(selected?.game!==game||!selected.spatialOverlays?.items.some(item=>item.id===id&&item.kind==='desktop')) throw new Error('Select the saved desktop profile first.');
        return applyProfileAction(selected.id,'spatialOverlays.'+id+'.'+action,{requireBindings:false});
      });
      handle("listOverlayWindows", async () => (await services.get('spatial')).listWindows());
      handle("selectOverlayWindow", async (game, id, sourceId) => (await services.get('spatial')).selectWindow(game, id, sourceId));
      handle("overlayStatus", game => ({...(services.peek('spatial')?.status(game) || {available:false,running:false,capacity:0,activeOverlayCount:0,items:[],message:enabled('spatial')?'Save and enable a source to start spatial overlays.':'Spatial overlays are disabled in Settings.'}),shortcutErrors:desktopShortcuts.status().errors}));
      handle("recenterOverlays", async game => (await services.get('spatial')).recenter(game));
      handle("centerGame", (game) => {
        if (testMode) throw new Error("Sending game input is disabled in test mode.");
        return gameControl.center(game, selectedForGame(telemetryDocument, game)?.centering);
      });
      handle("listDisplays", displays);
      handle("installRuntime", async () => {
        const state = await store.load();
        if (state.recoveryError) throw new Error(state.recoveryError);
        return runtime.install(state.document);
      });
      handle("removeRuntime", () => runtime.remove());
      handle("launchOutput", async (profile) => {
        requireModule('stream');
        model.validateProfile(profile);
        const state = await store.load();
        if (state.recoveryError) throw new Error(state.recoveryError);
        return runtime.launch(state.document, profile, displays());
      });
      handle("openDataFolder", () => shell.openPath(data));
      handle("openSimQVFR", () => shell.openExternal("https://github.com/Williem3/Sim-QVFR"));
      handle("openVectorXR", () => shell.openExternal("https://github.com/DienerTech/vectorxr"));
      handle("openBuyMeACoffee", () => shell.openExternal("https://buymeacoffee.com/dansuzuki"));
      handle("openAffiliateLinks", () => shell.openExternal("https://dansuzuki.com/discounts/"));
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
        return model.parseProfile(await fs.readFile(file, "utf8"),{helmetModuleEnabled:enabled('helmet')});
      });
      handle("importSteadyXr", async (game) => {
        if (!["iracing", "lmu"].includes(game)) throw new Error("Choose a supported game for this preset.");
        const result = await dialog.showOpenDialog(win, {
          title: "Import steadyXR preset",
          filters: [{ name: "steadyXR preset", extensions: ["ini"] }],
          properties: ["openFile"],
        });
        if (result.canceled || !result.filePaths[0]) return null;
        const file = result.filePaths[0];
        if ((await fs.stat(file)).size > 1024 * 1024) throw new Error("Preset exceeds 1 MiB.");
        const {decodeSteadyXrPreset, importSteadyXrPreset} = await import(pathToFileURL(path.join(__dirname, "../src/steadyxr-import.mjs")).href);
        return importSteadyXrPreset(decodeSteadyXrPreset(await fs.readFile(file)), {game, name:path.basename(file, path.extname(file))});
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
          JSON.stringify({ version: 1, profile:model.normalizeProfile(profile,{helmetModuleEnabled:enabled('helmet')}) }, null, 2) + "\n",
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
      updates.start();
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
}
