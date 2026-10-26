"use strict";
const fs = require("node:fs/promises");
const nativeFs = require("node:fs");
const { createReadStream, constants } = require("node:fs");
const path = require("node:path");
const { createHash, randomUUID } = require("node:crypto");
const { pathToFileURL } = require("node:url");
const MAX = 2 * 1024 * 1024;
const digest = (value) => createHash("sha256").update(value).digest("hex");
async function atomicWrite(file, contents, beforeReplace) {
  await fs.mkdir(path.dirname(file), { recursive: true });
  const temp = `${file}.${randomUUID()}.tmp`;
  try {
    const handle = await fs.open(temp, "wx");
    try {
      await handle.writeFile(contents, "utf8");
      await handle.sync();
    } finally {
      await handle.close();
    }
    // Windows readers (including the native config watcher) can briefly omit
    // FILE_SHARE_DELETE. Retry the atomic replacement; never unlink the target.
    for (let attempt=0;;attempt++) {
      // A guarded publication must not yield to editor IPC after checking state.
      // Recheck on every retry; all preparation and retry waits remain async.
      if(beforeReplace)beforeReplace();
      try {
        if(beforeReplace)nativeFs.renameSync(temp,file);
        else await fs.rename(temp,file);
        break;
      }
      catch(error) {
        if(process.platform!=='win32'||!['EPERM','EACCES','EBUSY'].includes(error.code)||attempt>=6)throw error;
        await new Promise(resolve=>setTimeout(resolve,25*2**attempt));
      }
    }
  } finally {
    await fs.unlink(temp).catch(() => {});
  }
}
async function createStore(directory) {
  const { createDocument, validateDocument } = await import(
    pathToFileURL(path.join(__dirname, "../src/model.mjs")).href
  );
  const root = path.resolve(directory),
    file = path.join(root, "profiles.json"),
    backupDir = path.join(root, "backups");
  await fs.mkdir(root, { recursive: true });
  let initial = createDocument();
  async function raw() {
    try {
      const size = (await fs.stat(file)).size;
      if (size > MAX) {
        const hash = createHash("sha256");
        for await (const chunk of createReadStream(file)) hash.update(chunk);
        return {
          contents: null,
          revision: hash.digest("hex"),
          oversized: true,
          exists: true,
        };
      }
      const bytes = await fs.readFile(file);
      return {
        contents: bytes.toString("utf8"),
        revision: digest(bytes),
        exists: true,
      };
    } catch (e) {
      if (e.code === "ENOENT")
        return { contents: null, revision: digest(""), exists: false };
      throw e;
    }
  }
  async function backups() {
    await fs.mkdir(backupDir, { recursive: true });
    const names = (await fs.readdir(backupDir)).filter((n) =>
      /^\d{13}-[0-9a-f-]{36}\.json$/.test(n),
    );
    return names
      .sort()
      .reverse()
      .slice(0, 100)
      .map((id) => ({
        id,
        name: "Saved configuration",
        createdAt: new Date(Number(id.slice(0, 13))).toISOString(),
      }));
  }
  async function load() {
    const current = await raw();
    let document, recoveryError;
    if (!current.exists) document = structuredClone(initial);
    else
      try {
        if (current.oversized) throw new Error("Profile data exceeds 2 MiB.");
        document = JSON.parse(current.contents);
        validateDocument(document);
      } catch (e) {
        document = structuredClone(initial);
        recoveryError = `Stored profiles could not be read: ${e.message} Restore a backup; the original file has been preserved.`;
      }
    return {
      document,
      revision: current.revision,
      backups: await backups(),
      ...(recoveryError ? { recoveryError } : {}),
    };
  }
  async function transact(doc, revision, recover = false) {
    validateDocument(doc);
    const serialized = JSON.stringify(doc, null, 2) + "\n";
    if (Buffer.byteLength(serialized, "utf8") > MAX)
      throw new Error(
        "Profile data exceeds 2 MiB. Remove unused profiles or extra imported data.",
      );
    if (typeof revision !== "string")
      throw new Error("Missing revision. Reload profiles.");
    const lockPath = path.join(root, "profiles.lock");
    let lock;
    try {
      lock = await fs.open(lockPath, "wx");
    } catch (e) {
      if (e.code !== "EEXIST") throw e;
      let abandoned = false;
      try {
        const owner = JSON.parse(await fs.readFile(lockPath, "utf8"));
        if (Number.isSafeInteger(owner.pid) && owner.pid > 0) {
          try {
            process.kill(owner.pid, 0);
          } catch (err) {
            abandoned = err.code === "ESRCH";
          }
        }
      } catch {}
      if (!abandoned)
        throw new Error("Profiles are busy in another operation. Try again.");
      await fs.unlink(lockPath);
      lock = await fs.open(lockPath, "wx");
    }
    await lock.writeFile(JSON.stringify({ pid: process.pid }), "utf8");
    try {
      const current = await raw();
      if (current.revision !== revision)
        throw new Error(
          "Profiles changed in another window. Reload before saving.",
        );
      if (current.exists && !recover) {
        try {
          if (current.oversized) throw new Error("Oversize");
          validateDocument(JSON.parse(current.contents));
        } catch {
          throw new Error(
            "Current data is damaged. Restore a backup before saving.",
          );
        }
      }
      if (current.exists) {
        await fs.mkdir(backupDir, { recursive: true });
        await fs.copyFile(
          file,
          path.join(backupDir, `${Date.now()}-${randomUUID()}.json`),
          constants.COPYFILE_EXCL,
        );
      }
      await atomicWrite(file, serialized);
      return await load();
    } finally {
      await lock.close();
      await fs.unlink(path.join(root, "profiles.lock")).catch(() => {});
    }
  }
  async function restore(id, revision) {
    if (typeof id !== "string" || !/^\d{13}-[0-9a-f-]{36}\.json$/.test(id))
      throw new Error("Invalid backup ID.");
    const target = path.join(backupDir, id);
    if ((await fs.stat(target)).size > MAX)
      throw new Error("Backup exceeds 2 MiB.");
    const document = JSON.parse(await fs.readFile(target, "utf8"));
    validateDocument(document);
    return transact(document, revision, true);
  }
  return {
    load,
    save: (doc, rev) => transact(doc, rev),
    restore,
    directory: root,
  };
}
module.exports = { createStore, atomicWrite };
