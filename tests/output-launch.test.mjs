import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { EventEmitter } from "node:events";
import { createRequire } from "node:module";
import { runInNewContext } from "node:vm";
import { promisify } from "node:util";
import { createProfile } from "../src/model.mjs";

// Exercise real launch validation and config writes without touching the registry
// or starting the user's installed output executable.
async function runtimeFixture(t) {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), "opengrid-output-launch-"));
  t.after(() => fs.rm(dir, { recursive: true, force: true }));
  const launches = [];
  const runtimePath = new URL("../desktop/runtime.cjs", import.meta.url);
  const require = createRequire(runtimePath);
  const childProcess = {
    execFile(file, args, options, callback) {
      assert.equal(path.basename(file), "OpenGridXR-Connection.exe");
      assert.equal(args[0], "--status");
      callback(null, JSON.stringify({machineDirectory:dir,machine:{present:true,enabled:true},legacyMachine:{present:false,enabled:false},user:{present:false,enabled:false}}), "");
    },
    spawn(file, args, options) {
      launches.push({ file, args: [...args], options });
      const child = new EventEmitter();
      child.exitCode = null;
      process.nextTick(() => child.emit("spawn"));
      return child;
    },
  };
  childProcess.execFile[promisify.custom] = async (...args) => new Promise((resolve, reject) => {
    childProcess.execFile(...args, (error, stdout, stderr) => error ? reject(error) : resolve({ stdout, stderr }));
  });
  const module = { exports: {} };
  runInNewContext(`(function(require, module, process) {${await fs.readFile(runtimePath, "utf8")}\n})`)(
    (name) => name === "node:child_process" ? childProcess : require(name),
    module,
    { platform: "win32", kill: process.kill, env: {} },
  );
  return {
    dir, launches,
    runtime: module.exports.createRuntime({ sourceDir: path.join(dir, "missing"), runtimeDir: dir }),
  };
}

for (const mode of ["driving", "streaming"]) {
  test(`Clean Output launches from saved ${mode} profile without changing its mode`, async (t) => {
    const { dir, launches, runtime } = await runtimeFixture(t);
    const profile = createProfile("iracing");
    delete profile.stream.preserveFullFrame;
    profile.mode = mode;
    profile.mask.enabled = true;
    profile.stream.extraLeft = 20;
    const doc = { profiles: [profile], selectedProfileId: profile.id };
    const before = JSON.stringify(doc);
    const result = await runtime.launch(doc, profile, [
      { id: "1", primary: true, bounds: { x: 0, y: 0, width: 1920, height: 1080 } },
    ]);
    assert.equal(result.started, true);
    assert.equal(launches.length, 1);
    assert.equal(launches[0].file, path.join(dir, "OpenGridXR-Output.exe"));
    assert.deepEqual(launches[0].args.slice(0, 4), ["--config", path.join(dir, "OpenGridXR.ini"), "--game", "iracing"]);
    assert.equal(JSON.stringify(doc), before);
    const ini = await fs.readFile(path.join(dir, "OpenGridXR.ini"), "utf8");
    assert.match(ini, new RegExp(`^Mode=${mode}$`, "m"));
    assert.match(ini, new RegExp(`^MaskEnabled=${mode === "driving" ? 1 : 0}$`, "m"));
    assert.match(ini, new RegExp(`^ExtraLeft=${mode === "driving" ? 0 : 20}$`, "m"));
    await assert.rejects(runtime.launch(doc, profile, []), /already open/);
  });
}
