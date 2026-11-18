"use strict";
const assert = require("node:assert/strict");
const path = require("node:path");
const { randomUUID } = require("node:crypto");
const { spawn } = require("node:child_process");
const { setTimeout: pause } = require("node:timers/promises");
const { createTelemetry } = require("../desktop/telemetry.cjs");
(async () => {
  const directory = path.resolve(__dirname, "../native/build/Release"), namespace = `desktop-${randomUUID()}`;
  const fixture = spawn(path.join(directory, "opengrid-telemetry-fixture.exe"), ["--namespace", namespace, "--duration-ms", "6000", "--freeze-after-ms", "600"], { windowsHide: true, stdio: ["ignore", "pipe", "pipe"] });
  const telemetry = createTelemetry({ executable: path.join(directory, "OpenGridXR-Telemetry.exe"), testNamespace: namespace });
  try {
    await new Promise((resolve, reject) => {
      const timeout = setTimeout(() => reject(new Error("Telemetry fixture startup timed out.")), 3000);
      fixture.once("error", (error) => { clearTimeout(timeout); reject(error); });
      fixture.stdout.once("data", () => { clearTimeout(timeout); resolve(); });
      fixture.stderr.on("data", (chunk) => process.stderr.write(chunk));
    });
    const seenLive = new Set(), seenStale = new Set(), latest = {};
    const deadline = Date.now() + 3000;
    while (Date.now() < deadline && seenStale.size < 2) {
      for (const game of ["iracing", "lmu"]) {
        const state = await telemetry.status(game, { enabled: true, updateHz: 20 });
        if (!state.record) continue;
        assert.equal(state.record.testData, true);
        latest[game] = state.record.status;
        if (state.record.status === "live") { assert.equal(typeof state.record.data.speedMps, "number"); seenLive.add(game); }
        if (seenLive.has(game) && state.record.status === "stale") seenStale.add(game);
      }
      await pause(50);
    }
    assert.equal(seenLive.size, 2, JSON.stringify(latest));
    assert.equal(seenStale.size, 2, JSON.stringify(latest));
    console.log("Both native telemetry sources reached the desktop controller live, then stale; isolated fixture mappings only.");
  } finally { telemetry.close(); fixture.kill(); }
})().catch((error) => { console.error(error); process.exitCode = 1; });
