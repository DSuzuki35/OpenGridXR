'use strict';
const fs = require('node:fs/promises');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const { atomicWrite } = require('./store.cjs');

async function createModuleSettings(directory) {
  const { moduleDefaults, validateModuleSettings } = await import(pathToFileURL(path.join(__dirname, '../src/modules.mjs')).href);
  const file = path.join(directory, 'module-settings.json');
  let settings = { version: 1, enabled: moduleDefaults() }, original = null, queue = Promise.resolve();
  try {
    if ((await fs.stat(file)).size > 65536) throw new Error('Module settings exceed 64 KiB.');
    original = await fs.readFile(file, 'utf8');
    settings = validateModuleSettings(JSON.parse(original));
  } catch (error) {
    if (error.code !== 'ENOENT') settings = {
      version: 1,
      enabled: Object.fromEntries(Object.keys(moduleDefaults()).map(id => [id, false])),
      error: `Could not read module settings. Optional modules are off; the original file is preserved. ${error.message}`,
    };
  }
  return {
    get: () => structuredClone(settings),
    save(value, beforeReplace) {
      const next = validateModuleSettings(value);
      const operation = queue.then(async () => {
        beforeReplace?.();
        // Copy the original bytes, including an invalid file, before replacing it.
        if (original !== null) await atomicWrite(`${file}.bak`, original);
        else {
          try { await fs.copyFile(file, `${file}.bak`); } catch (error) { if (error.code !== 'ENOENT') throw error; }
        }
        const contents = JSON.stringify(next, null, 2) + '\n';
        await atomicWrite(file, contents, beforeReplace);
        settings = next;
        original = contents;
        return structuredClone(settings);
      });
      queue = operation.catch(() => {});
      return operation;
    },
  };
}
module.exports = { createModuleSettings };
