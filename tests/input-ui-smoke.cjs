const { _electron: electron } = require("playwright-core");
const fs = require("node:fs/promises");
const path = require("node:path");
const os = require("node:os");
const assert = require("node:assert/strict");

(async () => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), "opengrid-input-ui-"));
  const env = {
    ...process.env,
    OPENGRIDXR_DATA_DIR: dir,
    OPENGRIDXR_TEST_MODE: "1",
    OPENGRIDXR_INPUT_TEST_FIXTURE: "1",
  };
  delete env.ELECTRON_RUN_AS_NODE;
  const app = await electron.launch({
    executablePath: process.env.OPENGRIDXR_TEST_EXE || require("electron"),
    args: process.env.OPENGRIDXR_TEST_EXE ? [] : [path.resolve(".")],
    env,
    timeout: 30000,
  });
  const errors = [];
  try {
    const page = await app.firstWindow();
    page.on("pageerror", (error) => errors.push(error.message));
    await page
      .getByRole("button", { name: "Contour point 1", exact: true })
      .waitFor();
    await page.getByRole("button", { name: "Stream", exact: true }).click();
    assert.equal(await page.getByRole("button", { name: /^Bind controller:/ }).count(), 0, "Stream setup has no controller binding buttons");
    await page.getByRole("switch", { name: "Mirror output", exact: true }).click();
    assert.equal(await page.getByRole("switch", { name: "Mirror output", exact: true }).getAttribute("aria-checked"), "true", "Editor setup toggles remain editable");
    await page.getByRole("button", { name: "Performance HUD", exact: true }).click();
    const resolution = page.getByRole("switch", { name: "Show render resolution & scale", exact: true });
    const preview = page.locator(".osd-preview");
    assert.equal(await preview.getByText("Unavailable", { exact: true }).count(), 1, "Desktop preview cannot invent actual eye resolution");
    assert.equal(await preview.getByText("Configured render scale", { exact: true }).count(), 1);
    await resolution.click();
    assert.equal(await preview.getByText("Rendered / eye", { exact: true }).count(), 0, "Resolution checkbox hides preview metrics");
    await resolution.click();
    await page.getByRole("group", { name: "Overlay layout", exact: true }).getByRole("button", { name: "Compact", exact: true }).click();
    assert.equal(await preview.getByText("Rendered / eye", { exact: true }).count(), 1, "Compact mode retains the optional resolution row");
    await page.getByRole("group", { name: "Overlay layout", exact: true }).getByRole("button", { name: "Expanded", exact: true }).click();
    await page.getByRole("button", { name: "Spatial overlays", exact: true }).click();
    const press = async () => {
      await page.waitForFunction(
        async () => (await window.openGrid.inputStatus()).available,
      );
      await app.evaluate(() => {
        const child = process
          ._getActiveHandles()
          .find(
            (handle) =>
              handle.spawnfile?.endsWith("OpenGridXR-Input.exe") &&
              handle.spawnargs?.includes("--fixture"),
          );
        if (!child)
          throw new Error("Isolated native input fixture is not running.");
        child.stdin.write('{"op":"fixture-press"}\n');
      });
    };
    await page
      .getByRole("button", {
        name: "Bind controller: Toggle all spatial overlays",
        exact: true,
      })
      .click();
    await press();
    await page
      .getByText(
        "Binding added to your draft. Save the profile to use it while driving.",
        { exact: true },
      )
      .waitFor();
    await page.getByRole("button", { name: "Done", exact: true }).click();
    await page
      .getByRole("button", { name: "Save profile", exact: true })
      .click();
    await page
      .getByRole("button", { name: "Profile saved", exact: true })
      .waitFor();
    const bound = await page.evaluate(() => window.openGrid.load());
    assert.equal(
      bound.document.profiles[0].inputBindings.bindings[0].index,
      127,
    );
    await press();
    await page.waitForFunction(
      () =>
        document
          .querySelector('[role="switch"][aria-label="Enable spatial overlays"]')
          ?.getAttribute("aria-checked") === "true",
    );
    assert.equal(
      await page
        .getByRole("button", { name: "Profile saved", exact: true })
        .isDisabled(),
      true,
      "Saved input event updates the clean editor",
    );
    const saved = await page.evaluate(() => window.openGrid.load());
    await page
      .getByRole("spinbutton", { name: "Source pixel budget value", exact: true })
      .fill("30");
    await press();
    await new Promise((resolve) => setTimeout(resolve, 200));
    assert.equal(
      (await page.evaluate(() => window.openGrid.load())).document.profiles[0]
        .spatialOverlays.enabled,
      true,
      "Dirty editor pauses native fixture actions",
    );
    const stale = structuredClone(saved);
    stale.document.profiles[0].spatialOverlays.enabled = false;
    await app.evaluate(
      ({ BrowserWindow }, state) =>
        BrowserWindow.getAllWindows()[0].webContents.send(
          "opengrid:inputProfile",
          state,
        ),
      stale,
    );
    await new Promise((resolve) => setTimeout(resolve, 100));
    assert.equal(
      await page
        .getByRole("switch", { name: "Enable spatial overlays", exact: true })
        .getAttribute("aria-checked"),
      "true",
      "Late input event cannot overwrite dirty profile",
    );
    assert.equal(
      await page
        .getByRole("spinbutton", { name: "Source pixel budget value", exact: true })
        .inputValue(),
      "30",
    );
    await page
      .getByRole("button", { name: "Save profile", exact: true })
      .click();
    await page
      .getByRole("button", { name: "Profile saved", exact: true })
      .waitFor();
    // Dynamic overlay binding uses a separate synthetic descriptor; no capture source is enabled.
    await page
      .getByRole("button", { name: "Spatial overlays", exact: true })
      .click();
    await page
      .getByRole("button", { name: "Add overlay", exact: true })
      .click();
    await page
      .getByRole("button", { name: "Save profile", exact: true })
      .click();
    await page
      .getByRole("button", { name: "Profile saved", exact: true })
      .waitFor();
    const state = await page.evaluate(() => window.openGrid.load());
    const profile = state.document.profiles[0],
      overlayId = profile.spatialOverlays.items[0].id;
    profile.inputBindings.bindings.push({
      ...profile.inputBindings.bindings[0],
      actionId: `spatialOverlays.${overlayId}.enabled`,
      index: 126,
    });
    const updated = await page.evaluate(
      (state) => window.openGrid.save(state.document, state.revision),
      state,
    );
    await app.evaluate(
      ({ BrowserWindow }, state) =>
        BrowserWindow.getAllWindows()[0].webContents.send(
          "opengrid:inputProfile",
          state,
        ),
      updated,
    );
    await page
      .getByRole("button", { name: "Delete selected overlay", exact: true })
      .click();
    await page
      .getByRole("button", { name: "Save profile", exact: true })
      .click();
    await page
      .getByRole("button", { name: "Profile saved", exact: true })
      .waitFor();
    const cleaned = await page.evaluate(() => window.openGrid.load());
    assert.equal(
      cleaned.document.profiles[0].inputBindings.bindings.length,
      1,
      "Deleting an overlay removes only its bindings",
    );
    assert.equal(
      cleaned.document.profiles[0].inputBindings.bindings[0].actionId,
      "spatialOverlays.enabled",
    );
    await page
      .getByRole("button", { name: "Controller bindings", exact: true })
      .click();
    await fs.mkdir(path.resolve("test-results"), { recursive: true });
    await page.screenshot({
      path: path.resolve("test-results/16-controller-bindings.png"),
    });
    assert.deepEqual(errors, []);
    console.log(
      "Native fixture learn/save/action, dirty event guard, and bound overlay deletion passed.",
    );
  } catch (error) {
    const page = await app.firstWindow();
    console.error((await page.locator("body").innerText()).slice(-5000));
    throw error;
  } finally {
    await app
      .evaluate(({ BrowserWindow }) => {
        for (const window of BrowserWindow.getAllWindows()) window.destroy();
      })
      .catch(() => {});
    await app.close();
    await fs.rm(dir, { recursive: true, force: true });
  }
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
