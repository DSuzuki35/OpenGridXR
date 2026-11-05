const { _electron: electron } = require("playwright-core");
const fs = require("node:fs/promises");
const path = require("node:path");
const os = require("node:os");
const assert = require("node:assert/strict");

(async () => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), "opengrid-framegen-ui-"));
  const env = { ...process.env, OPENGRIDXR_DATA_DIR: dir, OPENGRIDXR_TEST_MODE: "1" };
  delete env.ELECTRON_RUN_AS_NODE;
  const app = await electron.launch({ executablePath: require("electron"), args: [path.resolve(".")], env });
  try {
    const page = await app.firstWindow();
    page.setDefaultTimeout(8000);
    await page.emulateMedia({ reducedMotion: "no-preference" });
    const errors = [];
    page.on("pageerror", error => errors.push(error.message));
    await app.evaluate(({ shell }) => {
      globalThis.openedProjectUrls = [];
      shell.openExternal = async url => { globalThis.openedProjectUrls.push(url); };
    });
    await page.getByRole("button", { name: "Frame generation", exact: true }).click();
    await page.getByText("Derived from VectorXR", { exact: true }).waitFor();
    assert.match(await page.locator(".vectorxr-credit").innerText(), /DienerTech LLC/);
    assert.deepEqual(await app.evaluate(() => globalThis.openedProjectUrls), []);
    await page.getByRole("button", { name: "VectorXR original project", exact: true }).click();
    assert.deepEqual(await app.evaluate(() => globalThis.openedProjectUrls), ["https://github.com/DienerTech/vectorxr"]);
    // The privileged bridge never accepts a caller-selected URL.
    await page.evaluate(() => window.openGrid.openVectorXR("file:///C:/Windows/System32/cmd.exe"));
    assert.deepEqual(await app.evaluate(() => globalThis.openedProjectUrls), Array(2).fill("https://github.com/DienerTech/vectorxr"));

    const preview = page.locator(".synthesis-preview");
    await page.getByLabel("Synthesis mode", { exact: true }).selectOption("interpolate");
    assert.deepEqual(await preview.locator(".sequence-frame").evaluateAll(frames => frames.map(f => f.dataset.frameKind)), ["rendered", "generated", "rendered"]);
    assert.match(await preview.innerText(), /Between two known frames/);
    await fs.mkdir("test-results", { recursive: true });
    await page.screenshot({ path: "test-results/framegen-interpolation.png" });
    await page.getByLabel("Synthesis mode", { exact: true }).selectOption("extrapolate");
    assert.deepEqual(await preview.locator(".sequence-frame").evaluateAll(frames => frames.map(f => f.dataset.frameKind)), ["rendered", "rendered", "generated"]);
    assert.match(await preview.innerText(), /Projected beyond the latest frame/);
    assert.equal(await preview.locator(".generated .frame-time").innerText(), "t + 1.5");
    assert.match(await preview.innerText(), /NO MEASURED FPS OR LATENCY/);
    assert.notEqual(await preview.locator(".frame-motion-dot").evaluate(el => getComputedStyle(el).animationName), "none");
    await page.screenshot({ path: "test-results/framegen-extrapolation.png" });
    await page.emulateMedia({ reducedMotion: "reduce" });
    assert.equal(await preview.locator(".frame-motion-dot").evaluate(el => getComputedStyle(el).animationName), "none");
    assert.deepEqual(errors, []);
    console.log("Frame generation attribution, fixed external link, synthesis modes, animation and reduced-motion checks passed.");
  } finally {
    await app.evaluate(({ BrowserWindow }) => { for (const win of BrowserWindow.getAllWindows()) win.destroy(); }).catch(() => {});
    await app.close();
    await fs.rm(dir, { recursive: true, force: true });
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
