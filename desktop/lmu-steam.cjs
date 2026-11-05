'use strict';
const fs = require('node:fs/promises');
const path = require('node:path');
const {execFile} = require('node:child_process');
const {promisify} = require('node:util');
const run = promisify(execFile);

function steamLaunchOption(executable) {
  if (!path.isAbsolute(executable) || /["%\r\n\0]/.test(executable)) throw new Error('Invalid OpenGrid executable path.');
  return `"${executable}" --lmu-steam-launch %command%`;
}

async function protectedTarget(gameExecutable, args) {
  if (!Array.isArray(args) || !args.length || args.some(arg => typeof arg !== 'string' || /[\0\r\n]/.test(arg)) || args.join(' ').length > 24000)
    throw new Error('Invalid Steam protected-launch arguments.');
  const expected = path.join(path.dirname(gameExecutable), 'start_protected_game.exe');
  if (!path.isAbsolute(args[0]) || path.resolve(args[0]).toLowerCase() !== expected.toLowerCase())
    throw new Error('Steam must pass LMU start_protected_game.exe. Use the normal EAC-enabled Steam launch; a direct game launch is refused.');
  const actual = await fs.realpath(args[0]).catch(() => '');
  const directory = await fs.realpath(path.dirname(gameExecutable));
  if (path.dirname(actual).toLowerCase() !== directory.toLowerCase() || !(await fs.stat(actual).catch(() => null))?.isFile())
    throw new Error('The protected launcher must be inside the selected LMU installation.');
  const forwarded = args.slice(1);
  if (!forwarded.some(arg => arg.toLowerCase() === '+xr')) forwarded.push('+XR');
  return {executable: actual, args: forwarded};
}

async function verifySignatures(files) {
  if (process.platform !== 'win32') throw new Error('Signature verification requires Windows.');
  // Quote literal paths for PowerShell, never interpolate command fragments.
  const literals = files.map(file => "'" + file.replace(/'/g, "''") + "'").join(',');
  const script = `$ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue'; [Console]::OutputEncoding=[Text.UTF8Encoding]::new($false); Import-Module (Join-Path $PSHOME 'Modules\\Microsoft.PowerShell.Security\\Microsoft.PowerShell.Security.psd1'); @(${literals}) | ForEach-Object { $s=Get-AuthenticodeSignature -LiteralPath $_; [pscustomobject]@{file=$_;status=$s.Status.ToString()} } | ConvertTo-Json -Compress`;
  const {stdout} = await run(path.join(process.env.SystemRoot || 'C:\\Windows', 'System32','WindowsPowerShell','v1.0','powershell.exe'),
    ['-NoProfile','-NonInteractive','-EncodedCommand',Buffer.from(script,'utf16le').toString('base64')],{windowsHide:true,timeout:30000});
  const results = JSON.parse(stdout.replace(/^\uFEFF/,''));
  const list = Array.isArray(results) ? results : [results];
  if (list.length !== files.length || list.some((entry,i) => entry.file !== files[i] || entry.status !== 'Valid'))
    throw new Error('Steam/EAC launch requires valid signatures on the protected launcher and both OpenGrid DLLs. Install the signed build first.');
}

module.exports = {steamLaunchOption, protectedTarget, verifySignatures};
