import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { createRequire } from "node:module";
const require = createRequire(import.meta.url);
const { createNativeSettings } = require("../desktop/native-settings.cjs");
const fixture =
  "; untouched café\r\n[Graphics Options]\r\nVRMode=2\r\nOther=99\r\n[OpenXR]\r\n FoveatedOuterPctRes = 35  ; peripheral\r\nFoveatedInsetWidthPct=40\r\n[Other]\r\nResolutionScalePct=321\r\n";
async function setup(ensureStopped = async () => {}) {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), "opengrid-ini-"));
  const file = path.join(root, "rendererDX11OpenXR.ini"),
    backupDir = path.join(root, "backups");
  await fs.writeFile(file, Buffer.from(fixture, "latin1"));
  return {
    root,
    file,
    backupDir,
    api: createNativeSettings({ defaultPath: file, backupDir, ensureStopped }),
  };
}
test("review is read-only; apply preserves unrelated bytes and creates an exact backup", async () => {
  const s = await setup();
  try {
    const before = await fs.readFile(s.file),
      state = await s.api.inspect();
    assert.deepEqual(state.values, {
      outerResolution: 35,
      focusWidth: 40,
      resolutionScale: null,
    });
    const preview = await s.api.preview(
      { outerResolution: 30, focusWidth: 45, resolutionScale: 110 },
      state.revision,
    );
    assert.equal(preview.changes.length, 3);
    assert.deepEqual(await fs.readFile(s.file), before);
    const after = await s.api.apply(preview.token);
    assert.deepEqual(after.values, {
      outerResolution: 30,
      focusWidth: 45,
      resolutionScale: 110,
    });
    assert.equal(after.backups.length, 1);
    assert.deepEqual(
      await fs.readFile(path.join(s.backupDir, after.backups[0].id)),
      before,
    );
    const contents = (await fs.readFile(s.file)).toString("latin1");
    assert.match(contents, / FoveatedOuterPctRes = 30  ; peripheral\r\n/);
    assert.match(contents, /\[Other\]\r\nResolutionScalePct=321/);
    assert.match(contents, /untouched café/);
    const restored = await s.api.restore(after.backups[0].id, after.revision);
    assert.deepEqual(await fs.readFile(s.file), before);
    assert.equal(restored.backups.length, 2);
  } finally {
    await fs.rm(s.root, { recursive: true, force: true });
  }
});
test("stale previews and a running sim cannot change the file or create a backup", async () => {
  let running = false;
  const s = await setup(async () => {
    if (running) throw new Error("Close iRacing first.");
  });
  try {
    const state = await s.api.inspect(),
      values = { outerResolution: 30, focusWidth: 40, resolutionScale: 100 };
    let review = await s.api.preview(values, state.revision);
    await fs.appendFile(s.file, "; external edit\r\n");
    await assert.rejects(s.api.apply(review.token), /changed/i);
    const updated = await s.api.inspect();
    review = await s.api.preview(values, updated.revision);
    running = true;
    await assert.rejects(s.api.apply(review.token), /Close iRacing/);
    assert.equal((await s.api.inspect()).backups.length, 0);
  } finally {
    await fs.rm(s.root, { recursive: true, force: true });
  }
});
test("ambiguous sections, duplicate keys and out-of-range values never modify the INI", async () => {
  const s = await setup();
  try {
    const state = await s.api.inspect();
    await assert.rejects(
      s.api.preview(
        { outerResolution: 1, focusWidth: 40, resolutionScale: 100 },
        state.revision,
      ),
      /range|outerResolution/i,
    );
    await assert.rejects(
      s.api.preview(
        {
          outerResolution: 35,
          focusWidth: 40,
          resolutionScale: 100,
          VRMode: 2,
        },
        state.revision,
      ),
      /unknown|unsupported/i,
    );
    await fs.writeFile(
      s.file,
      "[OpenXR]\nFoveatedOuterPctRes=35\nFoveatedOuterPctRes=40\n",
    );
    await assert.rejects(s.api.inspect(), /duplicate/i);
    await fs.writeFile(s.file, "[OpenXR]\n[OpenXR]\n");
    await assert.rejects(s.api.inspect(), /duplicate/i);
    await assert.rejects(s.api.restore("../other.ini", "x"), /backup/i);
  } finally {
    await fs.rm(s.root, { recursive: true, force: true });
  }
});
test("UTF-16 game files preserve their encoding and unavailable configs are not created", async () => {
  const s = await setup();
  try {
    const original = Buffer.concat([
      Buffer.from([255, 254]),
      Buffer.from("[OpenXR]\r\nFoveatedOuterPctRes=35\r\n; äöü\r\n", "utf16le"),
    ]);
    await fs.writeFile(s.file, original);
    const state = await s.api.inspect(),
      review = await s.api.preview(
        { outerResolution: 30, focusWidth: 40, resolutionScale: 100 },
        state.revision,
      );
    await s.api.apply(review.token);
    const result = await fs.readFile(s.file);
    assert.deepEqual([...result.subarray(0, 2)], [255, 254]);
    assert.match(result.subarray(2).toString("utf16le"), /; äöü\r\n/);
    await fs.unlink(s.file);
    assert.equal((await s.api.inspect()).available, false);
    await assert.rejects(
      s.api.preview(
        { outerResolution: 30, focusWidth: 40, resolutionScale: 100 },
        "missing",
      ),
      /OpenXR|missing/i,
    );
    await assert.rejects(fs.access(s.file));
  } finally {
    await fs.rm(s.root, { recursive: true, force: true });
  }
});
