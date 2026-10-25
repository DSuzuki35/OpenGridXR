"use strict";
const { spawn } = require("node:child_process");
function createGameControl({ executable }) {
  let child = null;
  return {
    center(game, settings) {
      if (!["iracing", "lmu"].includes(game) || !settings?.gameShortcut || !/^(?:Ctrl\+)?(?:Alt\+)?(?:Shift\+)?F(?:[1-9]|1[0-2])$/.test(settings.gameShortcut) || !Number.isInteger(settings.delaySeconds) || settings.delaySeconds < 1 || settings.delaySeconds > 5)
        return Promise.reject(new Error("Save a game recenter shortcut first, and assign the same shortcut in the sim."));
      if (child) return Promise.reject(new Error("A centering command is already pending."));
      return new Promise((resolve, reject) => {
        let error = "", output = "", settled = false;
        const finish = (failure) => { if (settled) return; settled = true; clearTimeout(timeout); child = null; failure ? reject(new Error(failure)) : resolve({ message: output.trim() || "Shortcut sent." }); };
        child = spawn(executable, ["--game", game, "--key", settings.gameShortcut, "--delay-ms", String(settings.delaySeconds * 1000)], { windowsHide: true, stdio: ["ignore", "pipe", "pipe"] });
        const timeout = setTimeout(() => { child?.kill(); finish("Centering timed out."); }, 8000);
        child.stdout.on("data", (chunk) => { output = (output + chunk).slice(-2048); });
        child.stderr.on("data", (chunk) => { error = (error + chunk).slice(-2048); });
        child.on("error", () => finish("Build or install the game control helper first."));
        child.on("exit", (code) => finish(code === 0 ? null : error.trim() || "The centering command was not sent."));
      });
    },
    close() { child?.kill(); child = null; },
  };
}
module.exports = { createGameControl };
