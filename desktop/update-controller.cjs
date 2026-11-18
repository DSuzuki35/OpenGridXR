"use strict";
const { ChannelGitHubProvider, defaultUpdateChannel } = require("./update-channel-provider.cjs");

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
  const defaultChannel = defaultUpdateChannel(currentVersion);
  let state = { channel: defaultChannel, phase: enabled ? "idle" : "unsupported", currentVersion, version: null, percent: 0, prompt: false,
    message: enabled ? "Updates are checked automatically. Downloads and installation need your approval." : "Install the Windows setup package to enable updates." };
  let preferences = {}, initializing, checking, savingPreference = false, manual = false, closed = false, timer, postponed = null, installing = false;
  const listeners = [];
  const status = () => ({ ...state });
  const change = patch => { if (!closed) { state = { ...state, ...patch }; onChange(status()); } };
  const error = value => change({ phase: "error", prompt: false, message: `Update failed: ${String(value?.message || value).slice(0, 400)}` });
  updater.autoDownload = false;
  updater.autoInstallOnAppQuit = false;
  function configureChannel(channel) {
    updater.channel = channel === "beta" ? "beta" : "latest";
    updater.allowPrerelease = channel === "beta";
    // electron-updater enables downgrades in its channel setter.
    updater.allowDowngrade = false;
  }
  configureChannel(defaultChannel);
  updater.disableWebInstaller = true;
  updater.setFeedURL({ provider: "custom", updateProvider: ChannelGitHubProvider, owner: "DSuzuki35", repo: "OpenGridXR", private: false });
  function listen(name, fn) { updater.on(name, fn); listeners.push([name, fn]); }
  listen("error", error);
  listen("update-available", info => {
    if (!enabled || closed || state.phase !== "checking") return;
    change({ phase: "available", version: info.version, percent: 0,
      prompt: manual || (preferences.skippedVersion !== info.version && postponed !== info.version),
      message: `OpenGrid XR ${info.version} is available.` });
  });
  listen("update-not-available", () => { if (state.phase === "checking") change({ phase: "current", version: null, prompt: false, message: `No newer version is available on the ${state.channel} channel.` }); });
  listen("download-progress", progress => { if (state.phase === "downloading") change({ percent: Math.max(0, Math.min(100, Number(progress.percent) || 0)) }); });
  listen("update-downloaded", info => {
    if (state.phase === "downloading" && info.version === state.version)
      change({ phase: "downloaded", percent: 100, prompt: true, message: `Version ${info.version} is ready. Save your profile and close both sims and Clean Output before installing.` });
  });
  function initialize() {
    if (!initializing) initializing = Promise.resolve().then(readPreferences).then(stored => {
      preferences = stored && typeof stored === "object" && !Array.isArray(stored) ? stored : {};
      const channel = ["stable", "beta"].includes(preferences.channel) ? preferences.channel : defaultChannel;
      configureChannel(channel);
      change({ channel });
    }).catch(e => { initializing = null; throw e; });
    return initializing;
  }
  async function check(userRequested = false) {
    if (!enabled || closed || installing || savingPreference || ["downloading", "downloaded", "installing"].includes(state.phase)) return status();
    if (checking) return checking;
    manual = userRequested === true;
    change({ phase: "checking", prompt: false, message: "Checking GitHub Releases…" });
    checking = (async () => {
      try {
        await initialize();
        if (!closed) await updater.checkForUpdates();
      } catch (e) { error(e); }
      return status();
    })();
    try { return await checking; } finally { checking = null; }
  }
  async function setChannel(channel) {
    if (!["stable", "beta"].includes(channel)) throw new Error("Unknown update channel.");
    if (closed || installing || savingPreference || ["downloading","installing"].includes(state.phase))
      throw new Error("Wait for the current update operation to finish.");
    savingPreference = true;
    try {
      await initialize();
      if (checking) await checking;
      if (closed) return status();
      if (preferences.channel === channel && state.channel === channel) return status();
      const next = { ...preferences, channel };
      await writePreferences(next);
      preferences = next;
      configureChannel(channel);
      postponed = null;
      change({ channel, phase: enabled ? "idle" : "unsupported", version: null, prompt: false, percent: 0 });
    } finally { savingPreference = false; }
    return check(true);
  }
  async function decide(choice) {
    if (!enabled || closed || savingPreference || !["available", "downloaded"].includes(state.phase)) throw new Error("No update is available for this action.");
    if (choice === "later") { postponed = state.version; change({ prompt: false }); }
    else if (choice === "skip" && state.phase === "available") {
      savingPreference = true;
      try {
        const next = { ...preferences, skippedVersion: state.version };
        await writePreferences(next); preferences = next; change({ prompt: false });
      } finally { savingPreference = false; }
    } else if (choice === "download" && state.phase === "available") {
      change({ phase: "downloading", prompt: true, percent: 0, message: "Downloading the update…" });
      try { await updater.downloadUpdate(); } catch (e) { error(e); }
    } else throw new Error("Invalid update choice.");
    return status();
  }
  async function install() {
    if (!enabled || closed || installing || savingPreference || state.phase !== "downloaded") throw new Error("The update is not ready to install.");
    installing = true;
    try {
      if (!await ensureCanInstall() || closed) return status();
      change({ phase: "installing", prompt: false, message: "Installing and restarting…" });
      updater.quitAndInstall(false, true);
    } finally { installing = false; }
    return status();
  }
  return { status, initialize, setChannel, check, decide, install,
    start() { if (!enabled || closed || timer) return; void check(); timer = setInterval(() => void check(), 6 * 60 * 60 * 1000); timer.unref?.(); },
    close() { closed = true; clearInterval(timer); for (const [name, fn] of listeners) updater.removeListener(name, fn); },
  };
}
module.exports = { createUpdateController, ensureUpdateInstall };
