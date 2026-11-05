"use strict";

// Run after package:installer: node tests/update-download.cjs
// Uses the real NSIS updater and download/checksum pipeline. Only Electron's
// request transport and AppAdapter are replaced so this runs without a GUI.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const fsp = require("node:fs/promises");
const path = require("node:path");
const http = require("node:http");
const { once } = require("node:events");
const { createHash } = require("node:crypto");
const { Transform } = require("node:stream");
const yaml = require("js-yaml");
const { NsisUpdater } = require("electron-updater/out/NsisUpdater");
const { ElectronHttpExecutor } = require("electron-updater/out/electronHttpExecutor");
const { NodeHttpExecutor } = require("builder-util/out/nodeHttpExecutor");
const { createUpdateController } = require("../desktop/update-controller.cjs");

async function sha512(file) {
  const hash = createHash("sha512");
  for await (const chunk of fs.createReadStream(file)) hash.update(chunk);
  return hash.digest("base64");
}

async function main() {
  const root = path.resolve(__dirname, "..");
  const version = require("../package.json").version;
  const release = path.join(root, "release", version);
  const metadataText = await fsp.readFile(path.join(release, "latest.yml"), "utf8");
  const metadata = yaml.load(metadataText);
  assert.equal(metadata.version, version);
  const installer = metadata.files.find(file => file.url.endsWith(".exe"));
  assert.ok(installer, "Built release must contain an NSIS installer");
  assert.equal(path.basename(installer.url), installer.url, "Installer must be a local release filename");
  const tempParent = path.join(root, ".cache", "test-tmp");
  await fsp.mkdir(tempParent, { recursive: true });
  const temp = await fsp.mkdtemp(path.join(tempParent, "update-download-"));
  let server;
  const controllers = [];
  try {
    // Snapshot the artifact so another packaging run cannot change the bytes
    // halfway through these checks. No installer is ever executed.
    const artifact = path.join(temp, installer.url);
    await fsp.copyFile(path.join(release, installer.url), artifact);
    assert.equal(await sha512(artifact), installer.sha512, "Built installer must match latest.yml");
    assert.equal((await fsp.stat(artifact)).size, installer.size);
    const requests = [];
    server = http.createServer((request, response) => {
      const url = new URL(request.url, "http://127.0.0.1");
      requests.push(url.pathname);
      if (url.pathname === "/valid/latest.yml" || url.pathname === "/corrupt/latest.yml") {
        response.writeHead(200, { "Content-Type": "text/yaml" });
        response.end(metadataText);
        return;
      }
      const corrupt = url.pathname === `/corrupt/${installer.url}`;
      if (url.pathname !== `/valid/${installer.url}` && !corrupt) {
        response.writeHead(404); response.end(); return;
      }
      response.writeHead(200, { "Content-Type": "application/octet-stream", "Content-Length": installer.size });
      const stream = fs.createReadStream(artifact);
      stream.on("error", error => response.destroy(error));
      response.on("close", () => stream.destroy());
      if (!corrupt) stream.pipe(response);
      else {
        let first = true;
        stream.pipe(new Transform({
          transform(chunk, _encoding, callback) {
            if (first) { chunk[0] ^= 1; first = false; }
            callback(null, chunk);
          },
        })).pipe(response);
      }
    });
    server.listen(0, "127.0.0.1");
    await once(server, "listening");
    const baseUrl = `http://127.0.0.1:${server.address().port}`;

    async function fixture(name) {
      const directory = path.join(temp, name);
      const userDataPath = path.join(directory, "user-data");
      await fsp.mkdir(userDataPath, { recursive: true });
      const config = path.join(directory, "app-update.yml");
      await fsp.writeFile(config, "updaterCacheDirName: update-cache\n");
      const effects = { quitHooks: 0, executions: 0, quits: 0, relaunches: 0, downloaded: 0 };
      const app = {
        version: "0.2.1", name: "OpenGrid XR Update Test", isPackaged: true,
        appUpdateConfigPath: config, userDataPath, baseCachePath: directory,
        whenReady: async () => {},
        onQuit: () => { effects.quitHooks++; },
        quit: () => { effects.quits++; },
        relaunch: () => { effects.relaunches++; },
      };
      const updater = new NsisUpdater(undefined, app);
      updater.logger = null;
      updater.disableDifferentialDownload = true;
      const executor = new ElectronHttpExecutor();
      const nodeExecutor = new NodeHttpExecutor();
      executor.createRequest = (options, callback) => {
        assert.equal(options.hostname, "127.0.0.1", "Test must never access an external update server");
        assert.equal(options.protocol, "http:");
        assert.ok(!process.env.http_proxy, "Unset http_proxy for this loopback-only test");
        return nodeExecutor.createRequest(options, callback);
      };
      updater.httpExecutor = executor;
      // A regression must fail the assertions, never launch an installer.
      updater.doInstall = () => { effects.executions++; return false; };
      updater.on("update-downloaded", () => { effects.downloaded++; });
      const controller = createUpdateController({
        updater, enabled: true, currentVersion: app.version,
        readPreferences: async () => ({}), writePreferences: async () => {},
        ensureCanInstall: async () => false,
      });
      controllers.push(controller);
      // Product configuration remains the fixed GitHub feed. Local override is test-only.
      updater.setFeedURL({ provider: "generic", url: `${baseUrl}/${name}/` });
      return { updater, controller, effects, directory };
    }

    const valid = await fixture("valid");
    assert.equal(valid.updater.autoDownload, false);
    assert.equal(valid.updater.autoInstallOnAppQuit, false);
    const checked = await valid.controller.check();
    assert.equal(checked.phase, "available", checked.message);
    assert.equal(valid.controller.status().version, version);
    await valid.controller.decide("later");
    await valid.controller.decide("skip");
    assert.equal(requests.filter(url => url.endsWith(".exe")).length, 0, "Refusing an update must not request installer bytes");
    assert.equal(valid.updater.installerPath, null);
    assert.equal((await valid.controller.check(true)).prompt, true);
    assert.equal((await valid.controller.decide("download")).phase, "downloaded");
    assert.equal(valid.controller.status().percent, 100);
    assert.equal(valid.effects.downloaded, 1);
    assert.equal(await sha512(valid.updater.installerPath), installer.sha512);
    assert.ok(valid.updater.installerPath.startsWith(valid.directory + path.sep));
    await valid.controller.install(); // Explicit installation still refused by readiness/consent gate.
    assert.equal(valid.controller.status().phase, "downloaded");
    assert.deepEqual(valid.effects, { quitHooks: 0, executions: 0, quits: 0, relaunches: 0, downloaded: 1 });

    const corrupt = await fixture("corrupt");
    assert.equal((await corrupt.controller.check()).phase, "available");
    assert.equal((await corrupt.controller.decide("download")).phase, "error");
    assert.match(corrupt.controller.status().message, /sha512 checksum mismatch/i);
    assert.equal(corrupt.updater.installerPath, null);
    assert.deepEqual(corrupt.effects, { quitHooks: 0, executions: 0, quits: 0, relaunches: 0, downloaded: 0 });
    assert.equal(requests.filter(url => url.endsWith(".exe")).length, 2);
    console.log(`PASS: ${version} real NSIS metadata/download, refusal, SHA-512 rejection, no install-on-quit or execution`);
  } finally {
    for (const controller of controllers) controller.close();
    if (server) await new Promise(resolve => server.close(resolve));
    // Only this invocation's unique child of the workspace test cache is removed.
    assert.equal(path.dirname(temp), tempParent);
    await fsp.rm(temp, { recursive: true, force: true });
  }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
