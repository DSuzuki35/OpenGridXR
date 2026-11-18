"use strict";
const path = require("node:path");
const fs = require("node:fs/promises");
const childProcess = require("node:child_process");

// Studio 397's documented no-EAC launch: run the game executable directly.
// https://guide.lemansultimate.com/hc/en-gb/articles/14590657733903
async function launchLmuOffline({ executable, ensureStopped, environment = process.env, stat = fs.stat, spawn = childProcess.spawn }) {
  if (typeof executable !== "string" || !path.isAbsolute(executable) ||
      !/^(?:Le Mans Ultimate|LeMansUltimate)\.exe$/i.test(path.basename(executable)))
    throw new Error("Select the LMU game executable: Le Mans Ultimate.exe or LeMansUltimate.exe.");
  const info = await stat(executable).catch(() => null);
  if (!info?.isFile()) throw new Error("LMU executable not found. Select it from your installed game folder.");
  await ensureStopped();
  const env={...environment};
  for(const key of Object.keys(env))if(/^(LMU_QV_|OPENGRIDXR_LMU_)/i.test(key))delete env[key];
  env.OPENGRIDXR_LMU_INTEGRATED='0';env.LMU_QV_DYNAMIC_FOCUS='0';
  await new Promise((resolve, reject) => {
    const child = spawn(executable, ["+XR"], {
      cwd: path.dirname(executable), shell: false, detached: true,
      windowsHide: false, stdio: "ignore",
      env,
    });
    child.once("error", reject);
    child.once("spawn", () => { child.unref(); resolve(); });
  });
  return { message: "LMU launched in OpenXR for offline testing. Use Race Weekend; online racing requires the normal Steam/EAC launch. DLL loading still depends on your selected VR runtime." };
}
module.exports = { launchLmuOffline };
