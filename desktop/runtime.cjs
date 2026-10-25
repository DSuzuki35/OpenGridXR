"use strict";
const fs = require("node:fs/promises");
const path = require("node:path");
const { execFile, spawn } = require("node:child_process");
const { promisify } = require("node:util");
const { createHash } = require("node:crypto");
const run = promisify(execFile);
const { atomicWrite } = require("./store.cjs");
const { toRuntimeIni, toEnhancementsConfig } = require("./runtime-config.cjs");
const REG = "HKCU\\SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit";
const FILES = [
  "OpenGridXR.dll",
  "OpenGridXR.json",
  "OpenGridXR-Output.exe",
  "OpenGridXR-Enhancements.dll",
];
function alive(pid) {
  try {
    process.kill(pid, 0);
    return true;
  } catch {
    return false;
  }
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
function createRuntime({ sourceDir, runtimeDir, testMode = false }) {
  const manifest = path.join(runtimeDir, "OpenGridXR.json");
  let output = null;
  async function queryRegistration() {
    if (process.platform !== "win32") return false;
    try {
      const { stdout } = await run(
        "reg.exe",
        ["query", REG, "/v", manifest, "/reg:64"],
        { windowsHide: true, timeout: 5000 },
      );
      return /REG_DWORD\s+0x0\s*$/m.test(stdout);
    } catch {
      return false;
    }
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
    const session = sessions.find((s) => s.active),
      registered = await queryRegistration();
    let updateRequired = false;
    if (registered && available) {
      for (const file of FILES) {
        try {
          const [packaged, installed] = await Promise.all([
            fs.readFile(path.join(sourceDir, file)),
            fs.readFile(path.join(runtimeDir, file)),
          ]);
          if (
            !createHash("sha256")
              .update(packaged)
              .digest()
              .equals(createHash("sha256").update(installed).digest())
          ) {
            updateRequired = true;
            break;
          }
        } catch {
          updateRequired = true;
          break;
        }
      }
    }
    return {
      available,
      enhancementsAvailable,
      updateRequired,
      registered,
      active: Boolean(session),
      sessions,
      message:
        session?.message ||
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
    for (const game of ["iracing", "lmu"]) {
      await atomicWrite(
        path.join(runtimeDir, `OpenGridXR-enhancements-${game}.json`),
        JSON.stringify(toEnhancementsConfig(doc, game), null, 2) + "\n",
      );
    }
    await atomicWrite(
      path.join(runtimeDir, "OpenGridXR.ini"),
      toRuntimeIni(doc),
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
    await run(
      "reg.exe",
      [
        "add",
        REG,
        "/v",
        manifest,
        "/t",
        "REG_DWORD",
        "/d",
        "0",
        "/f",
        "/reg:64",
      ],
      { windowsHide: true, timeout: 5000 },
    );
    if (!(await queryRegistration()))
      throw new Error("Registration could not be verified.");
    return status();
  }
  async function remove() {
    await ensureGamesClosed();
    if (await queryRegistration())
      await run("reg.exe", ["delete", REG, "/v", manifest, "/f", "/reg:64"], {
        windowsHide: true,
        timeout: 5000,
      });
    return status();
  }
  async function launch(doc, profile, displays) {
    if (output && output.exitCode === null)
      throw new Error(
        "Clean Output is already open. Close it before changing its profile.",
      );
    const native = await status();
    if (!native.registered)
      throw new Error(
        "Install the OpenXR connection in Setup before opening Clean Output.",
      );
    assertSavedOutputProfile(doc, profile);
    if (profile.mode !== "streaming")
      throw new Error("Choose Driving + stream and save the profile first.");
    const args = outputArgs(profile, displays);
    await writeConfig(doc);
    output = spawn(
      path.join(runtimeDir, "OpenGridXR-Output.exe"),
      [
        "--config",
        path.join(runtimeDir, "OpenGridXR.ini"),
        "--game",
        profile.game,
        ...args,
      ],
      { cwd: runtimeDir, windowsHide: false, stdio: "ignore" },
    );
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
  return { status, sessionsStatus, install, remove, writeConfig, launch, ensureGamesClosed };
}
module.exports = {
  createRuntime,
  sessionState,
  validateManifest,
  outputArgs,
  assertSavedOutputProfile,
};
