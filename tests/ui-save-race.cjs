const { _electron: electron } = require("playwright-core");
const fs = require("node:fs/promises");
const path = require("node:path");
const os = require("node:os");
const assert = require("node:assert/strict");

(async () => {
  const directory = await fs.mkdtemp(
    path.join(os.tmpdir(), "opengrid-save-race-"),
  );
  const env = {
    ...process.env,
    OPENGRIDXR_DATA_DIR: directory,
    OPENGRIDXR_TEST_MODE: "1",
  };
  delete env.ELECTRON_RUN_AS_NODE;
  const app = await electron.launch({
    executablePath: process.env.OPENGRIDXR_TEST_EXE || require("electron"),
    args: process.env.OPENGRIDXR_TEST_EXE ? [] : [path.resolve(".")],
    env,
    timeout: 60000,
  });
  try {
    const page = await app.firstWindow();
    const crop = page.getByRole("spinbutton", { name: "Left", exact: true });
    await crop.waitFor({ timeout: 20000 });
    await crop.fill("10");
    const cropBox = await crop.boundingBox();
    const gameBox = await page.locator("#game-select").boundingBox();

    // Delay the real Electron handler's response, preserving real persistence,
    // sender checks, revision handling and native config writing underneath.
    // Electron's internal handler map is used only by this isolated test.
    await app.evaluate(({ ipcMain }) => {
      const original = ipcMain._invokeHandlers.get("opengrid:save");
      if (typeof original !== "function")
        throw new Error("Expected the real save IPC handler.");
      ipcMain.removeHandler("opengrid:save");
      let first = true;
      ipcMain.handle("opengrid:save", async (event, ...args) => {
        const result = await original(event, ...args);
        if (first) {
          first = false;
          globalThis.uiSavePending = true;
          await new Promise((resolve) => {
            globalThis.releaseUiSave = resolve;
          });
        }
        return result;
      });
    });

    await page
      .getByRole("button", { name: "Save profile", exact: true })
      .click();
    for (let attempts = 0; attempts < 100; attempts++) {
      if (await app.evaluate(() => globalThis.uiSavePending === true)) break;
      await new Promise((resolve) => setTimeout(resolve, 25));
    }
    assert.equal(
      await app.evaluate(() => globalThis.uiSavePending),
      true,
      "Save response is held in flight",
    );

    // Real mouse/keyboard events must not edit the draft while its older
    // snapshot is awaiting acceptance. Inert alone and overlay alone each
    // cover a different input path, so verify both through actual events.
    await page.mouse.click(
      cropBox.x + cropBox.width / 2,
      cropBox.y + cropBox.height / 2,
    );
    await page.keyboard.press("ArrowUp");
    assert.equal(
      await crop.inputValue(),
      "10",
      "A pending save blocks pointer/keyboard crop edits",
    );
    await page.mouse.click(
      gameBox.x + gameBox.width / 2,
      gameBox.y + gameBox.height / 2,
    );
    await page.keyboard.press("ArrowDown");
    await page.keyboard.press("Enter");
    assert.equal(
      await page.locator("#game-select").inputValue(),
      "iracing",
      "A pending save blocks game selection",
    );
    assert.equal(
      await page.locator(".workspace").evaluate((element) => element.inert),
      true,
      "Workspace is inaccessible to keyboard editing",
    );
    assert.equal(
      await page.locator(".sidebar").evaluate((element) => element.inert),
      true,
      "Navigation cannot switch editing context",
    );
    assert.equal(
      await page
        .getByRole("status")
        .filter({ hasText: "Saving your profile" })
        .count(),
      1,
      "Accessible progress remains available",
    );

    await app.evaluate(() => globalThis.releaseUiSave());
    await page
      .getByRole("button", { name: "Profile saved", exact: true })
      .waitFor();
    const state = await page.evaluate(() => window.openGrid.load());
    assert.equal(
      state.document.profiles[0].view.crop.left,
      10,
      "The submitted snapshot was persisted",
    );
    assert.equal(
      await crop.inputValue(),
      "10",
      "Accepting the response cannot erase a newer user edit",
    );
    assert.equal(
      await page.locator(".workspace").evaluate((element) => element.inert),
      false,
      "Workspace becomes editable after save",
    );
    await crop.fill("11");
    await page
      .getByRole("button", { name: "Save profile", exact: true })
      .click();
    await page
      .getByRole("button", { name: "Profile saved", exact: true })
      .waitFor();
    const next = await page.evaluate(() => window.openGrid.load());
    assert.equal(
      next.document.profiles[0].view.crop.left,
      11,
      "Editing and saving resumes normally",
    );
    console.log(
      "Delayed real save IPC blocks edits and selection; editing resumes without lost changes.",
    );
  } finally {
    await app
      .evaluate(({ BrowserWindow }) => {
        globalThis.releaseUiSave?.();
        for (const win of BrowserWindow.getAllWindows()) win.destroy();
      })
      .catch(() => {});
    await app.close();
    await fs.rm(directory, { recursive: true, force: true });
  }
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
