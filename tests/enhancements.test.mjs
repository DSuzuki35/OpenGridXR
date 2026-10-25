import test from "node:test";
import assert from "node:assert/strict";
import {
  createProfile,
  createDocument,
  validateProfile,
} from "../src/model.mjs";
import runtimeConfig from "../desktop/runtime-config.cjs";
import runtime from "../desktop/runtime.cjs";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";

const fg = () => ({
  enabled: false,
  synthesisMode: "interpolate",
  flowQuality: "balanced",
  motionEstimation: "shader",
  frameMultiplier: "dynamic",
  artifactSuppression: 50,
  motionLimitPercent: 10,
  diagnosticView: "off",
  toggleShortcut: "Ctrl+Alt+F9",
});
const osd = () => ({
  enabled: false,
  layout: "expanded",
  horizontalDegrees: 20,
  verticalDegrees: -12,
  distanceMeters: 1.2,
  scale: 100,
  opacity: 90,
  updateHz: 5,
  showGraph: true,
  showRuntime: true,
  showApp: true,
  showClock: false,
  showPerformance: true,
  showFramegen: true,
  showToolkit: true,
  accent: "violet",
  customColor: "#8e70f2",
  compactMetrics: "all",
  toggleShortcut: "Ctrl+Alt+F10",
  cycleShortcut: "Ctrl+Alt+F11",
});
function profile() {
  return { ...createProfile(), framegen: fg(), overlay: osd() };
}

test("new profiles do not activate frame generation or an overlay; legacy imports remain valid", () => {
  const p = createProfile();
  assert.equal(p.framegen?.enabled, false);
  assert.equal(p.overlay?.enabled, false);
  delete p.framegen;
  delete p.overlay;
  assert.doesNotThrow(() => validateProfile(p));
});
test("invalid enhancement settings cannot reach native configuration", () => {
  for (const [group, key, value] of [
    ["framegen", "enabled", 1],
    ["framegen", "frameMultiplier", "8x"],
    ["framegen", "artifactSuppression", 101],
    ["framegen", "motionLimitPercent", 1],
    ["framegen", "motionEstimation", "dlss"],
    ["framegen", "toggleShortcut", "F9"],
    ["overlay", "distanceMeters", 0],
    ["overlay", "opacity", 29],
    ["overlay", "updateHz", 2.5],
    ["overlay", "horizontalDegrees", 41],
    ["overlay", "customColor", "#aabbcc;run"],
    ["overlay", "showToolkit", "true"],
  ]) {
    const p = profile();
    p[group][key] = value;
    assert.throws(() => validateProfile(p), undefined, `${group}.${key}`);
  }
});
test("shortcut collisions are rejected while explicitly unbound keys are allowed", () => {
  const p = profile();
  p.overlay.toggleShortcut = p.framegen.toggleShortcut;
  assert.throws(() => validateProfile(p), /shortcut/i);
  p.overlay.toggleShortcut = "";
  p.overlay.cycleShortcut = "";
  p.framegen.toggleShortcut = "";
  assert.doesNotThrow(() => validateProfile(p));
});
test("native enhancement configuration isolates games and keeps unrelated modules off", () => {
  const doc = createDocument();
  doc.profiles[0].framegen = fg();
  doc.profiles[0].framegen.enabled = true;
  doc.profiles[0].overlay = osd();
  doc.profiles[0].overlay.enabled = true;
  doc.profiles[0].view.renderScale = 125;
  const c = runtimeConfig.toEnhancementsConfig(doc, "iracing");
  assert.equal(c.version, 3);
  assert.equal(c.modules.framegen.enabled, true);
  for (const name of ["depthxr", "pivotxr", "quadviews", "turbo"])
    assert.equal(c.modules[name].enabled, false, name);
  assert.equal(c.core.trackSeenApps, false);
  assert.equal(c.core.osd.enabled, true);
  assert.equal(c.core.osd.accent, "violet");
  assert.match(c.core.osd.toolkitSummary, /125%/);
  assert.deepEqual(c.modules.framegen.toggleBinding, {
    type: "keyboard",
    chord: ["Ctrl", "Alt", "F9"],
  });
  assert.equal(
    runtimeConfig.toEnhancementsConfig(doc, "lmu").modules.framegen.enabled,
    false,
  );
  assert.match(runtimeConfig.toRuntimeIni(doc), /EnhancementsEnabled=1/);
});
test("saved active game profile determines enhancement settings and legacy settings stay neutral", () => {
  const doc = createDocument(),
    second = createProfile("lmu", "VR tuning");
  second.framegen = { ...fg(), enabled: true, frameMultiplier: "2x" };
  doc.profiles.push(second);
  doc.activeProfiles = { lmu: second.id };
  assert.equal(
    runtimeConfig.toEnhancementsConfig(doc, "lmu").modules.framegen.defaults
      .frameMultiplier,
    "2x",
  );
  doc.selectedProfileId = doc.profiles[1].id;
  assert.equal(
    runtimeConfig.toEnhancementsConfig(doc, "lmu").modules.framegen.enabled,
    false,
  );
  delete doc.profiles[1].framegen;
  delete doc.profiles[1].overlay;
  assert.equal(
    runtimeConfig.toEnhancementsConfig(doc, "lmu").core.enabled,
    false,
  );
});
test("runtime writes enhancement settings only to its owned per-game files", async () => {
  const root = await fs.mkdtemp(
    path.join(os.tmpdir(), "opengrid-enhancements-"),
  );
  try {
    const runtimeDir = path.join(root, "runtime"),
      doc = createDocument();
    doc.profiles[0].framegen.enabled = true;
    const bridge = runtime.createRuntime({
      sourceDir: path.join(root, "source"),
      runtimeDir,
      testMode: true,
    });
    await bridge.writeConfig(doc);
    assert.deepEqual((await fs.readdir(runtimeDir)).sort(), [
      "OpenGridXR-enhancements-iracing.json",
      "OpenGridXR-enhancements-lmu.json",
      "OpenGridXR.ini",
    ]);
    const ir = JSON.parse(
      await fs.readFile(
        path.join(runtimeDir, "OpenGridXR-enhancements-iracing.json"),
        "utf8",
      ),
    );
    const lmu = JSON.parse(
      await fs.readFile(
        path.join(runtimeDir, "OpenGridXR-enhancements-lmu.json"),
        "utf8",
      ),
    );
    assert.equal(ir.modules.framegen.enabled, true);
    assert.equal(lmu.modules.framegen.enabled, false);
    assert.deepEqual(await fs.readdir(root), ["runtime"]);
  } finally {
    await fs.rm(root, { recursive: true, force: true });
  }
});
test("engine loaded status requires a live fresh session and does not assert generated frames", () => {
  const data = {
    active: true,
    pid: 123,
    updatedUnixMs: 1000,
    engineLoaded: true,
    engineError: "",
  };
  assert.equal(runtime.sessionState(data, 1000, () => true).engineLoaded, true);
  assert.equal(
    runtime.sessionState(data, 12000, () => true).engineLoaded,
    false,
  );
  assert.equal(
    runtime.sessionState(data, 1000, () => false).engineLoaded,
    false,
  );
  assert.equal(
    runtime.sessionState(data, 1000, () => true).framegenActive,
    undefined,
  );
});
