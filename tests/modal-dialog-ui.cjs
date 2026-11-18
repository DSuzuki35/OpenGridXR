const { _electron: electron } = require("playwright-core");
const fs = require("node:fs/promises");
const path = require("node:path");
const os = require("node:os");
const assert = require("node:assert/strict");

// Focus management regression for the native modal dialogs: initial focus,
// Escape, outside click, focus restoration and scrolling of tall content.
(async () => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), "opengrid-modal-ui-"));
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
    page.setDefaultTimeout(10000);
    page.on("pageerror", (error) => errors.push(error.message));
    await page.getByRole("img", { name: "Rectangular crop preview", exact: true }).waitFor();

    const focused = async (locator, message) => {
      const handle = await locator.elementHandle();
      await page
        .waitForFunction((element) => element === document.activeElement, handle, { timeout: 3000 })
        .catch(async () => {
          const active = await page.evaluate(() => document.activeElement?.outerHTML.slice(0, 160));
          throw new Error(`${message}; focused: ${active}`);
        });
    };
    const openDialogs = () => page.locator("dialog[open]").count();
    const isModal = () =>
      page.evaluate(() => document.querySelector("dialog[open]")?.matches(":modal") === true);

    // New profile: focus moves into the name field, Escape closes and restores focus.
    await page.getByRole("button", { name: "Profiles", exact: true }).click();
    const newButton = page.getByRole("button", { name: "New profile", exact: true });
    await newButton.click();
    const newDialog = page.getByRole("dialog", { name: "Create a profile", exact: true });
    await newDialog.waitFor();
    assert.equal(await isModal(), true, "New profile opens as a native modal dialog");
    await focused(newDialog.getByLabel("Profile name", { exact: true }), "New profile focuses the name field");
    await page.keyboard.press("Escape");
    await newDialog.waitFor({ state: "detached" });
    await focused(newButton, "Escape returns focus to New profile");

    // Outside click closes; a click inside the panel does not.
    await newButton.click();
    await newDialog.waitFor();
    await newDialog.getByRole("heading", { name: "Create a profile" }).click();
    assert.equal(await openDialogs(), 1, "Clicking inside the panel keeps the dialog open");
    await page.mouse.click(8, 8);
    await newDialog.waitFor({ state: "detached" });
    await focused(newButton, "Outside click returns focus to New profile");

    // Submitting creates the profile and also restores focus.
    await newButton.click();
    await newDialog.getByLabel("Profile name", { exact: true }).fill("Modal check");
    await page.keyboard.press("Enter");
    await newDialog.waitFor({ state: "detached" });
    await focused(newButton, "Creating a profile returns focus to New profile");
    await page.getByRole("heading", { name: "Modal check", exact: true }).waitFor();

    // Delete: the safe action is focused by default; Escape keeps the profile.
    const deleteButton = page.getByRole("button", { name: "Delete profile", exact: true });
    await deleteButton.click();
    const deleteDialog = page.getByRole("dialog", { name: "Delete this profile?", exact: true });
    await deleteDialog.waitFor();
    await focused(deleteDialog.getByRole("button", { name: "Keep profile", exact: true }), "Delete defaults to Keep profile");
    await page.keyboard.press("Escape");
    await deleteDialog.waitFor({ state: "detached" });
    await focused(deleteButton, "Escape returns focus to Delete profile");
    await page.getByRole("heading", { name: "Modal check", exact: true }).waitFor();

    // Help: Escape and focus restoration, then tall content scrolls in a short window.
    const helpButton = page.getByRole("button", { name: /Quick start/ });
    await helpButton.click();
    const helpDialog = page.getByRole("dialog", { name: "Quick start", exact: true });
    await helpDialog.waitFor();
    await focused(helpDialog.getByRole("button", { name: "Close help", exact: true }), "Help focuses its first control");
    await page.keyboard.press("Escape");
    await helpDialog.waitFor({ state: "detached" });
    await focused(helpButton, "Escape returns focus to the help button");

    const [width, height] = await page.evaluate(() => [innerWidth, innerHeight]);
    await page.setViewportSize({ width, height: 320 });
    try {
      await helpButton.click();
      await helpDialog.waitFor();
      const layout = await page.evaluate(() => {
        const dialog = document.querySelector("dialog[open]");
        const panel = dialog.querySelector(".modal");
        dialog.scrollTop = 0;
        const top = panel.getBoundingClientRect().top;
        dialog.scrollTop = dialog.scrollHeight;
        return { scrollable: dialog.scrollHeight > dialog.clientHeight, top, scrolled: dialog.scrollTop };
      });
      assert.equal(layout.scrollable, true, "Tall dialog content scrolls inside the dialog");
      assert.ok(layout.top >= 0, `Dialog top stays reachable (${layout.top})`);
      assert.ok(layout.scrolled > 0, "Dialog can scroll to its last action");
      const back = helpDialog.getByRole("button", { name: /Back to the grid/ });
      const box = await back.boundingBox();
      assert.ok(box && box.y >= 0 && box.y + box.height <= 320, `Last action is visible after scrolling: ${JSON.stringify(box)}`);
      await back.click();
      await helpDialog.waitFor({ state: "detached" });
      await focused(helpButton, "Closing help returns focus to the help button");
    } finally {
      await page.setViewportSize({ width, height });
    }

    // Binding capture: Escape cancels and restores focus; outside clicks are ignored.
    await page.getByRole("button", { name: "Spatial overlays", exact: true }).click();
    const bind = page.getByRole("button", { name: "Bind controller: Toggle all spatial overlays", exact: true });
    await bind.click();
    const bindDialog = page.getByRole("dialog", { name: "Bind controller input", exact: true });
    await bindDialog.waitFor();
    await focused(bindDialog.getByRole("button", { name: "Close binding capture", exact: true }), "Capture focuses its close button");
    await page.mouse.click(8, 8);
    assert.equal(await openDialogs(), 1, "Outside click does not cancel a capture");
    await page.keyboard.press("Escape");
    await bindDialog.waitFor({ state: "detached" });
    await focused(bind, "Escape returns focus to the binding button");

    assert.deepEqual(errors, []);
    console.log("Modal dialogs: initial focus, Escape, outside click, focus restore and tall-content scrolling passed.");
  } catch (error) {
    const page = await app.firstWindow();
    console.error((await page.locator("body").innerText()).slice(-3000));
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
