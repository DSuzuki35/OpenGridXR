const nodeFs = require('node:fs/promises');
const { constants } = require('node:fs');
const path = require('node:path');
const { createHash, randomUUID } = require('node:crypto');
const { promisify } = require('node:util');
const runFile = promisify(require('node:child_process').execFile);

const PLUGIN = 'oVRlay.SimHubLeds.dll';
const hash = bytes => createHash('sha256').update(bytes).digest('hex');

// Only the main process constructs this service and supplies its bundled source
// and folder dialog. All work is on demand; nothing runs on the telemetry path.
function createSimHubBridge({ sourceFile, dataDirectory, chooseDirectory,
  fs = nodeFs, env = process.env, platform = process.platform, execFile = runFile }) {
  let queue = Promise.resolve();
  let selected;
  const preferences = path.join(dataDirectory, 'simhub-bridge.json');
  const serial = operation => {
    const result = queue.then(operation);
    queue = result.catch(() => {});
    return result;
  };
  const normalize = file => platform === 'win32' ? path.resolve(file).toLowerCase() : path.resolve(file);
  const systemTool = name => path.join(env.SystemRoot || 'C:\\Windows', 'System32', name);
  const command = (name, args) => execFile(systemTool(name), args, { windowsHide:true, timeout:5000, maxBuffer:4 * 1024 * 1024, encoding:'utf8' });

  async function statOptional(file) {
    try { return await fs.lstat(file); } catch (error) { if (error.code === 'ENOENT') return null; throw error; }
  }

  // Reject both the final symlink and redirects in any ancestor (Windows
  // junctions included). No network/device paths or alternate data streams.
  async function validatePath(file, kind) {
    if (typeof file !== 'string' || !path.isAbsolute(file) || file.includes('\0') ||
      (platform === 'win32' && (/^[\\/]{2}/.test(file) || /:/.test(file.slice(2))))) {
      throw new Error('Choose a local SimHub folder without redirected paths.');
    }
    const absolute = path.resolve(file);
    let current = absolute;
    let leaf;
    while (true) {
      const info = await fs.lstat(current);
      if (info.isSymbolicLink()) throw new Error('Symbolic links and redirected folders are not supported. Choose the real SimHub folder.');
      if (current === absolute) leaf = info;
      else if (!info.isDirectory()) throw new Error('Invalid SimHub folder path.');
      const parent = path.dirname(current);
      if (parent === current) break;
      current = parent;
    }
    if (normalize(await fs.realpath(absolute)) !== normalize(absolute)) throw new Error('Redirected paths are not supported. Choose the real SimHub folder.');
    if (kind === 'directory' ? !leaf.isDirectory() : !leaf.isFile()) throw new Error('Choose a valid SimHub installation folder.');
    if (kind !== 'directory' && leaf.nlink > 1) throw new Error('Hard-linked plugin files are not supported. Use a regular file.');
    return leaf;
  }

  async function validateDirectory(directory) {
    await validatePath(directory, 'directory');
    const executable = await statOptional(path.join(directory, 'SimHubWPF.exe')) ? 'SimHubWPF.exe' : 'SimHub.exe';
    try {
      await validatePath(path.join(directory, executable), 'file');
      await validatePath(path.join(directory, 'SimHub.Plugins.dll'), 'file');
    } catch (error) {
      if (error.code === 'ENOENT') throw new Error('Choose a valid SimHub installation folder containing SimHub and SimHub.Plugins.dll.');
      throw error;
    }
    return path.resolve(directory);
  }

  async function sourceBytes() {
    try {
      if (path.basename(sourceFile) !== PLUGIN) throw new Error('Unexpected filename');
      const info = await validatePath(sourceFile, 'file');
      if (info.size < 68 || info.size > 64 * 1024 * 1024) throw new Error('Invalid size');
      const bytes = await fs.readFile(sourceFile);
      const peOffset = bytes.readUInt32LE(60);
      if (bytes.toString('ascii', 0, 2) !== 'MZ' || peOffset > bytes.length - 4 ||
        !bytes.subarray(peOffset, peOffset + 4).equals(Buffer.from('PE\0\0'))) throw new Error('Invalid DLL');
      return bytes;
    } catch {
      throw new Error('The bundled SimHub bridge is missing or invalid. Reinstall OpenGrid XR before trying again.');
    }
  }

  async function registryDirectories() {
    const directories = [];
    for (const hive of ['HKLM','HKCU']) {
      for (const view of ['32','64']) {
        try {
          const { stdout } = await command('reg.exe', ['query', `${hive}\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\SimHub_is1`, '/v', 'InstallLocation', `/reg:${view}`]);
          const match = stdout.match(/^\s*InstallLocation\s+REG_(?:EXPAND_)?SZ\s+(.+)$/im);
          if (match) directories.push(match[1].trim().replace(/%([^%]+)%/g, (value, key) => env[key] || value));
        } catch { /* Registry discovery is optional; chooser always remains available. */ }
      }
    }
    return directories;
  }

  async function discover() {
    if (selected === undefined) {
      selected = null;
      try {
        if (await statOptional(preferences)) {
          await validatePath(preferences, 'file');
          const saved = JSON.parse(await fs.readFile(preferences, 'utf8'));
          if (typeof saved.directory === 'string') selected = saved.directory;
        }
      } catch (error) {
        if (error instanceof SyntaxError) throw new Error('The saved SimHub folder could not be read. Choose the SimHub folder again.');
        throw error;
      }
    }
    // Never silently install elsewhere when a remembered selection is invalid.
    if (selected) return validateDirectory(selected);
    const defaults = [env['ProgramFiles(x86)'], env.ProgramFiles].filter(Boolean).map(root => path.join(root, 'SimHub'));
    for (const candidate of defaults) {
      try { return await validateDirectory(candidate); } catch { /* Try next candidate. */ }
    }
    for (const candidate of await registryDirectories()) {
      try { return await validateDirectory(candidate); } catch { /* Ignore unrelated/stale registry paths. */ }
    }
    return null;
  }

  async function isRunning() {
    try {
      const { stdout } = await command('tasklist.exe', ['/FO', 'CSV', '/NH']);
      const lines = stdout.trim().split(/\r?\n/).filter(Boolean);
      if (!lines.length) throw new Error('Empty process list');
      let found = false;
      for (const line of lines) {
        const match = line.match(/^"((?:[^"\r\n]|"")*)","\d+",/);
        if (!match) throw new Error('Invalid process list');
        if (/^SimHub(?:WPF)?\.exe$/i.test(match[1])) found = true;
      }
      return found;
    } catch {
      throw new Error('Could not check whether SimHub is running. Close SimHub and retry, or use the manual copy option.');
    }
  }

  function friendly(error) {
    if (['EACCES','EPERM','EBUSY'].includes(error.code)) {
      return new Error('Windows denied permission to install the bridge. Close SimHub and retry, or open the bridge folder and copy the DLL manually.');
    }
    if (error.code === 'ENOENT') return new Error('The SimHub folder is missing. Choose the SimHub installation folder again.');
    return error;
  }

  async function readStatus() {
    const result = { directory:selected || null, available:false, installed:false, current:false, running:null, message:'', error:null };
    if (platform !== 'win32') {
      result.message = 'SimHub bridge installation is available on Windows.';
      return result;
    }
    try {
      const bytes = await sourceBytes();
      result.directory = await discover();
      if (!result.directory) { result.message = 'Choose your SimHub installation folder.'; return result; }
      result.available = true;
      const target = path.join(result.directory, PLUGIN);
      if (await statOptional(target)) {
        await validatePath(target, 'file');
        result.installed = true;
        result.current = hash(await fs.readFile(target)) === hash(bytes);
      }
      result.running = await isRunning();
      result.message = result.running ? 'Close SimHub before installing or updating the bridge.' : result.current
        ? 'The SimHub bridge is up to date.' : result.installed ? 'A SimHub bridge update is available.' : 'Ready to install the SimHub bridge.';
    } catch (error) {
      result.error = friendly(error).message;
      result.message = result.error;
    }
    return result;
  }

  async function choose() {
    if (platform !== 'win32') throw new Error('SimHub bridge installation is available on Windows.');
    const choice = await chooseDirectory();
    if (!choice) return readStatus();
    const directory = await validateDirectory(choice);
    await fs.mkdir(dataDirectory, { recursive:true });
    await validatePath(dataDirectory, 'directory');
    if (await statOptional(preferences)) await validatePath(preferences, 'file');
    const temporary = path.join(dataDirectory, `simhub-bridge-${randomUUID()}.tmp`);
    try {
      await fs.writeFile(temporary, JSON.stringify({ directory }), { flag:'wx' });
      await fs.rename(temporary, preferences);
      selected = directory;
    } finally { await fs.unlink(temporary).catch(() => {}); }
    return readStatus();
  }

  async function install() {
    if (platform !== 'win32') throw new Error('SimHub bridge installation is available on Windows.');
    const status = await readStatus();
    if (status.error) throw new Error(status.error);
    if (!status.directory) throw new Error('Choose a valid SimHub installation folder first.');
    if (status.running !== false) throw new Error('Close SimHub before installing or updating the bridge.');
    if (status.current) return status;
    const directory = status.directory;
    const target = path.join(directory, PLUGIN);
    const temporary = path.join(directory, `${PLUGIN}.${randomUUID()}.tmp`);
    const backup = path.join(directory, `${PLUGIN}.opengrid-backup-${randomUUID()}`);
    const source = await sourceBytes();
    let staged = false;
    try {
      await validateDirectory(directory);
      await fs.copyFile(sourceFile, temporary, constants.COPYFILE_EXCL);
      staged = true;
      await validatePath(temporary, 'file');
      if (hash(await fs.readFile(temporary)) !== hash(source)) throw new Error('The copied bridge could not be verified. Please retry.');
      if (await statOptional(target)) {
        await validatePath(target, 'file');
        const previous = hash(await fs.readFile(target));
        await fs.copyFile(target, backup, constants.COPYFILE_EXCL);
        if (hash(await fs.readFile(backup)) !== previous) throw new Error('The previous bridge backup could not be verified. Installation stopped.');
      }
      // Recheck immediately before the only operation replacing a loadable DLL.
      if (await isRunning()) throw new Error('Close SimHub before installing or updating the bridge.');
      await validateDirectory(directory);
      if (await statOptional(target)) await validatePath(target, 'file');
      await validatePath(temporary, 'file');
      await fs.rename(temporary, target);
      staged = false;
      if (hash(await fs.readFile(target)) !== hash(source)) throw new Error('The installed bridge could not be verified. Use the manual copy option.');
      const result = await readStatus();
      if (!result.error) result.message = 'Bridge installed. Open SimHub, enable oVRlay lights in Plugins, then enable Mirror my wheel lights in oVRlay.';
      return result;
    } finally {
      // Never follow a replaced directory to clean up a temporary file.
      if (staged) {
        try { await validateDirectory(directory); await fs.unlink(temporary); } catch { /* Preserve the original error. */ }
      }
    }
  }

  const guarded = operation => serial(async () => { try { return await operation(); } catch (error) { throw friendly(error); } });
  return { status:() => serial(readStatus), choose:() => guarded(choose), install:() => guarded(install) };
}

module.exports = { createSimHubBridge };
