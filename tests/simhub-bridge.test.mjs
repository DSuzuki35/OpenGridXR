import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { createSimHubBridge } = require('../desktop/simhub-bridge.cjs');
const filename = 'oVRlay.SimHubLeds.dll';
function binary(value) {
  const bytes = Buffer.alloc(128, value);
  bytes.write('MZ'); bytes.writeUInt32LE(64, 60); bytes.write('PE\0\0', 64);
  return bytes;
}
async function fixture(t, overrides = {}) {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'opengrid-simhub-'));
  t.after(() => fs.rm(root, { recursive:true, force:true }));
  const directory = path.join(root, 'programs', 'SimHub');
  const sourceFile = path.join(root, filename);
  const dataDirectory = path.join(root, 'data');
  await fs.mkdir(directory, { recursive:true });
  await fs.writeFile(path.join(directory, 'SimHubWPF.exe'), 'fixture');
  await fs.writeFile(path.join(directory, 'SimHub.Plugins.dll'), 'fixture');
  await fs.writeFile(sourceFile, binary(1));
  const options = { sourceFile, dataDirectory, platform:'win32',
    env:{ ProgramFiles:path.join(root, 'programs'), SystemRoot:'C:\\Windows' },
    execFile:async (file) => {
      if (path.basename(file).toLowerCase() === 'tasklist.exe') return { stdout:'"System","4","Services","0","100 K"\r\n' };
      throw Object.assign(new Error('Registry value absent'), { code:1 });
    }, chooseDirectory:async () => null, ...overrides };
  return { root, directory, sourceFile, dataDirectory, options, bridge:createSimHubBridge(options), target:path.join(directory, filename) };
}

test('discovers a valid default install and atomically installs the bundled bytes', async t => {
  const f = await fixture(t);
  const before = await f.bridge.status();
  assert.equal(before.directory, f.directory); assert.equal(before.available, true);
  assert.equal(before.installed, false); assert.equal(before.running, false);
  const result = await f.bridge.install();
  assert.equal(result.current, true); assert.equal(result.error, null);
  assert.deepEqual(await fs.readFile(f.target), binary(1));
  assert.deepEqual((await fs.readdir(f.directory)).sort(), ['SimHub.Plugins.dll','SimHubWPF.exe',filename].sort());
});

test('updates preserve the previous bytes in a non-loadable backup and repeated installs do not write', async t => {
  const f = await fixture(t);
  await fs.writeFile(f.target, binary(2));
  assert.equal((await f.bridge.status()).current, false);
  await f.bridge.install();
  const names = await fs.readdir(f.directory);
  const backups = names.filter(name => name.includes('backup'));
  assert.equal(backups.length, 1); assert.equal(backups[0].endsWith('.dll'), false);
  assert.deepEqual(await fs.readFile(path.join(f.directory, backups[0])), binary(2));
  const timestamp = (await fs.stat(f.target)).mtimeMs;
  await Promise.all([f.bridge.install(), f.bridge.install()]);
  assert.equal((await fs.stat(f.target)).mtimeMs, timestamp);
  assert.deepEqual(await fs.readdir(f.directory), names);
});

for (const executable of ['SimHubWPF.exe','SimHub.exe']) test(`blocks writes while ${executable} runs`, async t => {
  const f = await fixture(t, { execFile:async file => {
    if (path.basename(file).toLowerCase() === 'tasklist.exe') return { stdout:`"${executable}","23","Console","1","100 K"` };
    throw new Error('absent');
  } });
  assert.equal((await f.bridge.status()).running, true);
  await assert.rejects(f.bridge.install(), /close SimHub/i);
  await assert.rejects(fs.stat(f.target), { code:'ENOENT' });
});

for (const output of [null, 'unrecognized tasklist output']) test(`process inspection fails closed: ${output}`, async t => {
  const f = await fixture(t, { execFile:async () => { if (output === null) throw new Error('unavailable'); return { stdout:output }; } });
  const state = await f.bridge.status();
  assert.equal(state.running, null); assert.match(state.error, /check.*SimHub|inspect/i);
  await assert.rejects(f.bridge.install(), /check.*SimHub|inspect/i);
  await assert.rejects(fs.stat(f.target), { code:'ENOENT' });
});

test('cancel preserves selection; valid custom selection persists independently of SimHub settings', async t => {
  const f = await fixture(t);
  const before = await f.bridge.status();
  assert.deepEqual(await f.bridge.choose(), before);
  const custom = path.join(f.root, 'custom');
  await fs.rename(f.directory, custom);
  const chooser = createSimHubBridge({ ...f.options, chooseDirectory:async () => custom });
  assert.equal((await chooser.choose()).directory, custom);
  assert.equal((await createSimHubBridge(f.options).status()).directory, custom);
  assert.deepEqual((await fs.readdir(custom)).sort(), ['SimHub.Plugins.dll','SimHubWPF.exe']);
});

test('invalid target is rejected without persisting the selected folder', async t => {
  const f = await fixture(t);
  const invalid = path.join(f.root, 'unrelated'); await fs.mkdir(invalid);
  const chooser = createSimHubBridge({ ...f.options, chooseDirectory:async () => invalid });
  await assert.rejects(chooser.choose(), /SimHub.*folder|valid.*SimHub/i);
  assert.equal((await chooser.status()).directory, f.directory);
  assert.deepEqual(await fs.readdir(invalid), []);
});

test('rejects missing or invalid bundled source without altering the installed plugin', async t => {
  const f = await fixture(t); await fs.writeFile(f.target, binary(2));
  await fs.writeFile(f.sourceFile, 'not a DLL');
  assert.equal((await f.bridge.status()).available, false);
  await assert.rejects(f.bridge.install(), /bundled.*bridge/i);
  assert.deepEqual(await fs.readFile(f.target), binary(2));
});

test('permission failure leaves old plugin intact and offers manual copy', async t => {
  const f = await fixture(t); await fs.writeFile(f.target, binary(2));
  const bridge = createSimHubBridge({ ...f.options, fs:{ ...fs, copyFile:async () => { throw Object.assign(new Error('denied'), { code:'EACCES' }); } } });
  await assert.rejects(bridge.install(), /permission.*manual|manual.*copy/i);
  assert.deepEqual(await fs.readFile(f.target), binary(2));
  assert.equal((await fs.readdir(f.directory)).some(name => name.includes('.tmp')), false);
});

test('rejects redirected directories even when SimHub markers exist behind the junction', async t => {
  const f = await fixture(t); const link = path.join(f.root, 'redirect');
  try { await fs.symlink(f.directory, link, 'junction'); } catch (error) {
    if (['EPERM','EACCES','ENOTSUP'].includes(error.code)) { t.skip('Junctions unavailable'); return; } throw error;
  }
  const bridge = createSimHubBridge({ ...f.options, chooseDirectory:async () => link });
  await assert.rejects(bridge.choose(), /redirect|symbolic|junction/i);
  await assert.rejects(fs.stat(f.target), { code:'ENOENT' });
});

test('rejects a hard-linked target without modifying the linked file', async t => {
  const f = await fixture(t); const other = path.join(f.root, 'other.dll');
  await fs.writeFile(other, binary(2)); await fs.link(other, f.target);
  await assert.rejects(f.bridge.install(), /link|redirect/i);
  assert.deepEqual(await fs.readFile(other), binary(2));
});

test('accepts Windows short-name expansion when canonical path identifies the same file', async t => {
  const f = await fixture(t);
  const expanded = `${f.root}-long-name`;
  const aliasFs = {
    ...fs,
    realpath: async file => (await fs.realpath(file)).replace(f.root, expanded),
    lstat: (file, options) => fs.lstat(file.replace(expanded, f.root), options),
  };
  const bridge = createSimHubBridge({...f.options, fs:aliasFs});
  assert.equal((await bridge.status()).available, true);
  assert.equal((await bridge.install()).current, true);
  assert.deepEqual(await fs.readFile(f.target), binary(1));
});

test('rejects canonical-path changes that identify a different file', async t => {
  const f = await fixture(t);
  const other = path.join(f.root, 'other-source.dll');
  await fs.writeFile(other, binary(1));
  const bridge = createSimHubBridge({...f.options, fs:{...fs,
    realpath: file => file === f.sourceFile ? Promise.resolve(other) : fs.realpath(file),
  }});
  await assert.rejects(bridge.install(), /bundled.*bridge/i);
  await assert.rejects(fs.stat(f.target), {code:'ENOENT'});
});

test('registry install location discovers custom installation', async t => {
  const f = await fixture(t); const custom = path.join(f.root, 'registry custom');
  await fs.rename(f.directory, custom);
  const bridge = createSimHubBridge({ ...f.options, execFile:async (file, args) => {
    if (path.basename(file).toLowerCase() === 'reg.exe') return { stdout:`HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\SimHub_is1\r\n    InstallLocation    REG_SZ    ${custom}\r\n` };
    return f.options.execFile(file, args);
  } });
  assert.equal((await bridge.status()).directory, custom);
});

test('unsupported platform performs no discovery, process inspection, or writes', async t => {
  const f = await fixture(t); let calls = 0;
  const bridge = createSimHubBridge({ ...f.options, platform:'unsupported', execFile:async () => { calls++; throw new Error('unexpected'); } });
  assert.equal((await bridge.status()).available, false);
  await assert.rejects(bridge.install(), /Windows/i); assert.equal(calls, 0);
});

test('SimHub starting during staging prevents replacement and cleans the staged copy', async t => {
  const f = await fixture(t); await fs.writeFile(f.target, binary(2));
  let inspections = 0;
  const bridge = createSimHubBridge({ ...f.options, execFile:async (file, args) => {
    if (path.basename(file).toLowerCase() !== 'tasklist.exe') return f.options.execFile(file, args);
    return { stdout:++inspections === 1 ? '"System","4","Services","0","100 K"' : '"SimHubWPF.exe","23","Console","1","100 K"' };
  } });
  await assert.rejects(bridge.install(), /close SimHub/i);
  assert.deepEqual(await fs.readFile(f.target), binary(2));
  assert.equal((await fs.readdir(f.directory)).some(name => name.endsWith('.tmp')), false);
});

test('a corrupt staged copy never replaces the previous plugin', async t => {
  const f = await fixture(t); await fs.writeFile(f.target, binary(2));
  const bridge = createSimHubBridge({ ...f.options, fs:{ ...fs, copyFile:async (source, destination, mode) => {
    await fs.copyFile(source, destination, mode);
    if (destination.endsWith('.tmp')) await fs.writeFile(destination, binary(3));
  } } });
  await assert.rejects(bridge.install(), /verified/i);
  assert.deepEqual(await fs.readFile(f.target), binary(2));
  assert.deepEqual((await fs.readdir(f.directory)).sort(), ['SimHub.Plugins.dll','SimHubWPF.exe',filename].sort());
});

test('failed atomic replacement preserves original bytes and a verified recovery backup', async t => {
  const f = await fixture(t); await fs.writeFile(f.target, binary(2));
  const bridge = createSimHubBridge({ ...f.options, fs:{ ...fs, rename:async () => { throw Object.assign(new Error('locked'), { code:'EBUSY' }); } } });
  await assert.rejects(bridge.install(), /close SimHub.*manual/i);
  assert.deepEqual(await fs.readFile(f.target), binary(2));
  const names = await fs.readdir(f.directory);
  assert.equal(names.some(name => name.endsWith('.tmp')), false);
  const backup = names.find(name => name.includes('backup'));
  assert.deepEqual(await fs.readFile(path.join(f.directory, backup)), binary(2));
});

test('simultaneous first installs produce one plugin and no redundant backups', async t => {
  const f = await fixture(t);
  const results = await Promise.all([f.bridge.install(), f.bridge.install(), f.bridge.status()]);
  assert.equal(results.every(result => result.current), true);
  assert.deepEqual((await fs.readdir(f.directory)).sort(), ['SimHub.Plugins.dll','SimHubWPF.exe',filename].sort());
});

test('a removed saved selection does not silently install into another discovered SimHub', async t => {
  const f = await fixture(t); const custom = path.join(f.root, 'custom');
  await fs.mkdir(custom);
  await fs.writeFile(path.join(custom, 'SimHub.exe'), 'fixture');
  await fs.writeFile(path.join(custom, 'SimHub.Plugins.dll'), 'fixture');
  const bridge = createSimHubBridge({ ...f.options, chooseDirectory:async () => custom });
  assert.equal((await bridge.choose()).directory, custom);
  await fs.rename(custom, `${custom}-moved`);
  const restarted = createSimHubBridge(f.options);
  assert.ok((await restarted.status()).error);
  await assert.rejects(restarted.install(), /missing/i);
  await assert.rejects(fs.stat(f.target), { code:'ENOENT' });
});
