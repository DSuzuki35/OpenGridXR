"use strict";

async function ensureUpdateInstall({ isReady, ensureStopped, confirm }) {
  const assertReady = () => { if (!isReady()) throw new Error("Save your profile and finish the current operation before updating."); };
  assertReady();
  await ensureStopped();
  assertReady();
  if (!await confirm()) return false;
  assertReady();
  await ensureStopped();
  assertReady();
  return true;
}

// All updater state lives in the main process. The renderer can only make named choices.
function createUpdateController({ updater, enabled, currentVersion, readPreferences, writePreferences, ensureCanInstall, onChange = () => {} }) {
  let state = { phase: enabled ? "idle" : "unsupported", currentVersion, version: null, percent: 0, prompt: false,
    message: enabled ? "Updates are checked automatically. Downloads and installation need your approval." : "Install the Windows setup package to enable updates." };
  let preferences = {}, initialized = false, manual = false, closed = false, timer, postponed = null, installing = false;
  const listeners = [];
  const status = () => ({ ...state });
  const change = patch => { if (!closed) { state = { ...state, ...patch }; onChange(status()); } };
  const error = value => change({ phase: "error", prompt: false, message: `Update failed: ${String(value?.message || value).slice(0, 400)}` });
  updater.autoDownload = false;
  updater.autoInstallOnAppQuit = false;
  updater.allowPrerelease = false;
  updater.allowDowngrade = false;
  updater.disableWebInstaller = true;
  updater.setFeedURL({ provider: "github", owner: "DSuzuki35", repo: "OpenGridXR", private: false, releaseType: "release" });
  function listen(name, fn) { updater.on(name, fn); listeners.push([name, fn]); }
  listen("error", error);
  listen("update-available", info => {
    if (!enabled || closed || state.phase !== "checking") return;
    change({ phase: "available", version: info.version, percent: 0,
      prompt: manual || (preferences.skippedVersion !== info.version && postponed !== info.version),
      message: `OpenGrid XR ${info.version} is available.` });
  });
  listen("update-not-available", () => { if (state.phase === "checking") change({ phase: "current", version: null, prompt: false, message: "You have the latest stable release." }); });
  listen("download-progress", progress => { if (state.phase === "downloading") change({ percent: Math.max(0, Math.min(100, Number(progress.percent) || 0)) }); });
  listen("update-downloaded", info => {
    if (state.phase === "downloading" && info.version === state.version)
      change({ phase: "downloaded", percent: 100, prompt: true, message: `Version ${info.version} is ready. Save your profile and close both sims and Clean Output before installing.` });
  });
  async function check(userRequested = false) {
    if (!enabled || closed || installing || ["checking", "downloading", "downloaded", "installing"].includes(state.phase)) return status();
    manual = userRequested === true;
    change({ phase: "checking", prompt: false, message: "Checking GitHub Releases…" });
    try {
      if (!initialized) { preferences = await readPreferences() || {}; initialized = true; }
      if (!closed) await updater.checkForUpdates();
    } catch (e) { error(e); }
    return status();
  }
  async function decide(choice) {
    if (!enabled || closed || !["available", "downloaded"].includes(state.phase)) throw new Error("No update is available for this action.");
    if (choice === "later") { postponed = state.version; change({ prompt: false }); }
    else if (choice === "skip" && state.phase === "available") {
      const next = { skippedVersion: state.version };
      await writePreferences(next); preferences = next; change({ prompt: false });
    } else if (choice === "download" && state.phase === "available") {
      change({ phase: "downloading", prompt: true, percent: 0, message: "Downloading the update…" });
      try { await updater.downloadUpdate(); } catch (e) { error(e); }
    } else throw new Error("Invalid update choice.");
    return status();
  }
  async function install() {
    if (!enabled || closed || installing || state.phase !== "downloaded") throw new Error("The update is not ready to install.");
    installing = true;
    try {
      if (!await ensureCanInstall() || closed) return status();
      change({ phase: "installing", prompt: false, message: "Installing and restarting…" });
      updater.quitAndInstall(false, true);
    } finally { installing = false; }
    return status();
  }
  return { status, check, decide, install,
    start() { if (!enabled || closed || timer) return; void check(); timer = setInterval(() => void check(), 6 * 60 * 60 * 1000); timer.unref?.(); },
    close() { closed = true; clearInterval(timer); for (const [name, fn] of listeners) updater.removeListener(name, fn); },
  };
}
module.exports = { createUpdateController, ensureUpdateInstall };
