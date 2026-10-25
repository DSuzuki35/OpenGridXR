const { _electron: electron } = require("playwright-core");
const fs = require("node:fs/promises");
const path = require("node:path");
const os = require("node:os");
const assert = require("node:assert/strict");
(async () => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), "opengrid-desktop-"));
  const rendererFixture = path.join(dir, "fixtures", "rendererDX11OpenXR.ini");
  const originalRenderer = Buffer.from(
    "; isolated UI test fixture\r\n[OpenXR]\r\nFoveatedOuterPctRes=35 ; keep peripheral comment\r\nFoveatedInsetWidthPct=40\r\nResolutionScalePct=100\r\nAllowEyeTracking=0\r\nUnrelatedSetting=keep-this\r\n[Other]\r\nValue=77\r\n",
    "utf8",
  );
  await fs.mkdir(path.dirname(rendererFixture), { recursive: true });
  await fs.writeFile(rendererFixture, originalRenderer);
  const results = path.resolve("test-results");
  await fs.mkdir(results, { recursive: true });
  const env = {
    ...process.env,
    OPENGRIDXR_DATA_DIR: dir,
    OPENGRIDXR_TEST_MODE: "1",
  };
  delete env.ELECTRON_RUN_AS_NODE;
  const app = await electron.launch({
    executablePath: process.env.OPENGRIDXR_TEST_EXE || require("electron"),
    args: process.env.OPENGRIDXR_TEST_EXE ? [] : [path.resolve(".")],
    env,
    timeout: 60000,
  });
  const errors = [];
  try {
    const page = await app.firstWindow();
    page.on("pageerror", (error) => errors.push(error.message));
    await page
      .getByRole("button", { name: "Contour point 1", exact: true })
      .waitFor({ timeout: 20000 });
    await page.screenshot({ path: path.join(results, "01-view-crop.png") });
    const initial = await page.evaluate(() => window.openGrid.load());
    assert.equal(initial.document.profiles.length, 2);
    assert.equal(initial.runtime.active, false);
    await page
      .getByRole("button", { name: "Add contour point", exact: true })
      .click();
    assert.equal(
      await page.getByRole("button", { name: /^Contour point \d+$/ }).count(),
      11,
    );
    const point = page.getByRole("button", {
      name: "Contour point 1",
      exact: true,
    });
    await point.focus();
    await point.press("ArrowRight");
    await page.getByRole("button", { name: "Luma", exact: true }).click();
    await page
      .getByRole("spinbutton", {
        name: "Sharpening strength value",
        exact: true,
      })
      .fill("65");
    await page
      .getByRole("spinbutton", { name: "Luma clamp value", exact: true })
      .fill("0.045");
    await page
      .getByRole("spinbutton", { name: "Luma radius value", exact: true })
      .fill("1.8");
    await page
      .getByRole("spinbutton", { name: "Luma pattern value", exact: true })
      .fill("3");
    await page.getByRole("button", { name: /^Save profile$/i }).click();
    await page
      .getByRole("button", { name: "Profile saved", exact: true })
      .waitFor();
    const saved = await page.evaluate(() => window.openGrid.load());
    assert.equal(saved.document.profiles[0].mask.left.length, 11);
    assert.equal(saved.document.profiles[0].mask.right.length, 11);
    assert.ok(saved.document.profiles[0].mask.left[0].x > 0.12);
    assert.deepEqual(saved.document.profiles[0].sharpening, {
      mode: "luma",
      strength: 65,
      clamp: 0.045,
      radius: 1.8,
      pattern: 3,
    });
    await page
      .getByRole("button", { name: "Foveated rendering", exact: true })
      .click();
    await page.getByText("Renderer file available", { exact: true }).waitFor();
    await page
      .getByRole("spinbutton", {
        name: "Peripheral resolution value",
        exact: true,
      })
      .fill("45");
    await page
      .getByRole("spinbutton", { name: "Focus width value", exact: true })
      .fill("32");
    await page
      .getByRole("spinbutton", {
        name: "Game supersampling value",
        exact: true,
      })
      .fill("125");
    await page
      .getByRole("button", { name: "Review game changes", exact: true })
      .click();
    await page
      .getByRole("button", { name: "Apply to iRacing", exact: true })
      .waitFor();
    assert.deepEqual(
      await fs.readFile(rendererFixture),
      originalRenderer,
      "Review does not modify the game file",
    );
    assert.equal(
      await page.locator(".native-change-table tbody tr").count(),
      3,
    );
    await page
      .getByRole("spinbutton", { name: "Focus width value", exact: true })
      .fill("33");
    assert.equal(
      await page
        .getByRole("button", { name: "Apply to iRacing", exact: true })
        .count(),
      0,
      "Editing invalidates the exact pending review",
    );
    await page.locator(".main-content").evaluate((element) => {
      element.scrollTop = 0;
    });
    await page.screenshot({
      path: path.join(results, "09-native-foveation.png"),
    });
    await page
      .getByRole("button", { name: "Review game changes", exact: true })
      .click();
    await page
      .getByRole("button", { name: "Apply to iRacing", exact: true })
      .scrollIntoViewIfNeeded();
    await page.screenshot({ path: path.join(results, "10-native-review.png") });
    await page
      .getByRole("button", { name: "Apply to iRacing", exact: true })
      .click();
    await page.locator(".operation-overlay").waitFor({ state: "hidden" });
    const changedRenderer = (await fs.readFile(rendererFixture)).toString(
      "utf8",
    );
    assert.match(
      changedRenderer,
      /FoveatedOuterPctRes=45 ; keep peripheral comment/,
    );
    assert.match(changedRenderer, /FoveatedInsetWidthPct=33/);
    assert.match(changedRenderer, /ResolutionScalePct=125/);
    assert.match(
      changedRenderer,
      /AllowEyeTracking=0\r\nUnrelatedSetting=keep-this/,
    );
    const nativeState = await page.evaluate(() =>
      window.openGrid.nativeSettings(),
    );
    assert.ok(
      nativeState.backups.length > 0,
      "Applying creates a real exact-file backup",
    );
    await page
      .getByLabel("Backup to restore", { exact: true })
      .selectOption(nativeState.backups[0].id);
    await page
      .getByRole("button", { name: "Restore full file", exact: true })
      .click();
    await page.locator(".operation-overlay").waitFor({ state: "hidden" });
    assert.deepEqual(
      await fs.readFile(rendererFixture),
      originalRenderer,
      "Explicit restore recovers the complete original bytes",
    );
    await page
      .getByRole("button", { name: "Review game changes", exact: true })
      .click();
    await page
      .getByRole("button", { name: "Apply to iRacing", exact: true })
      .waitFor();
    await page.getByLabel("Game", { exact: true }).selectOption("lmu");
    await page
      .getByText("NO VERIFIED NATIVE ADAPTER", { exact: true })
      .waitFor();
    assert.equal(
      await page
        .getByRole("button", { name: "Apply to iRacing", exact: true })
        .count(),
      0,
      "Changing game invalidates the pending review",
    );
    await page.getByLabel("Game", { exact: true }).selectOption("iracing");
    await page.getByText("Renderer file available", { exact: true }).waitFor();
    assert.equal(
      await page
        .getByRole("button", { name: "Apply to iRacing", exact: true })
        .count(),
      0,
    );
    await page.getByRole("button", { name: "Stream", exact: true }).click();
    const symmetric = page.getByRole("switch", {
      name: "Symmetric extra area",
      exact: true,
    });
    const extraLeft = page.getByRole("spinbutton", {
      name: "Extra area · left value",
      exact: true,
    });
    const extraRight = page.getByRole("spinbutton", {
      name: "Extra area · right value",
      exact: true,
    });
    assert.equal(await symmetric.getAttribute("aria-checked"), "false");
    await extraLeft.fill("18");
    await extraRight.fill("6");
    await symmetric.click();
    assert.equal(await extraLeft.inputValue(), "18");
    assert.equal(
      await extraRight.inputValue(),
      "18",
      "Enabling symmetry preserves the larger reserve",
    );
    await extraRight.fill("22");
    assert.equal(
      await extraLeft.inputValue(),
      "22",
      "Right-side edits update both linked sides",
    );
    await extraLeft.fill("19");
    assert.equal(
      await extraRight.inputValue(),
      "19",
      "Left-side edits update both linked sides",
    );
    await symmetric.click();
    assert.equal(await extraLeft.inputValue(), "19");
    assert.equal(
      await extraRight.inputValue(),
      "19",
      "Unlinking preserves both values",
    );
    await extraRight.fill("7");
    assert.equal(
      await extraLeft.inputValue(),
      "19",
      "Unlinked editing remains independent",
    );
    await symmetric.click();
    const displays = await page.evaluate(() => window.openGrid.listDisplays());
    assert.ok(displays.length > 0);
    await page
      .getByLabel("Output display", { exact: true })
      .selectOption(displays[0].id);
    assert.equal(
      await page
        .getByRole("switch", { name: "Start fullscreen", exact: true })
        .getAttribute("aria-checked"),
      "true",
    );
    await page.getByRole("button", { name: /^Save profile$/i }).click();
    await page
      .getByRole("button", { name: "Profile saved", exact: true })
      .waitFor();
    const streamSaved = await page.evaluate(() => window.openGrid.load());
    assert.equal(
      streamSaved.document.profiles[0].stream.displayId,
      displays[0].id,
    );
    await page.reload();
    await page.getByRole("button", { name: "Stream", exact: true }).click();
    assert.equal(
      await symmetric.getAttribute("aria-checked"),
      "true",
      "Symmetry preference survives a real file save/reload",
    );
    assert.equal(await extraLeft.inputValue(), "19");
    assert.equal(await extraRight.inputValue(), "19");
    await page.screenshot({ path: path.join(results, "02-stream.png") });
    await page.getByRole("button", { name: "Helmet", exact: true }).click();
    await page.screenshot({ path: path.join(results, "03-helmet.png") });
    await page
      .getByRole("button", { name: "Frame generation", exact: true })
      .click();
    await page
      .getByRole("switch", { name: "Configure frame generation", exact: true })
      .click();
    await page
      .getByLabel("Synthesis mode", { exact: true })
      .selectOption("extrapolate");
    await page
      .getByLabel("Frame multiplier", { exact: true })
      .selectOption("3x");
    await page
      .getByLabel("Motion estimation", { exact: true })
      .selectOption("hardware");
    await page
      .getByLabel("Frame generation toggle shortcut", { exact: true })
      .selectOption("Ctrl+Alt+F8");
    assert.equal(
      await page.getByText("Awaiting an XR session", { exact: true }).count(),
      1,
      "Configured FG does not claim a live engine session",
    );
    await page.screenshot({
      path: path.join(results, "07-frame-generation.png"),
    });
    await page
      .getByRole("button", { name: "Performance HUD", exact: true })
      .click();
    await page
      .getByRole("switch", { name: "Enable performance HUD", exact: true })
      .click();
    await page
      .getByRole("spinbutton", { name: "Horizontal angle value", exact: true })
      .fill("24");
    await page
      .getByRole("spinbutton", { name: "Panel distance value", exact: true })
      .fill("1.5");
    await page
      .getByRole("spinbutton", { name: "Panel opacity value", exact: true })
      .fill("75");
    await page
      .getByRole("button", { name: "Teal accent", exact: true })
      .click();
    await page.getByRole("button", { name: "Compact", exact: true }).click();
    const metricPlaceholders = await page
      .locator(".overlay-metric-value")
      .allTextContents();
    assert.ok(
      metricPlaceholders.length > 0 &&
        metricPlaceholders.every((text) => text.startsWith("--")),
      "Overlay preview has no fabricated live FPS or frame times",
    );
    const overlayKey = page.getByLabel("Overlay toggle shortcut", {
      exact: true,
    });
    assert.equal(
      await overlayKey.locator('option[value="Ctrl+Alt+F8"]').isDisabled(),
      true,
      "FG shortcut cannot be assigned to overlay",
    );
    await overlayKey.selectOption("Ctrl+Alt+F7");
    await page
      .getByLabel("Overlay layout shortcut", { exact: true })
      .selectOption("Ctrl+Alt+F6");
    await page.getByRole("button", { name: /^Save profile$/i }).click();
    await page
      .getByRole("button", { name: "Profile saved", exact: true })
      .waitFor();
    await page.reload();
    await page
      .getByRole("button", { name: "Performance HUD", exact: true })
      .click();
    assert.equal(
      await page
        .getByRole("switch", { name: "Enable performance HUD", exact: true })
        .getAttribute("aria-checked"),
      "true",
    );
    assert.equal(
      await page
        .getByRole("button", { name: "Compact", exact: true })
        .getAttribute("aria-pressed"),
      "true",
    );
    assert.equal(
      await page
        .getByRole("spinbutton", { name: "Panel opacity value", exact: true })
        .inputValue(),
      "75",
    );
    const enhanced = (await page.evaluate(() => window.openGrid.load()))
      .document.profiles[0];
    assert.equal(enhanced.framegen.enabled, true);
    assert.equal(enhanced.framegen.synthesisMode, "extrapolate");
    assert.equal(enhanced.framegen.motionEstimation, "hardware");
    assert.equal(enhanced.framegen.frameMultiplier, "3x");
    assert.equal(enhanced.framegen.toggleShortcut, "Ctrl+Alt+F8");
    assert.equal(enhanced.overlay.distanceMeters, 1.5);
    assert.equal(enhanced.overlay.horizontalDegrees, 24);
    assert.equal(enhanced.overlay.accent, "teal");
    assert.equal(enhanced.overlay.toggleShortcut, "Ctrl+Alt+F7");
    assert.equal(enhanced.overlay.cycleShortcut, "Ctrl+Alt+F6");
    await page.screenshot({ path: path.join(results, "08-vr-overlay.png") });
    await page.getByRole("button", { name: "Telemetry", exact: true }).click();
    assert.match(
      await page.getByTestId("telemetry-speed").textContent(),
      /^--/,
    );
    assert.equal(
      await page
        .getByRole("switch", { name: "Enable telemetry", exact: true })
        .getAttribute("aria-checked"),
      "false",
    );
    await page.screenshot({
      path: path.join(results, "11-telemetry-offline.png"),
    });
    await page
      .getByRole("switch", { name: "Enable telemetry", exact: true })
      .click();
    await page
      .getByRole("spinbutton", {
        name: "Source update rate value",
        exact: true,
      })
      .fill("17");
    await page.getByRole("button", { name: /^Save profile$/i }).click();
    await page
      .getByRole("button", { name: "Profile saved", exact: true })
      .waitFor();
    await page.reload();
    await page.getByRole("button", { name: "Telemetry", exact: true }).click();
    assert.equal(
      await page
        .getByRole("switch", { name: "Enable telemetry", exact: true })
        .getAttribute("aria-checked"),
      "true",
    );
    assert.equal(
      await page
        .getByRole("spinbutton", {
          name: "Source update rate value",
          exact: true,
        })
        .inputValue(),
      "17",
    );
    assert.deepEqual(
      (await page.evaluate(() => window.openGrid.load())).document.profiles[0]
        .telemetry,
      { enabled: true, updateHz: 17 },
    );

    // Isolated, explicitly labelled renderer fixtures exercise fresh/stale UI
    // transitions. These records are never included in the production app.
    await app.evaluate(({ ipcMain }) => {
      globalThis.telemetryUiOriginal = ipcMain._invokeHandlers.get(
        "opengrid:telemetryStatus",
      );
      globalThis.telemetryUiCalls = 0;
      globalThis.telemetryUiFixture = {
        available: true,
        running: true,
        enabled: true,
        requestedHz: 17,
        message: "Isolated telemetry UI fixture.",
        record: {
          schemaVersion: 1,
          game: "iracing",
          source: "iracing-sdk",
          status: "live",
          connected: true,
          stale: false,
          testData: true,
          sequence: 7,
          timestampMs: Date.now(),
          ageMs: 0,
          data: {
            speedMps: 50,
            gear: 4,
            rpm: 6500,
            throttle: 0.75,
            brake: 0,
            clutch: null,
            fuelLiters: 34.5,
            lap: 4,
            lapsCompleted: 3,
            lapTimeSeconds: 63.125,
            lastLapSeconds: 90.5,
            bestLapSeconds: 89.25,
            inPits: false,
            flags: {
              yellow: false,
              blue: false,
              green: true,
              white: false,
              checkered: false,
              red: false,
              black: false,
            },
          },
        },
      };
      ipcMain.removeHandler("opengrid:telemetryStatus");
      ipcMain.handle("opengrid:telemetryStatus", async (event, game) => {
        globalThis.telemetryUiCalls++;
        return game === "iracing"
          ? globalThis.telemetryUiFixture
          : globalThis.telemetryUiOriginal(event, game);
      });
    });
    await page.getByText("Test fixture data", { exact: true }).waitFor();
    assert.match(
      await page.getByTestId("telemetry-speed").textContent(),
      /^180/,
    );
    assert.equal(await page.getByTestId("telemetry-gear").textContent(), "4");
    assert.equal(
      await page.getByTestId("telemetry-lap-time").textContent(),
      "1:03.125",
    );
    await app.evaluate(() => {
      globalThis.telemetryUiFixture.record.status = "stale";
      globalThis.telemetryUiFixture.record.stale = true;
      globalThis.telemetryUiFixture.record.ageMs = 2100;
    });
    await page.getByText("Stale data", { exact: true }).waitFor();
    assert.match(
      await page.getByTestId("telemetry-speed").textContent(),
      /^--/,
    );
    assert.match(await page.getByTestId("telemetry-rpm").textContent(), /^--/);
    assert.equal(
      await page.getByTestId("telemetry-lap-time").textContent(),
      "--:--.---",
    );
    await app.evaluate(() => {
      globalThis.telemetryUiFixture.record.status = "disconnected";
      globalThis.telemetryUiFixture.record.stale = false;
      globalThis.telemetryUiFixture.record.connected = false;
    });
    await page.getByText("Game disconnected", { exact: true }).waitFor();
    assert.match(
      await page.getByTestId("telemetry-speed").textContent(),
      /^--/,
    );
    // The attached automation target keeps visibility visible even when its
    // native window is hidden. Exercise the document-visibility boundary
    // explicitly, then restore the real inherited property immediately.
    await page.evaluate(() => {
      Object.defineProperty(document, "hidden", {
        configurable: true,
        get: () => true,
      });
      document.dispatchEvent(new Event("visibilitychange"));
    });
    await page.getByText("Paused while hidden", { exact: true }).waitFor();
    const hiddenCalls = await app.evaluate(() => globalThis.telemetryUiCalls);
    await new Promise((resolve) => setTimeout(resolve, 650));
    assert.ok(
      (await app.evaluate(() => globalThis.telemetryUiCalls)) <=
        hiddenCalls + 1,
      "A hidden-document visibility event stops telemetry polling",
    );
    await page.evaluate(() => {
      delete document.hidden;
      document.dispatchEvent(new Event("visibilitychange"));
    });
    await page.getByText("Game disconnected", { exact: true }).waitFor();
    await page.getByRole("button", { name: "Profiles", exact: true }).click();
    const unmountedCalls = await app.evaluate(
      () => globalThis.telemetryUiCalls,
    );
    await new Promise((resolve) => setTimeout(resolve, 650));
    assert.ok(
      (await app.evaluate(() => globalThis.telemetryUiCalls)) <=
        unmountedCalls + 1,
      "Leaving the telemetry page releases its status polling",
    );
    await app.evaluate(({ ipcMain }) => {
      ipcMain.removeHandler("opengrid:telemetryStatus");
      ipcMain.handle(
        "opengrid:telemetryStatus",
        globalThis.telemetryUiOriginal,
      );
      delete globalThis.telemetryUiFixture;
    });
    await page
      .getByRole("button", { name: "Spatial overlays", exact: true })
      .click();
    assert.equal(
      await page
        .getByRole("switch", { name: "Enable spatial overlays", exact: true })
        .getAttribute("aria-checked"),
      "false",
    );
    assert.equal(
      await page
        .getByRole("button", { name: "Center VR in game", exact: true })
        .isDisabled(),
      true,
      "Game centering starts unbound",
    );
    await page
      .getByText("Make room for what matters.", { exact: true })
      .waitFor();
    await page
      .getByRole("button", { name: "Add overlay", exact: true })
      .click();
    const showInStream = page.getByRole("switch", {
      name: "Show in stream",
      exact: true,
    });
    assert.equal(
      await showInStream.count(),
      1,
      "Each spatial overlay has an independent stream visibility preference",
    );
    assert.equal(
      await showInStream.getAttribute("aria-checked"),
      "false",
      "New overlays are private by default",
    );
    await showInStream.click();
    await page
      .getByLabel("Overlay name", { exact: true })
      .fill("Race dashboard");
    await page
      .getByLabel("Source URL", { exact: true })
      .fill("https://example.invalid/dashboard");
    await page
      .getByRole("switch", { name: "Enable overlay", exact: true })
      .click();
    await page
      .getByLabel("Source resolution", { exact: true })
      .selectOption("1280x720");
    await page
      .getByRole("spinbutton", {
        name: "Requested source rate value",
        exact: true,
      })
      .fill("12");
    await page
      .getByRole("spinbutton", {
        name: "Source pixel budget value",
        exact: true,
      })
      .fill("24");
    await page
      .getByRole("group", { name: "Placement presets", exact: true })
      .getByRole("button", { name: "Left", exact: true })
      .click();
    await page
      .getByRole("spinbutton", { name: "Distance forward value", exact: true })
      .fill("1.75");
    await page
      .getByRole("spinbutton", { name: "Overlay width value", exact: true })
      .fill("0.8");
    await page
      .getByRole("spinbutton", { name: "Overlay opacity value", exact: true })
      .fill("85");
    await page
      .getByLabel("Game centering key", { exact: true })
      .selectOption("F4");
    await page
      .getByLabel("Key modifiers", { exact: true })
      .selectOption("Ctrl+Shift+");
    await page
      .getByRole("spinbutton", { name: "Centering delay value", exact: true })
      .fill("2");
    assert.equal(
      await page
        .getByRole("button", {
          name: "Save profile before centering",
          exact: true,
        })
        .isDisabled(),
      true,
    );
    await page
      .getByRole("switch", { name: "Enable spatial overlays", exact: true })
      .click();
    await page.getByRole("button", { name: /^Save profile$/i }).click();
    await page
      .getByRole("button", { name: "Profile saved", exact: true })
      .waitFor();
    const waitingOverlays = await page.evaluate(() =>
      window.openGrid.overlayStatus("iracing"),
    );
    assert.equal(waitingOverlays.available, false);
    assert.equal(waitingOverlays.activeOverlayCount, 0);
    assert.equal(
      waitingOverlays.running,
      false,
      "No capture helper starts without an active XR session",
    );
    assert.equal(waitingOverlays.items[0].status, "waiting");
    await page
      .getByRole("button", { name: "Center VR in game", exact: true })
      .click();
    await page
      .locator(".toast.error")
      .filter({ hasText: /test mode/i })
      .waitFor();
    await page
      .getByRole("button", { name: "Dismiss message", exact: true })
      .click();
    await page
      .getByRole("switch", { name: "Enable spatial overlays", exact: true })
      .click();
    await page.getByRole("button", { name: /^Save profile$/i }).click();
    await page
      .getByRole("button", { name: "Profile saved", exact: true })
      .waitFor();
    await page.reload();
    await page
      .getByRole("button", { name: "Contour point 1", exact: true })
      .waitFor();
    await page
      .getByRole("button", { name: "Spatial overlays", exact: true })
      .click();
    assert.equal(
      await page.getByLabel("Overlay name", { exact: true }).inputValue(),
      "Race dashboard",
    );
    assert.equal(
      await page
        .getByRole("switch", { name: "Show in stream", exact: true })
        .getAttribute("aria-checked"),
      "true",
      "Stream visibility survives a save and reload",
    );
    assert.equal(
      await page
        .getByRole("spinbutton", {
          name: "Distance forward value",
          exact: true,
        })
        .inputValue(),
      "1.75",
    );
    assert.equal(
      await page.getByLabel("Game centering key", { exact: true }).inputValue(),
      "F4",
    );
    assert.equal(
      await page.getByLabel("Key modifiers", { exact: true }).inputValue(),
      "Ctrl+Shift+",
    );
    await page.screenshot({
      path: path.join(results, "12-spatial-overlays.png"),
    });
    await page.locator(".spatial-map-panel").scrollIntoViewIfNeeded();
    await page.screenshot({
      path: path.join(results, "14-spatial-placement.png"),
    });
    for (let index = 1; index < 15; index++)
      await page
        .getByRole("button", { name: "Add overlay", exact: true })
        .click();
    assert.equal(
      await page
        .getByRole("list", { name: "Configured overlays" })
        .getByRole("listitem")
        .count(),
      15,
    );
    assert.equal(
      await page
        .getByRole("button", { name: "Add overlay", exact: true })
        .isDisabled(),
      true,
    );
    await page
      .getByRole("button", { name: "Delete selected overlay", exact: true })
      .click();
    assert.equal(
      await page
        .getByRole("button", { name: "Add overlay", exact: true })
        .isDisabled(),
      false,
    );
    await page
      .getByRole("button", { name: "Add overlay", exact: true })
      .click();
    await page
      .getByRole("button", {
        name: "Select overlay Race dashboard",
        exact: true,
      })
      .click();
    await page.getByRole("button", { name: /^Save profile$/i }).click();
    await page
      .getByRole("button", { name: "Profile saved", exact: true })
      .waitFor();
    const spatialSaved = await page.evaluate(() => window.openGrid.load());
    const spatialProfile = spatialSaved.document.profiles.find(
      (profile) => profile.id === spatialSaved.document.selectedProfileId,
    );
    assert.equal(spatialProfile.spatialOverlays.items.length, 15);
    assert.equal(spatialProfile.spatialOverlays.enabled, false);
    assert.equal(spatialProfile.spatialOverlays.pixelBudgetMP, 24);
    assert.deepEqual(spatialProfile.centering, {
      gameShortcut: "Ctrl+Shift+F4",
      delaySeconds: 2,
    });
    const spatialFirst = spatialProfile.spatialOverlays.items[0];
    assert.deepEqual(
      [
        spatialFirst.x,
        spatialFirst.y,
        spatialFirst.z,
        spatialFirst.yaw,
        spatialFirst.widthMeters,
        spatialFirst.opacity,
      ],
      [-0.65, 0, -1.75, 25, 0.8, 85],
    );
    assert.equal(spatialFirst.width, 1280);
    assert.equal(spatialFirst.height, 720);
    assert.equal(spatialFirst.fps, 12);
    assert.equal(spatialFirst.showInStream, true);
    assert.ok(
      spatialProfile.spatialOverlays.items
        .slice(1)
        .every((item) => item.showInStream === false),
      "Other overlays retain their private defaults",
    );
    const spatialOff = await page.evaluate(() =>
      window.openGrid.overlayStatus("iracing"),
    );
    assert.equal(spatialOff.running, false);
    assert.ok(spatialOff.items.every((item) => item.effectiveFps === 0));
    await page.setViewportSize({ width: 1100, height: 760 });
    await page.screenshot({
      path: path.join(results, "13-spatial-compact.png"),
    });
    assert.equal(
      await page.evaluate(
        () => document.documentElement.scrollWidth > window.innerWidth + 1,
      ),
      false,
      "Spatial controls fit the compact desktop width",
    );
    await page.setViewportSize({ width: 1440, height: 950 });
    await page.getByRole("button", { name: "Profiles", exact: true }).click();
    await page.screenshot({ path: path.join(results, "04-profiles.png") });
    const exportPath = path.join(dir, "exported.json");
    await app.evaluate(({ dialog }, file) => {
      dialog.showSaveDialog = async () => ({ canceled: false, filePath: file });
    }, exportPath);
    await page
      .getByRole("button", { name: "Export as JSON", exact: true })
      .click();
    await page.waitForFunction(() => !document.querySelector(".toast.error"));
    await new Promise((resolve) => setTimeout(resolve, 300));
    const exported = JSON.parse(await fs.readFile(exportPath, "utf8"));
    assert.equal(exported.profile.mask.left.length, 11);
    assert.equal(exported.profile.stream.extraSymmetric, true);
    assert.equal(exported.profile.framegen.enabled, true);
    assert.equal(exported.profile.overlay.layout, "compact");
    assert.equal(exported.profile.sharpening.clamp, 0.045);
    assert.deepEqual(exported.profile.telemetry, {
      enabled: true,
      updateHz: 17,
    });
    assert.deepEqual(
      exported.profile.spatialOverlays,
      spatialProfile.spatialOverlays,
    );
    assert.deepEqual(exported.profile.centering, spatialProfile.centering);
    assert.deepEqual(exported.profile.nativeFoveation, {
      outerResolution: 45,
      focusWidth: 33,
      resolutionScale: 125,
    });
    assert.equal(
      exported.profile.stream.extraLeft,
      exported.profile.stream.extraRight,
    );
    await app.evaluate(({ dialog }, file) => {
      dialog.showOpenDialog = async () => ({
        canceled: false,
        filePaths: [file],
      });
    }, exportPath);
    await page.getByRole("button", { name: "Import", exact: true }).click();
    await page.getByRole("button", { name: /^Save profile$/i }).click();
    await page
      .getByRole("button", { name: "Profile saved", exact: true })
      .waitFor();
    const imported = await page.evaluate(() => window.openGrid.load());
    assert.equal(imported.document.profiles.length, 3);
    assert.equal(imported.document.profiles.at(-1).stream.extraSymmetric, true);
    assert.equal(
      imported.document.profiles.at(-1).framegen.toggleShortcut,
      "Ctrl+Alt+F8",
    );
    assert.equal(imported.document.profiles.at(-1).overlay.opacity, 75);
    assert.deepEqual(
      imported.document.profiles.at(-1).spatialOverlays,
      spatialProfile.spatialOverlays,
    );
    assert.deepEqual(
      imported.document.profiles.at(-1).centering,
      spatialProfile.centering,
    );
    const legacySpatialPath = path.join(dir, "legacy-spatial.json");
    const legacySpatial = structuredClone(exported);
    legacySpatial.profile.name = "Legacy spatial fixture";
    for (const item of legacySpatial.profile.spatialOverlays.items) {
      delete item.showInStream;
    }
    await fs.writeFile(legacySpatialPath, JSON.stringify(legacySpatial));
    await app.evaluate(({ dialog }, file) => {
      dialog.showOpenDialog = async () => ({
        canceled: false,
        filePaths: [file],
      });
    }, legacySpatialPath);
    await page.getByRole("button", { name: "Import", exact: true }).click();
    await page.getByRole("button", { name: /^Save profile$/i }).click();
    await page
      .getByRole("button", { name: "Profile saved", exact: true })
      .waitFor();
    await page
      .getByRole("button", { name: "Spatial overlays", exact: true })
      .click();
    assert.equal(
      await page
        .getByRole("switch", { name: "Show in stream", exact: true })
        .getAttribute("aria-checked"),
      "false",
      "A legacy imported overlay remains private without the optional field",
    );
    assert.equal(
      await page
        .getByRole("button", { name: "Profile saved", exact: true })
        .isDisabled(),
      true,
      "Viewing the legacy default does not change the profile",
    );
    await page.getByRole("button", { name: "Setup", exact: true }).click();
    await page.screenshot({ path: path.join(results, "05-setup.png") });
    const blocked = await page.evaluate(async () => {
      try {
        await window.openGrid.installRuntime();
        return false;
      } catch (e) {
        return e.message.includes("test mode");
      }
    });
    assert.ok(blocked, "Tests cannot modify registration");
    await page.reload();
    await page
      .getByRole("button", { name: "Contour point 1", exact: true })
      .waitFor();
    assert.equal(
      await page.getByRole("button", { name: /^Contour point \d+$/ }).count(),
      11,
    );
    await page.setViewportSize({ width: 1100, height: 760 });
    await page.screenshot({ path: path.join(results, "06-compact.png") });
    const overflow = await page.evaluate(
      () => document.documentElement.scrollWidth > window.innerWidth + 1,
    );
    assert.equal(overflow, false, "No horizontal overflow");
    assert.deepEqual(errors, []);
    console.log(
      "Desktop UI, contour edit, save/reload, file export/import, and registration isolation passed.",
    );
  } catch (error) {
    console.error(error);
    try {
      await app.evaluate(({ BrowserWindow }) =>
        BrowserWindow.getAllWindows()[0]?.show(),
      );
      const page = await app.firstWindow();
      await page.screenshot({ path: path.join(results, "failure.png") });
      console.error((await page.locator("body").innerText()).slice(0, 6000));
    } catch {}
    throw error;
  } finally {
    await app
      .evaluate(({ BrowserWindow }) => {
        for (const w of BrowserWindow.getAllWindows()) w.destroy();
      })
      .catch(() => {});
    await app.close();
    await fs.rm(dir, { recursive: true, force: true });
  }
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
