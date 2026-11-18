"use strict";
const { spawn } = require("node:child_process");
function createGameControl({ executable, spawnProcess = spawn }) {
  let child = null, pending = null;
  const cancelled = "Centering cancelled. Save or discard profile edits before using bindings.";
  return {
    center(game, settings, { canApply = () => true } = {}) {
      if (!["iracing", "lmu"].includes(game) || !settings?.gameShortcut || !/^(?:Ctrl\+)?(?:Alt\+)?(?:Shift\+)?F(?:[1-9]|1[0-2])$/.test(settings.gameShortcut) || !Number.isInteger(settings.delaySeconds) || settings.delaySeconds < 1 || settings.delaySeconds > 5)
        return Promise.reject(new Error("Save a game recenter shortcut first, and assign the same shortcut in the sim."));
      if (child) return Promise.reject(new Error("A centering command is already pending."));
      if (!canApply()) return Promise.reject(new Error(cancelled));
      return new Promise((resolve, reject) => {
        let error = "", output = "", settled = false;
        const finish = (failure) => { if (settled) return; settled = true; clearTimeout(timeout); clearInterval(watchdog); child = null; pending = null; failure ? reject(new Error(failure)) : resolve({ message: output.trim() || "Shortcut sent." }); };
        child = spawnProcess(executable, ["--game", game, "--key", settings.gameShortcut, "--delay-ms", String(settings.delaySeconds * 1000)], { windowsHide: true, stdio: ["ignore", "pipe", "pipe"] });
        const command = child;
        const cancel = (message) => { command.kill(); finish(message); };
        pending = { canApply, cancel };
        const timeout = setTimeout(() => cancel("Centering timed out."), 8000);
        const watchdog = setInterval(() => { if (!canApply()) cancel(cancelled); }, 20);
        child.stdout.on("data", (chunk) => { output = (output + chunk).slice(-2048); });
        child.stderr.on("data", (chunk) => { error = (error + chunk).slice(-2048); });
        child.on("error", () => finish("Build or install the game control helper first."));
        child.on("exit", (code) => finish(code === 0 ? null : error.trim() || "The centering command was not sent."));
      });
    },
    cancelIfInvalid() { if (pending && !pending.canApply()) pending.cancel(cancelled); },
    close() { pending?.cancel("Centering cancelled."); },
  };
}
module.exports = { createGameControl };
