// Rasterize our original SVG mark and wrap its PNG sizes in a Windows icon.
// Run with: npx electron scripts/build-icon.cjs
const { app, BrowserWindow } = require("electron");
const fs = require("node:fs/promises");
const path = require("node:path");
app
  .whenReady()
  .then(async () => {
    const svg = await fs.readFile(
      path.join(__dirname, "../public/favicon.svg"),
      "utf8",
    );
    const window = new BrowserWindow({
      width: 256,
      height: 256,
      useContentSize: true,
      show: false,
      frame: false,
      transparent: true,
      webPreferences: {
        offscreen: true,
        sandbox: true,
        contextIsolation: true,
      },
    });
    const rendered = new Promise((resolve, reject) => {
      const timeout = setTimeout(
        () => reject(new Error("Icon paint timed out.")),
        15000,
      );
      window.webContents.once("paint", (_event, _rect, image) => {
        clearTimeout(timeout);
        resolve(image);
      });
    });
    await window.loadURL(
      `data:text/html;charset=utf-8,${encodeURIComponent(`<html style="background:transparent"><body style="margin:0;width:256px;height:256px">${svg}</body></html>`)}`,
    );
    const image = await rendered;
    if (image.isEmpty())
      throw new Error("Icon rendering returned an empty image.");
    const sizes = [256, 64, 48, 32, 16];
    const pngs = sizes.map((size) =>
      image.resize({ width: size, height: size, quality: "best" }).toPNG(),
    );
    const header = Buffer.alloc(6 + 16 * sizes.length);
    header.writeUInt16LE(1, 2);
    header.writeUInt16LE(sizes.length, 4);
    let offset = header.length;
    for (let i = 0; i < sizes.length; i++) {
      const entry = 6 + i * 16;
      header[entry] = header[entry + 1] = sizes[i] === 256 ? 0 : sizes[i];
      header.writeUInt16LE(1, entry + 4);
      header.writeUInt16LE(32, entry + 6);
      header.writeUInt32LE(pngs[i].length, entry + 8);
      header.writeUInt32LE(offset, entry + 12);
      offset += pngs[i].length;
    }
    await fs.writeFile(
      path.join(__dirname, "../public/app.ico"),
      Buffer.concat([header, ...pngs]),
    );
    window.destroy();
    console.log("Built public/app.ico from the original SVG mark.");
    app.quit();
  })
  .catch((error) => {
    console.error(error);
    app.exit(1);
  });
