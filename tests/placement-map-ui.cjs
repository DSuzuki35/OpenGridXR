const { _electron: electron } = require("playwright-core");
const fs = require("node:fs/promises");
const path = require("node:path");
const os = require("node:os");
const http = require("node:http");
const assert = require("node:assert/strict");

(async () => {
  const { build } = await import("vite");
  const fixtureId = path.resolve("tests/__placement_fixture__.jsx").replaceAll("\\", "/");
  const bundle = await build({ logLevel: "error", base: "/", plugins: [{
    name: "placement-test-entry",
    resolveId(id) { if (id === "virtual:placement-fixture") return fixtureId; },
    load(id) { if (id === fixtureId) return `
      import React, { useState } from "react";
      import { createRoot } from "react-dom/client";
      import PlacementMap from "../src/components/PlacementMap.jsx";
      import { Range } from "../src/components/Controls.jsx";
      import { createSpatialOverlay } from "../src/spatial-overlays.mjs";
      import "../src/styles.css";
      function Fixture() {
        const [items, setItems] = useState([
          {...createSpatialOverlay(), id: "alpha", name: "Alpha", x: 0.1234, y: 0.4, z: -3},
          {...createSpatialOverlay(), id: "bravo", name: "Bravo", x: -2, y: -2, z: -6}
        ]);
        const [selectedId, setSelectedId] = useState("alpha");
        const [disabled, setDisabled] = useState(false);
        const [profileKey, setProfileKey] = useState("one");
        const selected = items.find(item => item.id === selectedId);
        const change = (id, patch) => setItems(current => current.map(item => item.id === id ? {...item, ...patch} : item));
        return <main style={{width: 900, margin: "20px auto"}}>
          <PlacementMap items={items} selected={selected} onSelect={setSelectedId} onChange={change} disabled={disabled} profileKey={profileKey}/>
          <Range label="Horizontal position" unit="m" value={selected.x} min={-5} max={5} step={0.01} onChange={x => change(selected.id, {x})}/>
          <Range label="Vertical position" unit="m" value={selected.y} min={-5} max={5} step={0.01} onChange={y => change(selected.id, {y})}/>
          <Range label="Distance forward" unit="m" value={-selected.z} min={0.1} max={10} step={0.01} onChange={z => change(selected.id, {z: -z})}/>
          <button onClick={() => setDisabled(value => !value)}>Toggle busy</button>
          <button onClick={() => setProfileKey(value => value === "one" ? "two" : "one")}>Switch profile</button>
          <output>{JSON.stringify({items, selectedId, disabled, profileKey})}</output>
        </main>;
      }
      createRoot(document.getElementById("root")).render(<Fixture/>);
    `; },
  }], build: { write: false, cssCodeSplit: false, rollupOptions: { input: "virtual:placement-fixture", output: { entryFileNames: "fixture.js", assetFileNames: "[name][extname]" } } } });
  const assets = new Map(bundle.output.map(item => ["/" + item.fileName, { body: item.type === "chunk" ? item.code : item.source, type: item.fileName.endsWith(".css") ? "text/css" : "text/javascript" }]));
  for (const font of ["archivo", "inter", "dm-mono"]) assets.set(`/fonts/${font}.woff2`, { body: await fs.readFile(path.resolve("public/fonts", `${font}.woff2`)), type: "font/woff2" });
  assets.set("/", { type: "text/html", body: `<html><head>${bundle.output.filter(item => item.fileName.endsWith(".css")).map(item => `<link rel="stylesheet" href="/${item.fileName}">`).join("")}</head><body><div id="root"></div><script type="module" src="/fixture.js"></script></body></html>` });
  const server = http.createServer((req, res) => {
    const asset = assets.get(new URL(req.url, "http://127.0.0.1").pathname);
    res.writeHead(asset ? 200 : 404, { "Content-Type": asset?.type || "text/plain" }); res.end(asset?.body || "");
  });
  await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), "opengrid-placement-ui-"));
  const entry = path.join(dir, "main.cjs");
  await fs.writeFile(entry, 'const {app,BrowserWindow}=require("electron");app.whenReady().then(()=>{new BrowserWindow({width:1180,height:1000,webPreferences:{contextIsolation:true}}).loadURL("about:blank")});');
  const env = { ...process.env }; delete env.ELECTRON_RUN_AS_NODE;
  let app;
  try {
    app = await electron.launch({ executablePath: require("electron"), args: [entry], env });
    const page = await app.firstWindow(); page.setDefaultTimeout(10000);
    const errors = []; page.on("pageerror", error => errors.push(error.message));
    await page.goto(`http://127.0.0.1:${server.address().port}/`, { waitUntil: "commit" });
    const overlay = name => page.getByRole("button", { name: `Move overlay ${name}`, exact: true });
    await overlay("Alpha").waitFor();
    // Font swaps can move the SVG while the first coordinate-based drag runs.
    await page.evaluate(() => document.fonts.ready);
    const state = async () => JSON.parse(await page.locator("output").textContent());
    const positions = async () => (await state()).items.map(({ x, y, z }) => ({ x, y, z }));
    const svgPoint = async (x, y) => page.locator(".spatial-map-panel > svg").evaluate((svg, point) => {
      const p = new DOMPoint(point.x, point.y).matrixTransform(svg.getScreenCTM()); return { x: p.x, y: p.y };
    }, { x, y });
    const startDrag = async name => {
      const p = await overlay(name).evaluate(element => { const p = new DOMPoint(0, 0).matrixTransform(element.getScreenCTM()); return { x: p.x, y: p.y }; });
      await page.mouse.move(p.x, p.y); await page.mouse.down();
    };
    const dragTo = async (name, x, y) => { await startDrag(name); const p = await svgPoint(x, y); await page.mouse.move(p.x, p.y, { steps: 5 }); await page.mouse.up(); };

    await overlay("Alpha").click();
    assert.deepEqual((await positions())[0], { x: 0.1234, y: 0.4, z: -3 }, "Selecting an overlay without dragging preserves its full precision");
    await dragTo("Alpha", 438, 30 + 5 / 9.9 * 230);
    assert.deepEqual(await positions(), [{ x: 2, y: 0.4, z: -5 }, { x: -2, y: -2, z: -6 }], "Top drag edits X/Z only and leaves other overlay untouched");
    assert.equal(await page.getByRole("spinbutton", { name: "Horizontal position value", exact: true }).inputValue(), "2");
    assert.equal(await page.getByRole("spinbutton", { name: "Distance forward value", exact: true }).inputValue(), "5");
    await page.getByRole("button", { name: "Front · X/Y", exact: true }).click();
    await dragTo("Alpha", 276, 99);
    assert.deepEqual((await positions())[0], { x: -1, y: 2, z: -5 }, "Front drag edits X/Y and preserves depth");
    assert.equal(await page.getByRole("spinbutton", { name: "Vertical position value", exact: true }).inputValue(), "2");
    await overlay("Bravo").click();
    assert.equal((await state()).selectedId, "bravo", "Map click changes editor selection");
    await overlay("Bravo").press("Shift+ArrowUp");
    assert.deepEqual((await positions())[1], { x: -2, y: -1.9, z: -6 }, "Keyboard uses visible front axis");
    await page.getByRole("spinbutton", { name: "Horizontal position value", exact: true }).fill("3");
    assert.match(await overlay("Bravo").getAttribute("transform"), /^translate\(492 /, "Numeric editor changes are reflected on map");
    await dragTo("Bravo", 650, 5);
    assert.deepEqual((await positions())[1], { x: 5, y: 5, z: -6 }, "Front dragging beyond grid is clamped");
    await overlay("Bravo").press("ArrowRight");
    assert.equal((await positions())[1].x, 5);
    await page.getByRole("button", { name: "Top · X/Z", exact: true }).click();
    await dragTo("Bravo", 5, 305);
    assert.deepEqual((await positions())[1], { x: -5, y: 5, z: -0.1 }, "Top dragging beyond grid is clamped");
    await overlay("Bravo").press("ArrowUp");
    assert.deepEqual((await positions())[1], { x: -5, y: 5, z: -0.11 });
    const beforeCancel = await positions();
    await startDrag("Bravo");
    const moved = await svgPoint(330, 145); await page.mouse.move(moved.x, moved.y);
    await page.keyboard.press("Escape"); await page.mouse.up();
    assert.deepEqual(await positions(), beforeCancel, "Escape restores the original drag position");
    await page.getByRole("button", { name: "Toggle busy", exact: true }).click();
    await overlay("Bravo").press("ArrowUp");
    await dragTo("Bravo", 330, 145);
    assert.deepEqual(await positions(), beforeCancel, "Busy state blocks pointer and keyboard edits");
    await page.getByRole("button", { name: "Toggle busy", exact: true }).click();
    await startDrag("Alpha");
    await page.getByRole("button", { name: "Switch profile", exact: true }).evaluate(button => button.click());
    await page.mouse.move(moved.x, moved.y); await page.mouse.up();
    assert.deepEqual(await positions(), beforeCancel, "A profile switch cancels captured gestures");
    assert.deepEqual(errors, []);
    await fs.mkdir(path.resolve("test-results"), { recursive: true });
    await page.screenshot({ path: path.resolve("test-results/placement-map-top.png"), fullPage: true });
    await page.getByRole("button", { name: "Front · X/Y", exact: true }).click();
    await page.screenshot({ path: path.resolve("test-results/placement-map-front.png"), fullPage: true });
    console.log("Placement map: top/front drags, preserved axes, selection, numeric synchronization, bounds, keyboard, cancellation and busy/profile guards passed.");
  } finally {
    if (app) await app.close();
    await new Promise(resolve => server.close(resolve));
    await fs.rm(dir, { recursive: true, force: true });
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
