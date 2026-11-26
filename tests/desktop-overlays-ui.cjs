const { _electron } = require("playwright-core");
const fs = require("node:fs/promises");
const path = require("node:path");
const os = require("node:os");
const http = require("node:http");
const assert = require("node:assert/strict");
(async () => {
  const { hasWheelLights, hasWheelScreen, wheelLightDevice, wheelModels } = await import("../src/spatial-overlays.mjs");
  const { build } = await import("vite");
  const fixtureId = path.resolve("tests/__desktop_fixture__.jsx").replaceAll("\\", "/");
  const bundle = await build({ logLevel: "error", plugins: [{ name: "desktop-fixture", resolveId(id) { if (id === "virtual:desktop-fixture") return fixtureId; }, load(id) {
    if (id === fixtureId) return `import React,{useState} from "react";import{createRoot}from"react-dom/client";
      import Panel from"../src/components/SpatialOverlaysPanel.jsx";import{createProfile}from"../src/model.mjs";import{createSpatialOverlay}from"../src/spatial-overlays.mjs";
      import {Context} from "../src/components/BindingContext.jsx";import{listToggleActions} from "../src/input-bindings.mjs";
      function Fixture(){const[p,set]=useState(()=>{const p=createProfile();p.spatialOverlays.items=[{...createSpatialOverlay(),kind:"desktop"}];return p;});
      return <Context.Provider value={{actions:listToggleActions(p),settings:p.inputBindings,learn(){}}}><Panel profile={p} onChange={patch=>set(p=>({...p,spatialOverlays:{...p.spatialOverlays,...patch}}))} run={async(_,fn)=>fn()} busy={false} dirty={false}/><output aria-label="Fixture profile">{JSON.stringify(p)}</output></Context.Provider>;}
      createRoot(document.getElementById("root")).render(<Fixture/>);`;
  } }], build: { write: false, rollupOptions: { input: "virtual:desktop-fixture", output: { entryFileNames: "fixture.js" } } } });
  const assets = new Map(bundle.output.map(item => ["/" + item.fileName, item.type === "chunk" ? item.code : item.source]));
  const server = http.createServer((req, res) => { const body = req.url === "/" ? '<!doctype html><meta charset="utf-8"><div id="root"></div><script type="module" src="/fixture.js"></script>' : assets.get(req.url); res.setHeader("Content-Type", req.url === "/" ? "text/html" : "text/javascript"); res.end(body || ""); });
  await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), "opengrid-desktop-ui-")), entry = path.join(dir, "main.cjs");
  await fs.writeFile(entry, 'const{app,BrowserWindow}=require("electron");app.whenReady().then(()=>new BrowserWindow({show:false,webPreferences:{contextIsolation:true}}).loadURL("about:blank"));');
  const env = { ...process.env }; delete env.ELECTRON_RUN_AS_NODE;
  let app;
  try {
    app = await _electron.launch({ executablePath: require("electron"), args: [entry], env });
    const page = await app.firstWindow(); page.setDefaultTimeout(3000);
    await page.addInitScript(() => { window.openGrid = {
      overlayStatus: async () => window.fixtureStatus || ({ items: [], shortcutErrors: [] }),
      steeringInputStatus: async () => ({available:true,devices:[{id:"dinput:{00000000-0000-0000-0000-000000000001}",name:"Test wheelbase",axes:["x","rx"]}],samples:{"dinput:{00000000-0000-0000-0000-000000000001}":{x:window.fixtureSteering ?? 0,rx:0}}}),
      steeringInputStopDiscovery: async () => {},
      recenterOverlays: async game => { window.recenteredGame = game; return { message: "Recentered" }; },
      importWheelModel: async () => ({id:"f".repeat(64),name:"My rim"}),
      listOverlayDisplays: async () => [{ id: "-42", name: "4:3 screen", x: -1600, y: 0, width: 1600, height: 1200 }, { id: "-43", name: "Portrait screen", x: 0, y: 0, width: 1080, height: 1920 }],
      selectOverlayDisplay: async (_, id, displayId) => ({ id: displayId, name: "Selected screen", ...(displayId === "-42" ? { width: 1600, height: 1200 } : { width: 1080, height: 1920 }) }),
      listOverlayWindows: async () => [{ id: "window:3:0", name: "Telemetry window" }],
      selectOverlayWindow: async () => ({ id: "window:3:0", name: "Telemetry window" }),
    }; });
    await page.goto(`http://127.0.0.1:${server.address().port}/`);
    assert.equal(await page.getByLabel("Recenter overlays key", { exact: true }).count(), 1, "The editor offers an overlay recenter shortcut");
    assert.equal(await page.getByLabel("Reset position key", { exact: true }).count(), 0, "Desktop capture has no dashboard reset");
    await page.getByRole("button", { name: "Refresh monitors", exact: true }).click();
    await page.getByLabel("Current monitors").selectOption("-42");
    await page.getByRole("button", { name: "Use this monitor", exact: true }).click();
    const state = async () => JSON.parse(await page.getByLabel("Fixture profile").textContent());
    const occlusion = page.getByRole("switch", { name: "Cockpit occlusion", exact: true });
    assert.equal(await occlusion.count(), 0);
    assert.equal((await state()).spatialOverlays.items[0].displayId, "-42");
    assert.match(await page.getByLabel("Selected source", { exact: true }).innerText(), /Selected screen/);
    assert.match(await page.getByLabel("Selected source", { exact: true }).innerText(), /1600 × 1200/);
    assert.equal(await page.getByLabel("Current monitors").inputValue(), "", "picker may clear without losing selected source identity");
    await page.evaluate(id => { window.fixtureStatus = { items: [{ id, sourceKind: "desktop", sourceReference: "-42", source: null }], shortcutErrors: [] }; }, (await state()).spatialOverlays.items[0].id);
    await page.waitForFunction(() => document.querySelector('[aria-label="Selected source"]').textContent.includes("Select a source for this app run"));
    assert.deepEqual([(await state()).spatialOverlays.items[0].width, (await state()).spatialOverlays.items[0].height], [1440, 1080], "4:3 monitor selection preserves its natural aspect within capture limits");
    await page.getByLabel("Current monitors").selectOption("-43");
    await page.getByRole("button", { name: "Use this monitor", exact: true }).click();
    assert.deepEqual([(await state()).spatialOverlays.items[0].width, (await state()).spatialOverlays.items[0].height], [608, 1080], "portrait monitor selection fits within capture limits");
    assert.equal(await page.getByRole("switch", { name: "Wink clicks" }).count(), 0);
    assert.equal(await page.getByLabel("Desktop pointing mode").locator('option[value="gaze"]').count(), 0);
    await page.getByLabel("Desktop pointing mode").selectOption("head");
    await page.getByRole("spinbutton", { name: "Precision zoom value", exact: true }).fill("225");
    assert.equal((await state()).spatialOverlays.items[0].precisionScale, 2.25);
    await page.getByLabel("Precision hold key", { exact: true }).selectOption("F7");
    await page.getByLabel("Precision key modifiers", { exact: true }).selectOption("Ctrl+Alt+");
    assert.equal((await state()).spatialOverlays.items[0].precisionShortcut, "Ctrl+Alt+F7");
    await page.getByLabel("Desktop visibility key", { exact: true }).selectOption("F9");
    await page.getByLabel("Desktop key modifiers", { exact: true }).selectOption("Ctrl+Alt+");
    assert.equal((await state()).spatialOverlays.items[0].shortcut, "Ctrl+Alt+F9");
    await page.getByLabel("Desktop pointing mode").selectOption("mouse");
    assert.equal(await page.getByLabel("Precision hold key", { exact: true }).isDisabled(), true);
    await page.getByRole("button", { name: "Window", exact: true }).click();
    await page.getByRole("button", { name: "Refresh windows", exact: true }).click();
    await page.getByLabel("Current desktop windows").selectOption("window:3:0");
    await page.getByRole("button", { name: "Use this window", exact: true }).click();
    assert.match(await page.getByLabel("Selected source", { exact: true }).innerText(), /Telemetry window/);
    await page.getByRole("button", { name: "Web URL", exact: true }).click();
    const recenterKey = page.getByLabel("Recenter overlays key", { exact: true });
    const resetKey = page.getByLabel("Reset position key", { exact: true });
    await recenterKey.selectOption("F2");
    assert.equal(await resetKey.locator('option[value="F2"]').isDisabled(), true);
    await resetKey.selectOption("F8");
    assert.equal(await recenterKey.locator('option[value="F8"]').isDisabled(), true);
    assert.equal((await state()).spatialOverlays.items[0].resetShortcut, "Ctrl+Alt+F8");
    await resetKey.selectOption("");
    assert.equal(await recenterKey.locator('option[value="F8"]').isDisabled(), false);
    await recenterKey.selectOption("");
    await page.getByLabel("Source URL", { exact: true }).fill("http://localhost:8888/dashboard");
    await page.getByLabel("Source resolution", {exact:true}).selectOption("1024x576");
    assert.match(await page.getByLabel("Selected source", { exact: true }).innerText(), /localhost:8888\/dashboard/);
    assert.equal(await occlusion.getAttribute("aria-checked"), "false");
    await occlusion.click();
    assert.equal((await state()).spatialOverlays.items[0].cockpitOcclusion, true);
    await page.evaluate(item => {
      window.fixtureStatus = { items: [{ id: item.id, sourceKind: "url", sourceReference: item.url, cockpitDepthActive: false,
        source: { kind: "url", reference: item.url, name: "Timing dashboard", displaying: true } }] };
    }, (await state()).spatialOverlays.items[0]);
    await page.getByText(/showing the ordinary dashboard/i).waitFor();
    await page.evaluate(() => { window.fixtureStatus.items[0].cockpitDepthActive = true; });
    await page.getByText("Cockpit occlusion active", { exact: true }).waitFor();
    const profile = await state();
    await page.evaluate(id => {
      window.fixtureStatus.profileId = id;
      window.fixtureStatus.available = true;
      window.fixtureStatus.car = { id: "iracing-22", name: "Cup car" };
      window.fixtureStatus.items[0].placementAhead = {x:0.15,y:0.05,z:-0.9,yaw:10,pitch:-3,roll:0,widthMeters:0.8};
    }, profile.id);
    await page.getByRole("switch", {name:"Remember placement for this car"}).click();
    await page.getByRole("button", {name:"Place in front of me"}).click();
    await page.getByRole("button", { name: "Recenter overlays", exact: true }).click();
    assert.equal(await page.evaluate(() => window.recenteredGame), profile.game, "Manual recenter targets the current game's overlay anchors");
    const move = page.getByRole("button", {name:"Move overlay " + profile.spatialOverlays.items[0].name});
    await move.press("ArrowRight");
    await move.press("Control+ArrowUp");
    await move.press("Alt+Shift+ArrowUp");
    await move.press("Control+ArrowRight");
    await move.press("Alt+Shift+ArrowLeft");
    await move.press("Control+Alt+Shift+ArrowUp");
    await page.getByRole("switch", {name:"Follow steering wheel"}).click();
    await page.getByRole("button", {name:"Assigned axis", exact:true}).click();
    assert.equal((await state()).spatialOverlays.steeringInput.lockDegrees, 900);
    await page.getByRole("button", {name:"Assign steering axis", exact:true}).click();
    await page.evaluate(() => { window.fixtureSteering = .5; });
    await page.getByText(/Assigned Test wheelbase/).waitFor();
    assert.equal((await state()).spatialOverlays.steeringInput.axis, "x");
    await page.getByText("225.0° right", {exact:true}).waitFor();
    await page.getByRole("switch", {name:"Invert steering axis",exact:true}).click();
    await page.getByText("225.0° left", {exact:true}).waitFor();
    await page.getByLabel("Steering lock value",{exact:true}).fill("1080");
    await page.getByText("270.0° left", {exact:true}).waitFor();
    if (process.env.STEERING_UI_SCREENSHOT) {
      for (const name of ["src/styles.css","src/components/PlacementMap.css","src/components/dashboard-pivot.css","src/components/placement-editor.css"]) await page.addStyleTag({path:name});
      await page.setViewportSize({width:1280,height:1100});
      const cdp = await page.context().newCDPSession(page);
      const clip = await page.locator('.steering-input-settings').evaluate(element => {
        const r=element.getBoundingClientRect(); return {x:r.x+scrollX,y:r.y+scrollY,width:r.width,height:r.height,scale:1};
      });
      const shot = await cdp.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:true,clip});
      await fs.writeFile(process.env.STEERING_UI_SCREENSHOT,Buffer.from(shot.data,'base64'));await cdp.detach();
    }
    await page.evaluate(() => { window.fixtureSteering = undefined; });
    await page.getByRole("button", {name:"Game telemetry",exact:true}).click();
    assert.equal((await state()).spatialOverlays.steeringInput.source, "telemetry");
    const field = name => page.getByRole("spinbutton", {name,exact:true});
    const pivotY = field("Pivot vertical value");
    await pivotY.fill("-0,241");
    await pivotY.press("Enter");
    const placed = (await state()).spatialOverlays.items[0];
    assert.deepEqual([placed.x,placed.z,placed.widthMeters,placed.steeringFollow], [0,-1,0.6,false], "base pose is independent");
    const carPose = placed.carPlacements[0];
    assert.deepEqual([carPose.carId,carPose.anchor,carPose.x,carPose.y,carPose.z,carPose.widthMeters,carPose.steeringFollow,carPose.pivotY],
      ["iracing-22","local",0.175,0.05,-0.925,0.601,true,-0.241]);
    assert.deepEqual([carPose.yaw,carPose.pitch,carPose.roll],[11,-2.9,-0.1]);
    const pitch = field("Overlay pitch value");
    await pitch.fill("1.23");
    await pitch.press("Tab");
    assert.equal((await state()).spatialOverlays.items[0].carPlacements[0].pitch,1.2);
    await pitch.fill("204");
    await pitch.press("Enter");
    assert.equal(await pitch.inputValue(),"180");
    await pitch.fill("1..2");
    assert.equal(await pitch.getAttribute("aria-invalid"),"true");
    await pitch.press("Escape");
    assert.equal(await pitch.inputValue(),"180");
    await pitch.fill("-3,4");
    await pitch.press("Enter");
    await pitch.press("ArrowDown");
    assert.equal((await state()).spatialOverlays.items[0].carPlacements[0].pitch,-3.5);
    await field("Distance forward value").fill("0.999");
    await field("Distance forward value").press("Tab");
    assert.equal((await state()).spatialOverlays.items[0].carPlacements[0].z,-0.999);
    const opacity = field("Overlay opacity value");
    await opacity.fill("84,9");
    await opacity.press("Enter");
    assert.equal((await state()).spatialOverlays.items[0].opacity,84.9);
    await page.getByRole("slider",{name:"Overlay yaw",exact:true}).press("ArrowRight");
    assert.equal((await state()).spatialOverlays.items[0].carPlacements[0].yaw,11.1);
    await page.getByRole("button",{name:"Set wheel hub on the dashboard diagram"}).press("Shift+ArrowRight");
    assert.equal((await state()).spatialOverlays.items[0].carPlacements[0].pivotX,0.001);
    await page.route("**/fonts/*.woff2", route => route.fulfill({ path: path.join("public/fonts",path.basename(new URL(route.request().url()).pathname)), contentType:"font/woff2" }));
    for (const name of ["src/styles.css","src/components/PlacementMap.css","src/components/dashboard-pivot.css","src/components/placement-editor.css"]) await page.addStyleTag({path:name});
    await page.evaluate(() => document.fonts.ready);
    const panel = page.locator("section.panel").filter({has:page.getByRole("heading",{name:"Dashboard placement"})});
    for (const width of [1140,900,375]) {
      await page.setViewportSize({width,height:1050});
      assert.ok(await panel.evaluate(element => element.scrollWidth <= element.clientWidth + 1), `placement editor fits at ${width}px`);
      assert.ok(await panel.locator(".placement-number input").evaluateAll(inputs => inputs.every(input => input.clientWidth > 36)), "value fields stay legible");
    }
    if (process.argv.includes("--screenshot")) {
      await page.setViewportSize({width:1100,height:1100});
      await fs.mkdir("test-results",{recursive:true});
      // A hidden Electron window throttles animation frames. Capture its rendered surface directly.
      const cdp = await page.context().newCDPSession(page);
      await cdp.send("Emulation.setDeviceMetricsOverride",{width:1100,height:1200,deviceScaleFactor:1,mobile:false});
      const clip = await panel.evaluate(element => {
        document.documentElement.style.overflow = "hidden";
        const rect = element.getBoundingClientRect();
        return {x:rect.x + scrollX,y:rect.y + scrollY,width:rect.width,height:rect.height,scale:1};
      });
      const capture = await cdp.send("Page.captureScreenshot",{format:"png",captureBeyondViewport:true,clip});
      await fs.writeFile("test-results/dashboard-car-placement.png",Buffer.from(capture.data,"base64"));
    }
    await field("Horizontal position value").fill("4,123");
    await page.evaluate(() => { window.fixtureStatus.car = {id:"iracing-23",name:"Second car"}; });
    await page.getByText(/Second car · Using the default placement/).waitFor();
    assert.equal(await page.getByRole("spinbutton",{name:"Horizontal position value"}).inputValue(),"0");
    await resetKey.selectOption("F2");
    await page.getByRole("button", { name: "Window", exact: true }).click();
    assert.equal(await occlusion.count(), 0);
    assert.equal((await state()).spatialOverlays.items[0].resetShortcut, "", "Changing source releases the dashboard reset shortcut");
    assert.equal(await recenterKey.locator('option[value="F2"]').isEnabled(), true);
    await page.getByRole("button", { name: "Web URL", exact: true }).click();
    assert.equal(await occlusion.getAttribute("aria-checked"), "false");
    await page.getByRole("button",{name:"3D wheel",exact:true}).click();
    const unoccludedWheel=page.getByRole("switch",{name:"3D model without occlusion",exact:true});
    assert.equal(await occlusion.count(),0,"wheel has a single explicit depth control");
    assert.equal(await unoccludedWheel.getAttribute("aria-checked"),"true");
    await unoccludedWheel.click();
    assert.equal((await state()).spatialOverlays.items[0].cockpitOcclusion,true);
    const modelPicker=page.getByLabel("Steering wheel model");
    assert.equal(await modelPicker.inputValue(),"hyper-p1");
    await modelPicker.selectOption("hyper-sl");
    assert.equal(await unoccludedWheel.getAttribute("aria-checked"),"false","model changes preserve occlusion preference");
    await unoccludedWheel.click();
    assert.equal((await state()).spatialOverlays.items[0].cockpitOcclusion,false);
    await modelPicker.selectOption("ascher-artura");
    assert.equal(await page.getByRole("switch",{name:"Mirror SimHub LEDs",exact:true}).count(),hasWheelLights("ascher-artura")?1:0);
    assert.equal(await page.getByLabel("SimHub LED device").count(),0);
    assert.equal(await page.getByText("This model has no mapped LEDs.",{exact:true}).count(),hasWheelLights("ascher-artura")?0:1);
    assert.equal(await unoccludedWheel.getAttribute("aria-checked"),"true");
    assert.equal(await page.getByLabel("Wheel display URL (optional)").count(),hasWheelScreen("ascher-artura")?1:0);
    const driver = page.getByRole("switch", { name: "Driver (experimental)", exact: true });
    const axisNote = page.getByText(/The driver supports steering axis offsets/);
    await field("Pivot vertical value").fill("0.10");
    await field("Pivot vertical value").press("Enter");
    assert.equal(await axisNote.count(), 1);
    assert.equal(await driver.isEnabled(), false);
    await page.getByRole("button", { name: "Use centered axis", exact: true }).click();
    assert.equal((await state()).spatialOverlays.items[0].pivotY, 0);
    assert.equal(await axisNote.count(), 0);
    assert.equal(await driver.isEnabled(), true);
    assert.equal(await driver.getAttribute("aria-checked"), "false");
    assert.equal(await page.getByLabel("Glove color").count(), 0);
    await driver.click();
    const gloves = page.getByLabel("Glove color"), suit = page.getByLabel("Suit color");
    assert.equal(await gloves.inputValue(), "0");
    assert.equal(await suit.inputValue(), "0");
    assert.deepEqual(await suit.locator("option").allTextContents(), ["Purple", "Black", "Red", "Blue", "White"]);
    await gloves.selectOption("2");
    await suit.selectOption("4");
    await page.getByRole("switch", { name: "Show torso", exact: true }).click();
    await page.getByRole("switch", { name: "Show legs", exact: true }).click();
    const driverItem = (await state()).spatialOverlays.items[0];
    assert.deepEqual([driverItem.driverEnabled, driverItem.driverGloveTheme, driverItem.driverSuitTheme, driverItem.driverTorso, driverItem.driverLegs], [true, 2, 4, true, true]);
    const seatHeight = page.getByRole("spinbutton", {name:"Seat height value", exact:true});
    await seatHeight.fill("-0,123"); await seatHeight.press("Enter");
    assert.equal((await state()).spatialOverlays.items[0].driverSeatHeight, -.123);
    const legs = page.getByRole("spinbutton", {name:"Leg length value", exact:true});
    await legs.fill("85.5"); await legs.press("Enter");
    assert.equal((await state()).spatialOverlays.items[0].driverLegLength, .855);
    await page.getByRole("button", {name:"Reset seat & body fit", exact:true}).click();
    assert.equal(await seatHeight.inputValue(), "0");
    assert.equal(await legs.inputValue(), "100");
    assert.equal((await state()).spatialOverlays.items[0].driverEnabled, true, "fit reset preserves driver visibility");
    await page.getByText("Remember placement for this car", {exact:true}).click();
    await seatHeight.fill("-0.08"); await seatHeight.press("Enter");
    const fitted = (await state()).spatialOverlays.items[0];
    assert.equal(fitted.driverSeatHeight, 0);
    assert.equal(fitted.carPlacements.find(p => p.carId === "iracing-23").driverSeatHeight, -.08);
    await seatHeight.fill("-0.2");
    await page.evaluate(() => { window.fixtureStatus.car = {id:"iracing-24",name:"Third car"}; });
    await page.getByText(/Third car · Using the default placement/).waitFor();
    assert.equal(await seatHeight.inputValue(), "0", "car changes discard the previous car's uncommitted fit draft");
    await gloves.focus();
    assert.equal((await state()).spatialOverlays.items[0].driverSeatHeight, 0);
    if (process.env.DRIVER_UI_SCREENSHOT) {
      await page.setViewportSize({ width: 1100, height: 1100 });
      await page.addStyleTag({ content: "body { margin: 20px; }" });
      const cdp = await page.context().newCDPSession(page);
      await cdp.send("Emulation.setDeviceMetricsOverride", { width: 1100, height: 1200, deviceScaleFactor: 1, mobile: false });
      const clip = await page.locator(".wheel-model-settings").evaluate(element => {
        const rect = element.getBoundingClientRect();
        const x = Math.max(0, rect.x + scrollX - 12);
        const y = Math.max(0, rect.y + scrollY - 12);
        const width = Math.max(rect.width, element.scrollWidth) + 32;
        return { x, y, width, height: rect.height + 24, scale: 1 };
      });
      const capture = await cdp.send("Page.captureScreenshot", { format: "png", captureBeyondViewport: true, clip });
      await fs.writeFile(process.env.DRIVER_UI_SCREENSHOT, Buffer.from(capture.data, "base64"));
      await cdp.detach();
    }
    for (const model of wheelModels) {
      await modelPicker.selectOption(model.id);
      assert.equal(await driver.getAttribute("aria-checked"), "true", `${model.name} supports the driver`);
      assert.equal(await gloves.inputValue(), "2");
      assert.equal(await suit.inputValue(), "4");
    }
    await page.getByRole("button",{name:"Import OBJ or binary STL"}).click();
    assert.equal(await modelPicker.inputValue(),"f".repeat(64));
    assert.equal(await driver.count(), 0);
    assert.equal((await state()).spatialOverlays.items[0].driverEnabled, false);
    assert.deepEqual(await page.getByLabel("SimHub LED device").locator('option').evaluateAll(options=>options.map(option=>Number(option.value))),
      [0,...wheelModels.filter(w=>hasWheelLights(w.id)).map(w=>wheelLightDevice(w.id))],
      "the physical LED picker offers only mapped devices with their protocol IDs");
    await page.getByLabel("Wheel display URL (optional)").fill("http://localhost:8888/dash");
    await page.getByLabel("SimHub LED device").selectOption("5");
    await page.getByRole("spinbutton",{name:"Scene lighting on wheel value"}).fill("65");
    const wheel=JSON.parse(await page.getByLabel("Fixture profile").textContent()).spatialOverlays.items[0];
    assert.equal(wheel.wheelName,"My rim");assert.equal(wheel.wheelDevice,5);assert.equal(wheel.wheelLights,true);
    assert.equal(wheel.wheelAmbientLight,65);
    await modelPicker.selectOption("hyper-sl");
    assert.equal(await page.getByRole("switch",{name:"Show in stream",exact:true}).count(),0);
    assert.equal((await state()).spatialOverlays.items[0].url,"http://localhost:8888/dash");
    await modelPicker.selectOption("f".repeat(64));
    assert.equal(await page.getByLabel("Wheel display URL (optional)").inputValue(),"http://localhost:8888/dash");
    await page.getByRole("button",{name:"Web URL",exact:true}).click();
    assert.equal(await modelPicker.count(),0);
    console.log("Source identity, dashboard masking, per-car steering placement, and desktop input verified.");
  } finally { if (app) await app.close(); await new Promise(resolve => server.close(resolve)); await fs.rm(dir, { recursive: true, force: true }); }
})().catch(error => { console.error(error); process.exitCode = 1; });
