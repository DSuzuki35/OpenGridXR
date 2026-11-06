"use strict";
const fs = require("node:fs/promises");
const path = require("node:path");
const { execFile, spawn } = require("node:child_process");
const { promisify } = require("node:util");
const { createHash } = require("node:crypto");
const run = promisify(execFile);
const { atomicWrite } = require("./store.cjs");
const { createOutputFraming } = require('./output-framing.cjs');
const { toRuntimeIni, toEnhancementsConfig, selectedForGame } = require("./runtime-config.cjs");
const REG = "HKCU\\SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit";
const FILES = [
  "OpenGridXR.dll",
  "OpenGridXR.json",
  "OpenGridXR-Output.exe",
  "OpenGridXR-Enhancements.dll",
  "OpenGridXR-Connection.exe",
];
function alive(pid) {
  try {
    process.kill(pid, 0);
    return true;
  } catch {
    return false;
  }
}
function lmuDiagnosticState(data, now = Date.now(), isAlive = alive) {
  // Lifecycle snapshots have no frame heartbeat. A bounded recent failure
  // survives process exit, explicitly labelled as a previous launch attempt.
  const boundedText = value => typeof value === 'string' && value.length <= 512 && !/[\x00-\x1f\x7f]/.test(value);
  const warning=data?.warning===undefined?'':data.warning;
  const nativeOuterRefusalCount=data?.nativeOuterRefusalCount===undefined?0:data.nativeOuterRefusalCount;
  if (!data || data.version !== 1 || !Number.isSafeInteger(data.pid) || data.pid <= 0 || data.pid > 0xffffffff ||
      !Number.isSafeInteger(data.timestampMs) || now - data.timestampMs < -2000 || now - data.timestampMs > 86400000 ||
      !['starting','ready','unavailable'].includes(data.state) || !boundedText(data.reason) || !boundedText(data.cpuAffinity) ||
      !boundedText(warning) || ![data.reentryCount,data.outerTimeoutCount,nativeOuterRefusalCount].every(n => Number.isSafeInteger(n) && n >= 0)) return null;
  const activeProcess=isAlive(data.pid);
  if (!activeProcess && data.state !== 'unavailable') return null;
  return {version:1,pid:data.pid,timestampMs:data.timestampMs,state:data.state,reason:data.reason,cpuAffinity:data.cpuAffinity,
    reentryCount:data.reentryCount,outerTimeoutCount:data.outerTimeoutCount,warning,nativeOuterRefusalCount,activeProcess};
}
function lmuRenderStatus(data, pid, now, isAlive) {
  if (!data || !["quad", "stereoVrs"].includes(data.mode) ||
      !["native", "qvfr", "vectorxr"].includes(data.provider) ||
      !["starting", "ready", "unavailable"].includes(data.hookStatus) ||
      !Number.isSafeInteger(data.frameId) || data.frameId < 0 ||
      typeof data.focusFovValid !== "boolean") return null;
  const sizes = value => Array.isArray(value) && value.length === 2 && value.every(s =>
    s && [s.width, s.height].every(n => Number.isInteger(n) && n >= 0 && n <= 65536));
  if (!sizes(data.outerSizes) || !sizes(data.focusSizes)) return null;
  const keys = ["focusLeftHorizontal", "focusLeftVertical", "focusRightHorizontal", "focusRightVertical"];
  if (keys.some(key => !Number.isFinite(data[key]) || data[key] < 0 || data[key] >= 180)) return null;
  if (!Array.isArray(data.renderedFov) || data.renderedFov.length > 4) return null;
  if (data.focusFovValid && (data.mode !== "quad" || data.renderedFov.length !== 4 || data.frameId === 0)) return null;
  const fovs = data.renderedFov.map(f => f && Object.fromEntries(["angleLeft", "angleRight", "angleUp", "angleDown"].map(k => [k, f[k]])));
  if (fovs.some(f => !f || Object.values(f).some(n => !Number.isFinite(n) || Math.abs(n) >= Math.PI / 2) || f.angleLeft >= f.angleRight || f.angleDown >= f.angleUp)) return null;
  const diagnostics = data.diagnostics?.pid === pid ? lmuDiagnosticState(data.diagnostics, now, isAlive) : null;
  return {
    mode: data.mode, provider: data.provider, hookStatus: data.hookStatus, frameId: data.frameId,
    focusFovValid: data.focusFovValid, ...Object.fromEntries(keys.map(k => [k, data[k]])),
    outerSizes: data.outerSizes.map(({width,height}) => ({width,height})),
    focusSizes: data.focusSizes.map(({width,height}) => ({width,height})),
    renderedFov: fovs,
    captureState: ["inactive", "ready", "ready-focus", "ready-outer-only", "skipped"].includes(data.captureState) ? data.captureState : "inactive",
    streamingExpansionUnsupported: data.streamingExpansionUnsupported === true,
    ...(diagnostics ? {diagnostics} : {}),
  };
}
function sessionState(data, now = Date.now(), isAlive = alive) {
  const active = Boolean(
    data &&
    data.active === true &&
    Number.isSafeInteger(data.pid) &&
    data.pid > 0 &&
    Number.isFinite(data.updatedUnixMs) &&
    now - data.updatedUnixMs >= -2000 &&
    now - data.updatedUnixMs <= 10000 &&
    isAlive(data.pid),
  );
  return {
    active,
    game: data?.game,
    frames: data?.frames,
    mutableFov: data?.mutableFov,
    captureSupported: data?.captureSupported,
    maskCalls: data?.maskCalls,
    engineLoaded: active && data.engineLoaded === true,
    lmuRendering: active && data.game === "lmu" ? lmuRenderStatus(data.lmuRendering, data.pid, now, isAlive) : null,
    engineError:
      active && typeof data.engineError === "string"
        ? data.engineError.slice(0, 500)
        : "",
    message: active
      ? data.message || "OpenXR session connected."
      : "No active OpenXR session.",
  };
}
function validateManifest(m) {
  if (
    m?.api_layer?.name !== "XR_APILAYER_OPENGRIDXR" ||
    !["OpenGridXR.dll", "./OpenGridXR.dll", ".\\OpenGridXR.dll"].includes(
      m.api_layer.library_path,
    )
  )
    throw new Error("Native manifest is not an owned OpenGrid XR layer.");
}
function outputArgs(profile, displays) {
  const id = profile.stream.displayId || "primary";
  const display =
    id === "primary"
      ? displays.find((d) => d.primary)
      : displays.find((d) => d.id === id);
  if (!display)
    throw new Error(
      "The selected output display is no longer available. Choose a connected monitor.",
    );
  const b = display.bounds;
  if (
    !b ||
    ![b.x, b.y, b.width, b.height].every(Number.isFinite) ||
    b.width < 1 ||
    b.height < 1
  )
    throw new Error("Output display bounds are invalid.");
  return [
    "--display",
    [b.x, b.y, b.width, b.height].map(Math.round).join(","),
    ...(profile.stream.fullscreen !== false ? ["--fullscreen"] : []),
  ];
}
function assertSavedOutputProfile(doc, profile) {
  const saved = doc.profiles.find((p) => p.id === profile.id);
  if (!saved || JSON.stringify(saved) !== JSON.stringify(profile))
    throw new Error("Save this profile before opening Clean Output.");
  const selected =
    doc.profiles.find(
      (p) => p.id === doc.selectedProfileId && p.game === profile.game,
    ) ||
    doc.profiles.find(
      (p) =>
        p.id === doc.activeProfiles?.[profile.game] && p.game === profile.game,
    ) ||
    doc.profiles.find((p) => p.game === profile.game);
  if (selected?.id !== profile.id)
    throw new Error("Save the selected profile before opening Clean Output.");
}
function createRuntime({ sourceDir, runtimeDir, dataDir = path.join(process.env.APPDATA || runtimeDir, 'opengrid-xr'), testMode = false, prepareAssets = async () => {}, transformDocument = doc => doc, outputEnabled = () => true, getLmuPreferences = async () => ({cpuCacheAffinity:false}), lmuRendering = null }) {
  const manifest = path.join(runtimeDir, "OpenGridXR.json");
  let output = null;
  let outputFraming = null;
  // Reuse one LMU service so saves do not repeat registry/Steam discovery.
  const lmu = () => lmuRendering ||= require('./lmu-rendering.cjs').createLmuRendering({dataDir,runtimeDir});
  // status() runs after every save. Include identity and change time so a
  // same-size replacement with a preserved modification time is rehashed.
  const digests = new Map();
  const digestKey = stat => `${stat.dev}:${stat.ino}:${stat.size}:${stat.mtimeNs}:${stat.ctimeNs}`;
  async function digest(file) {
    const key = digestKey(await fs.stat(file, {bigint:true})), cached = digests.get(file);
    if (cached?.key === key) return cached.value;
    const value = createHash("sha256").update(await fs.readFile(file)).digest("hex");
    // A concurrent installation must not seed a stale cache entry.
    if (digestKey(await fs.stat(file, {bigint:true})) === key) digests.set(file, {key, value});
    else digests.delete(file);
    return value;
  }
  async function sameContents(a, b) {
    const [left, right] = await Promise.all([fs.stat(a), fs.stat(b)]);
    if (left.size !== right.size) return false;
    const [x, y] = await Promise.all([digest(a), digest(b)]);
    return x === y;
  }
  async function registrations() {
    const absent = {present:false, enabled:false};
    if (process.platform !== 'win32' || testMode) return {machine:absent, legacyMachine:absent, user:absent};
    const {stdout} = await run(path.join(sourceDir, 'OpenGridXR-Connection.exe'), ['--status', manifest], {windowsHide:true, timeout:5000});
    const state = JSON.parse(stdout);
    if (typeof state.machineDirectory !== 'string' || !path.isAbsolute(state.machineDirectory) ||
        !['machine','legacyMachine','user'].every(key => typeof state[key]?.present === 'boolean' && typeof state[key]?.enabled === 'boolean'))
      throw new Error('Invalid connection status from the native helper.');
    return state;
  }
  async function changeMachineRegistration(operation) {
    // The signed helper elevates itself and installs executable assets under
    // Program Files. No mutation accepts paths from JavaScript.
    try {
      await run(path.join(runtimeDir, 'OpenGridXR-Connection.exe'), [operation], {windowsHide:true});
    } catch (error) {
      const messages = {
        1223: 'Administrator prompt cancelled. The layer registration was not changed.',
        170: 'Close iRacing, Le Mans Ultimate and Clean Output before changing the connection.',
        5: 'Windows denied access to the system-wide OpenGrid registration.',
      };
      throw new Error(messages[error.code] || `OpenGrid registration failed: ${error.message}`);
    }
  }
  async function removeUserRegistration() {
    if ((await registrations()).user.present)
      await run('reg.exe', ['delete', REG, '/v', manifest, '/f', '/reg:64'], {windowsHide:true, timeout:5000});
    if ((await registrations()).user.present) throw new Error('User registration removal could not be verified.');
  }
  async function sessionsStatus() {
    return Promise.all(["iracing", "lmu"].map(async (game) => {
      try {
        const file = path.join(runtimeDir, `OpenGridXR-status-${game}.json`);
        if ((await fs.stat(file)).size > 65536) return { active: false, game };
        return { ...sessionState(JSON.parse(await fs.readFile(file, "utf8"))), game };
      } catch { return { active: false, game }; }
    }));
  }
  async function lmuDiagnosticsStatus() {
    let handle;
    try {
      const file = path.join(runtimeDir, 'OpenGridXR-lmu-diagnostics.json');
      handle = await fs.open(file, 'r');
      if ((await handle.stat()).size > 8192) return null;
      // Also bound the actual read if the file changes after stat.
      const buffer = Buffer.alloc(8193);
      const {bytesRead} = await handle.read(buffer, 0, buffer.length, 0);
      if (bytesRead > 8192) return null;
      return lmuDiagnosticState(JSON.parse(new TextDecoder('utf-8', {fatal:true}).decode(buffer.subarray(0,bytesRead))));
    } catch { return null; }
    finally { await handle?.close().catch(() => {}); }
  }
  async function status() {
    const available = (
      await Promise.all(
        FILES.map((f) =>
          fs.access(path.join(sourceDir, f)).then(
            () => true,
            () => false,
          ),
        ),
      )
    ).every(Boolean);
    const sessions = await sessionsStatus();
    const enhancementsAvailable = await fs
      .access(path.join(sourceDir, "OpenGridXR-Enhancements.dll"))
      .then(
        () => true,
        () => false,
      );
    const session = sessions.find((s) => s.active);
    const {machine, legacyMachine, user, machineDirectory} = await registrations();
    const registered = machine.enabled || legacyMachine.enabled || user.enabled;
    const registrationRepairRequired = legacyMachine.present || user.enabled;
    let updateRequired = registrationRepairRequired;
    if (registered && available) {
      for (const file of FILES) {
        try {
          if (!(await sameContents(path.join(sourceDir, file), path.join(runtimeDir, file)))) {
            updateRequired = true;
            break;
          }
        } catch {
          updateRequired = true;
          break;
        }
      }
      if (machine.enabled && machineDirectory) {
        const executableFiles = ['OpenGridXR.dll', 'OpenGridXR-Enhancements.dll', 'OpenGridXR.json'];
        const provider = path.join('providers','qvfr','OpenGridXR-QVFR.dll');
        if (await fs.access(path.join(runtimeDir,provider)).then(()=>true,()=>false)) executableFiles.push(provider);
        for (const file of executableFiles) {
          try {
            if (!(await sameContents(path.join(runtimeDir,file), path.join(machineDirectory,file)))) {updateRequired=true;break;}
          } catch {updateRequired=true;break;}
        }
      }
    }
    return {
      available,
      enhancementsAvailable,
      updateRequired,
      registered,
      registrationPresent: machine.present || legacyMachine.present || user.present,
      registrationRepairRequired,
      active: Boolean(session),
      sessions,
      message:
        (registrationRepairRequired ? 'Repair the runtime connection: system-wide registration is required for normal LMU launches.' : session?.message) ||
        (registered
          ? "Connection installed · start a fresh OpenXR game session."
          : available
            ? "Native connection ready to install."
            : "Native runtime has not been built yet."),
      ...(session ? { session } : {}),
    };
  }
  async function ensureGamesClosed() {
    if (testMode)
      throw new Error("Runtime installation is disabled in test mode.");
    const { stdout } = await run(
      "powershell.exe",
      [
        "-NoProfile",
        "-NonInteractive",
        "-Command",
        "@(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -in @('iRacingSim64DX11','iRacingSim64DX11_test','Le Mans Ultimate','LeMansUltimate','OpenGridXR-Output') } | Select-Object -ExpandProperty ProcessName) | ConvertTo-Json -Compress",
      ],
      { windowsHide: true, timeout: 10000 },
    );
    if (stdout.trim())
      throw new Error(
        "Close iRacing, Le Mans Ultimate and Clean Output before changing the connection.",
      );
  }
  async function writeConfig(doc) {
    doc = transformDocument(doc);
    const lmuProfile = selectedForGame(doc, 'lmu');
    if (!testMode && lmuProfile?.lmuRendering?.mode === 'quad' && lmuProfile.lmuRendering.provider === 'qvfr')
      await lmu().prepareAutomatic(lmuProfile);
    await prepareAssets(doc);
    for (const game of ["iracing", "lmu"]) {
      await atomicWrite(
        path.join(runtimeDir, `OpenGridXR-enhancements-${game}.json`),
        JSON.stringify(toEnhancementsConfig(doc, game), null, 2) + "\n",
      );
    }
    await atomicWrite(
      path.join(runtimeDir, "OpenGridXR.ini"),
      toRuntimeIni(doc,{lmuPreferences:await getLmuPreferences()}),
    );
  }
  async function install(doc) {
    await ensureGamesClosed();
    const before = await status();
    if (!before.available) throw new Error("Build the native runtime first.");
    validateManifest(
      JSON.parse(
        await fs.readFile(path.join(sourceDir, "OpenGridXR.json"), "utf8"),
      ),
    );
    await fs.mkdir(runtimeDir, { recursive: true });
    for (const f of FILES) {
      const bytes = await fs.readFile(path.join(sourceDir, f));
      const temp = path.join(runtimeDir, `${f}.new`);
      await fs.writeFile(temp, bytes);
      await fs.rename(temp, path.join(runtimeDir, f));
    }
    await writeConfig(doc);
    await changeMachineRegistration('--install');
    if (!(await registrations()).machine.enabled)
      throw new Error("System-wide registration could not be verified.");
    // Keep the legacy working connection until machine registration succeeds.
    await removeUserRegistration();
    return status();
  }
  async function remove() {
    await ensureGamesClosed();
    const before = await registrations();
    if (before.machine.present || before.legacyMachine.present) {
      // Older installations have no helper yet. Copy only our packaged helper
      // so that a repaired 0.4.2 installation can also be removed directly.
      await fs.mkdir(runtimeDir, {recursive:true});
      await fs.copyFile(path.join(sourceDir, 'OpenGridXR-Connection.exe'), path.join(runtimeDir, 'OpenGridXR-Connection.exe'));
      await changeMachineRegistration('--remove');
      const after = await registrations();
      if (after.machine.present || after.legacyMachine.present)
        throw new Error('System-wide registration removal could not be verified.');
    }
    await removeUserRegistration();
    return status();
  }
  async function launch(doc, profile, displays) {
    if (!outputEnabled()) throw new Error("Clean Output is disabled in Settings.");
    if (output && output.exitCode === null)
      throw new Error(
        "Clean Output is already open. Close it before changing its profile.",
      );
    const native = await status();
    if (!native.registered)
      throw new Error(
        "Install the OpenXR connection in Setup before opening Clean Output.",
      );
    if (native.updateRequired)
      throw new Error('Update the OpenXR connection in Setup before opening Clean Output. Close both sims and Clean Output, then update the runtime.');
    assertSavedOutputProfile(doc, profile);
    // Equality uses the saved profile; only runtime preparation sees overrides.
    profile = transformDocument(doc).profiles.find(p => p.id === profile.id);
    const args = outputArgs(profile, displays);
    await writeConfig(doc);
    output = spawn(
      path.join(runtimeDir, "OpenGridXR-Output.exe"),
      [
        "--config",
        path.join(runtimeDir, "OpenGridXR.ini"),
        "--game",
        profile.game,
        "--framing-stdin",
        ...args,
      ],
      { cwd: runtimeDir, windowsHide: false, stdio: ['pipe', 'ignore', 'ignore'] },
    );
    outputFraming?.close();
    const framing = outputFraming = createOutputFraming(output.stdin, profile);
    output.once('exit', () => framing.close());
    await new Promise((resolve, reject) => {
      output.once("spawn", resolve);
      output.once("error", reject);
    });
    output.on("error", () => {});
    return {
      started: true,
      message:
        "Clean Output opened. Start the game in OpenXR mode; restart it after changing render settings.",
    };
  }
  function closeOutput() {
    outputFraming?.close();
    outputFraming = null;
    if (output && output.exitCode === null) output.kill();
    output = null;
  }
  function previewOutputFraming(request, selectedProfileId) {
    if (!request || !outputEnabled()) { outputFraming?.reset(); return false; }
    return outputFraming?.preview(request, selectedProfileId) || false;
  }
  function resetOutputFraming() { outputFraming?.reset(); }
  return { status, sessionsStatus, lmuDiagnosticsStatus, install, remove, writeConfig, launch, closeOutput, ensureGamesClosed,
    previewOutputFraming, resetOutputFraming };
}
module.exports = {
  createRuntime,
  lmuDiagnosticState,
  sessionState,
  validateManifest,
  outputArgs,
  assertSavedOutputProfile,
};
