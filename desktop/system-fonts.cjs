'use strict';
const {execFile}=require('node:child_process');
const {promisify}=require('node:util');
const path=require('node:path');

const MAX_BYTES=1024*1024;
const MAX_FAMILIES=8192;
const SUCCESS_CACHE_MS=5*60*1000;
const FAILURE_CACHE_MS=5000;
// Use the Windows system executable rather than looking up a command on PATH.
const POWERSHELL=path.win32.join(process.env.SystemRoot||'C:\\Windows','System32','WindowsPowerShell','v1.0','powershell.exe');
// No renderer/profile values are interpolated into this read-only query. Only
// family names leave WPF: no font paths, file bytes, or GDI handles are exposed.
const SCRIPT=`$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
Add-Type -AssemblyName PresentationCore
$families = @(@(foreach ($family in [System.Windows.Media.Fonts]::SystemFontFamilies) {
  foreach ($face in $family.GetTypefaces()) {
    $glyph = $null
    if ($face.TryGetGlyphTypeface([ref]$glyph)) {
      $family.Source
      break
    }
  }
}) | Sort-Object -Unique)
[Console]::Write((ConvertTo-Json -InputObject $families -Compress -Depth 2))`;
const ARGS=['-NoProfile','-NonInteractive','-WindowStyle','Hidden','-EncodedCommand',Buffer.from(SCRIPT,'utf16le').toString('base64')];

function parseFamilies(stdout) {
  if(typeof stdout!=='string'||Buffer.byteLength(stdout,'utf8')>MAX_BYTES)throw new Error('invalid font output');
  const values=JSON.parse(stdout.replace(/^\uFEFF/,''));
  if(!Array.isArray(values)||values.length>MAX_FAMILIES)throw new Error('invalid font list');
  const families=[...new Set(values.filter(value=>
    typeof value==='string'&&value.trim().length>0&&[...value].length<=160&&
    !/[\p{Cc}\p{Cf}\p{Cs}\u2028\u2029\\/:]/u.test(value)&&!value.startsWith('#')
  ))].sort();
  if(!families.length)throw new Error('empty font list');
  return families;
}
const unavailable=error=>({available:false,families:[],error});
const copy=result=>({...result,families:[...result.families]});

function createSystemFonts({run=promisify(execFile),platform=process.platform,now=Date.now}={}) {
  let cached=null,expiresAt=0,pending=null;
  async function enumerate() {
    let stdout;
    try {
      ({stdout}=await run(POWERSHELL,[...ARGS],{windowsHide:true,timeout:5000,maxBuffer:MAX_BYTES,encoding:'utf8'}));
    } catch(error) {
      return unavailable(error?.killed||error?.code==='ETIMEDOUT'
        ?'System font enumeration timed out. Refresh to try again.'
        :'System fonts could not be read on this Windows installation. Refresh to try again.');
    }
    try { return {available:true,families:parseFamilies(stdout),error:''}; }
    catch { return unavailable('System font list was invalid, empty, or exceeded the read limit. Refresh to try again.'); }
  }
  return {
    async list({refresh=false}={}) {
      if(platform!=='win32')return unavailable('Installed system fonts are available only in the Windows desktop app.');
      if(pending)return copy(await pending);
      if(!refresh&&cached&&now()<expiresAt)return copy(cached);
      pending=enumerate().then(result=>{
        cached=result;
        expiresAt=now()+(result.available?SUCCESS_CACHE_MS:FAILURE_CACHE_MS);
        return result;
      }).finally(()=>{pending=null;});
      return copy(await pending);
    }
  };
}
module.exports={createSystemFonts};
