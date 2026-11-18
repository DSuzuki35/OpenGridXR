const { _electron: electron } = require("playwright-core");
const fs = require("node:fs/promises");
const path = require("node:path");
const os = require("node:os");
const assert = require("node:assert/strict");

(async () => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), "opengrid-support-ui-"));
  const env = { ...process.env, OPENGRIDXR_DATA_DIR: dir, OPENGRIDXR_TEST_MODE: "1" };
  delete env.ELECTRON_RUN_AS_NODE;
  const app = await electron.launch({ executablePath: process.env.OPENGRIDXR_TEST_EXE || require("electron"), args: process.env.OPENGRIDXR_TEST_EXE ? [] : [path.resolve(".")], env });
  try {
    const page = await app.firstWindow();
    page.setDefaultTimeout(8000);
    const errors = [];
    page.on("pageerror", error => errors.push(error.message));
    await app.evaluate(({ shell }) => {
      globalThis.openedSupportUrls = [];
      shell.openExternal = async url => { globalThis.openedSupportUrls.push(url); };
    });
    await page.getByRole("button", { name: "Setup", exact: true }).click();
    const support = page.getByRole("region", { name: "Optional support", exact: true });
    await support.waitFor();
    assert.equal(await support.evaluate(node => !!node.closest('.sidebar-bottom')), true, "Support links live in the persistent sidebar footer");
    assert.match(await support.innerText(), /affiliate/i);
    assert.deepEqual(await app.evaluate(() => globalThis.openedSupportUrls), []);
    await support.getByRole("button", { name: "Buy me a coffee", exact: true }).click();
    await support.getByRole("button", { name: "Affiliate links", exact: true }).click();
    const destinations = ["https://buymeacoffee.com/dansuzuki", "https://dansuzuki.com/discounts/"];
    assert.deepEqual(await app.evaluate(() => globalThis.openedSupportUrls), destinations);
    // Renderer arguments must never select a different privileged destination.
    await page.evaluate(async () => {
      await window.openGrid.openBuyMeACoffee("file:///C:/Windows/System32/cmd.exe");
      await window.openGrid.openAffiliateLinks("https://example.com/");
    });
    assert.deepEqual(await app.evaluate(() => globalThis.openedSupportUrls), [...destinations, ...destinations]);
    await fs.mkdir("test-results", { recursive: true });
    await support.screenshot({ path: "test-results/support-panel.png" });
    await page.getByRole("button", { name: "Profiles", exact: true }).click();
    assert.equal(await support.isVisible(), true, "Support remains available outside Setup");
    await page.setViewportSize({ width: 800, height: 700 });
    assert.equal(await support.getByRole("button", { name: "Affiliate links", exact: true }).isVisible(), true, "Compact sidebar keeps an accessible support button");
    await page.setViewportSize({ width: 1280, height: 800 });
    await app.evaluate(({ shell }) => { shell.openExternal = async () => { throw new Error("Browser unavailable"); }; });
    await support.getByRole("button", { name: "Buy me a coffee", exact: true }).click();
    await support.getByRole("alert").filter({ hasText: "Could not open your browser. Please try again." }).waitFor();
    assert.deepEqual(errors, []);
    console.log("Optional support destinations, fixed external IPC and opening failure feedback passed.");
  } finally {
    await app.evaluate(({ BrowserWindow }) => { for (const win of BrowserWindow.getAllWindows()) win.destroy(); }).catch(() => {});
    await app.close();
    await fs.rm(dir, { recursive: true, force: true });
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
