'use strict';
const fs=require('node:fs/promises'),path=require('node:path');
const manifest=require('../src/visor-template-manifest.json');
function templateAsset(request){
  if(!request||typeof request!=='object'||Array.isArray(request)||Object.keys(request).some(key=>!['style','format'].includes(key))||
    !manifest.styles.includes(request.style)||!Object.hasOwn(manifest.formats,request.format))throw Error('Unsupported visor template request.');
  const format=manifest.formats[request.format];return {...format,file:format.file.replace('{style}',request.style)};
}
function createVisorTemplates({assetDirectory,showSaveDialog}){
  return {async download(request){
    const asset=templateAsset(request);let bytes;
    try{const file=path.join(assetDirectory,asset.file),stat=await fs.stat(file);if(!stat.isFile()||stat.size>32*1024*1024)throw Error();bytes=await fs.readFile(file);}
    catch{throw Error('Visor template is missing. Build or update the application assets.');}
    const result=await showSaveDialog({title:'Save visor template',defaultPath:asset.file,filters:[{name:asset.label,extensions:[asset.extension]}]});
    if(result.canceled||!result.filePath)return {saved:false,canceled:true};
    if(path.extname(result.filePath).toLowerCase()!=='.'+asset.extension)throw Error('Save this template with the .'+asset.extension+' extension.');
    await fs.writeFile(result.filePath,bytes);return {saved:true,fileName:path.basename(result.filePath)};
  }};
}
module.exports={createVisorTemplates,templateAsset};
