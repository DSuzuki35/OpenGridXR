'use strict';
const fs = require('node:fs/promises');
const path = require('node:path');
const {pathToFileURL} = require('node:url');
const {createStore,atomicWrite} = require('./store.cjs');
const {createModuleSettings} = require('./module-settings.cjs');
const {createRuntime} = require('./runtime.cjs');
const {selectedForGame,toRuntimeIni,toEnhancementsConfig} = require('./runtime-config.cjs');
const {createLmuRendering} = require('./lmu-rendering.cjs');

async function launchSavedSteam({store,moduleSettings,runtime,rendering,runtimeDir,args,effectiveDocument,reloadModules=async()=>moduleSettings.get()}) {
  const state = await store.load();
  if (!moduleSettings.get().enabled.lmuRendering) throw new Error('LMU rendering is disabled. Enable it in OpenGrid or remove the OpenGrid Steam launch option.');
  const document = effectiveDocument(state.document,moduleSettings.get().enabled);
  const profile = selectedForGame(document,'lmu');
  if (!profile || !profile.lmuRendering || profile.lmuRendering.mode === 'off')
    throw new Error('Save an active LMU quad-view or stereo VRS profile in OpenGrid first.');
  const expected = toRuntimeIni(document);
  const ensureStopped = async () => {
    await runtime.ensureGamesClosed();
    if (!(await runtime.status()).registered) throw new Error('Install the OpenGrid runtime layer before launching through Steam.');
    if ((await store.load()).revision !== state.revision || JSON.stringify((await reloadModules()).enabled) !== JSON.stringify(moduleSettings.get().enabled))
      throw new Error('Saved settings changed during launch. Start LMU again.');
    if (await fs.readFile(path.join(runtimeDir,'OpenGridXR.ini'),'utf8') !== expected)
      throw new Error('Runtime settings do not match the saved profile. Open OpenGrid, save the profile, then launch from Steam again.');
    const engine = JSON.parse(await fs.readFile(path.join(runtimeDir,'OpenGridXR-enhancements-lmu.json'),'utf8'));
    if(JSON.stringify(engine)!==JSON.stringify(toEnhancementsConfig(document,'lmu')))
      throw new Error('Enhancement settings do not match the saved profile. Save in OpenGrid and launch again.');
  };
  await ensureStopped();
  return rendering.launch(profile,{protectedArgs:args,ensureStopped});
}

async function runSteamEntry({app,args}) {
  if (process.env.OPENGRIDXR_TEST_MODE === '1') throw new Error('Steam game launching is disabled in test mode.');
  const dataDir = app.getPath('userData');
  const runtimeDir = process.env.OPENGRIDXR_DATA_DIR ? path.join(dataDir,'runtime') : path.join(process.env.LOCALAPPDATA || dataDir,'OpenGridXR','runtime');
  const {effectiveDocument} = await import(pathToFileURL(path.join(__dirname,'../src/modules.mjs')).href);
  const store = await createStore(dataDir), moduleSettings = await createModuleSettings(dataDir);
  const runtime = createRuntime({runtimeDir,sourceDir:app.isPackaged?path.join(process.resourcesPath,'native'):path.join(__dirname,'../native/build/Release')});
  const rendering = createLmuRendering({dataDir,runtimeDir});
  // A distinct lock protects simultaneous Steam Play requests; no editor/profile writes.
  const lock = path.join(dataDir,'lmu-steam-launch.lock');
  let handle;
  try { handle = await fs.open(lock,'wx'); }
  catch(error) { if(error.code==='EEXIST')throw new Error('Another Steam launch is being prepared. If no OpenGrid launch process is running, remove lmu-steam-launch.lock from the OpenGrid data folder.');throw error; }
  try {
    const result = await launchSavedSteam({store,moduleSettings,runtime,rendering,runtimeDir,args,effectiveDocument,reloadModules:async()=>(await createModuleSettings(dataDir)).get()});
    await atomicWrite(path.join(dataDir,'lmu-steam-last-launch.json'),JSON.stringify({success:true,...result},null,2));
    return result;
  } catch(error) {
    await atomicWrite(path.join(dataDir,'lmu-steam-last-launch.json'),JSON.stringify({success:false,error:error.message},null,2)).catch(()=>{});
    throw error;
  } finally { await handle.close(); await fs.unlink(lock).catch(()=>{}); }
}
module.exports = {runSteamEntry,launchSavedSteam};
