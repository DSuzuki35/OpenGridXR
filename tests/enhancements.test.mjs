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

test('helmet-only profiles load the compositor engine without enabling frame generation',()=>{
  const doc=createDocument(),p=doc.profiles[0];p.helmet.enabled=true;
  for(const mode of ['driving','streaming']) {
    p.mode=mode;const engine=runtimeConfig.toEnhancementsConfig(doc,p.game);
    assert.equal(engine.core.enabled,true);assert.equal(engine.modules.framegen.enabled,false);
    assert.match(runtimeConfig.toRuntimeIni(doc),/EnhancementsEnabled=1/);
  }
});
test('visor scene adjustments stay neutral for old profiles and follow the headset helmet in both modes',()=>{
  const doc=createDocument(),p=doc.profiles[0];
  p.helmet.enabled=true;p.helmet.visorContrast=125;p.helmet.visorBrightness=90;p.helmet.visorSaturation=80;
  for(const mode of ['driving','streaming']) {
    p.mode=mode;
    const ini=runtimeConfig.toRuntimeIni(doc).split('[lmu]')[0];
    assert.match(ini,/HelmetVisorEnabled=1/);assert.match(ini,/HelmetVisorContrast=125/);
    assert.match(ini,/HelmetVisorBrightness=90/);assert.match(ini,/HelmetVisorSaturation=80/);
  }
  p.helmet.enabled=false;delete p.helmet.visorContrast;delete p.helmet.visorBrightness;delete p.helmet.visorSaturation;
  const ini=runtimeConfig.toRuntimeIni(doc).split('[lmu]')[0];
  assert.match(ini,/HelmetVisorEnabled=0/);assert.match(ini,/HelmetVisorContrast=100/);
  assert.match(ini,/HelmetVisorBrightness=100/);assert.match(ini,/HelmetVisorSaturation=100/);
});
test('runtime prepares helmet assets before publishing native settings and propagates failures',async()=>{
  const dir=await fs.mkdtemp(path.join(os.tmpdir(),'opengrid-assets-order-'));let called=0;
  try {
    const instance=runtime.createRuntime({sourceDir:dir,runtimeDir:dir,testMode:true,prepareAssets:async()=>{called++;throw new Error('Asset preparation fixture error');}});
    await assert.rejects(instance.writeConfig(createDocument()),/preparation fixture/);assert.equal(called,1);
    await assert.rejects(fs.access(path.join(dir,'OpenGridXR.ini')));
  } finally {await fs.rm(dir,{recursive:true,force:true});}
});

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
    ["overlay", "showResolution", "true"],
  ]) {
    const p = profile();
    p[group][key] = value;
    assert.throws(() => validateProfile(p), undefined, `${group}.${key}`);
  }
});

test("resolution HUD carries configured scale separately and supports older profiles", () => {
  const doc = createDocument();
  const p = doc.profiles[0];
  p.view.renderScale = 135;
  assert.equal(p.overlay.showResolution, true);
  let config = runtimeConfig.toEnhancementsConfig(doc, "iracing").core.osd;
  assert.equal(config.showResolution, true);
  assert.equal(config.configuredRenderScale, 135);
  assert.equal(config.renderWidth, undefined, "Resolution must come from submitted native images");
  p.overlay.showResolution = false;
  assert.equal(runtimeConfig.toEnhancementsConfig(doc, "iracing").core.osd.showResolution, false);
  delete p.overlay.showResolution;
  assert.doesNotThrow(() => validateProfile(p));
  assert.equal(runtimeConfig.toEnhancementsConfig(doc, "iracing").core.osd.showResolution, true);
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
