"use strict";
const { _electron } = require("playwright-core");
const { spawn } = require("node:child_process");
const fs = require("node:fs/promises");
const path = require("node:path");
const os = require("node:os");
const http = require("node:http");

(async()=>{
  const dir=await fs.mkdtemp(path.join(os.tmpdir(),"opengrid-wheel-gpu-"));
  const root=path.resolve(__dirname,"..");
  const bin=path.join(root,"native/build/Release");
  const env={...process.env,LOCALAPPDATA:dir,OPENGRIDXR_TEST_MODE:"1",OPENGRIDXR_DATA_DIR:path.join(dir,"profile")};
  delete env.ELECTRON_RUN_AS_NODE;
  const server=http.createServer((_,res)=>res.end("<html><body style='background: red'></body></html>"));
  let app,receiver;
  try {
    await new Promise(resolve=>server.listen(0,"127.0.0.1",resolve));
    await fs.writeFile(path.join(dir,"test.obj"),"v -1 -1 0\nv 1 -1 0\nv 0 1 0\nf 1 2 3\n");
    receiver=spawn(path.join(bin,"opengrid-overlay-renderer-tests.exe"),["--listen","--seconds","90"],{env,windowsHide:true,stdio:["ignore","pipe","inherit"]});
    await new Promise((resolve,reject)=>{receiver.once("error",reject);receiver.once("exit",()=>reject(Error("GPU receiver exited")));receiver.stdout.once("data",resolve);});
    receiver.stdout.resume();
    app=await _electron.launch({executablePath:process.env.OPENGRIDXR_TEST_EXE || require("electron"),args:process.env.OPENGRIDXR_TEST_EXE?[]:[root],env});
    await app.firstWindow();
    const results=await app.evaluate(async(electron,{root,dir,bin,url})=>{
      if(electron.app.isPackaged) {
        root=electron.app.getAppPath();
        bin=process.resourcesPath+"/native";
      }
      const load=process.getBuiltinModule("module").createRequire(root+"/desktop/main.cjs");
      const model=load(root+"/src/model.mjs");
      const {createSpatialOverlay,wheelModels}=load(root+"/src/spatial-overlays.mjs");
      const {importInWorker}=load(root+"/desktop/wheel-import.cjs");
      const {createOverlayHost}=load(root+"/desktop/overlay-host.cjs");
      const custom=await importInWorker(dir+"/test.obj");
      const controller=await load(root+"/desktop/spatial-overlays.cjs").createSpatialOverlays({
        model,electron,executable:bin+"/OpenGridXR-OverlayHost.exe",
        hostFactory:opts=>createOverlayHost({...opts,testMode:true}),
        getSessions:async()=>[{game:"iracing",active:true,engineLoaded:true}],
      });
      const document=model.createDocument(),p=document.profiles[0],reports=[];
      let udp;
      try {
        const socket=load("node:dgram").createSocket("udp4");
        let sequence=0;
        const frame=Buffer.alloc(20+88*3,77);
        frame.write("OVRLED02");frame.writeUInt32LE(88,12);frame.writeUInt32LE(1,16);
        udp=setInterval(()=>{frame.writeUInt32LE(++sequence,8);socket.send(frame,29543,"127.0.0.1");},50);
        try {
          for(const wheel of [...wheelModels,{...custom,screenHeight:576}]) {
            const item={...createSpatialOverlay(),enabled:true,wheelModel:wheel.id,wheelName:wheel.name,
              wheelLights:wheel.id==="hyper-p1",url,width:512,height:288,widthMeters:.3};
            p.spatialOverlays={enabled:true,pixelBudgetMP:60,items:[item]};
            controller.update(document);
            const until=Date.now()+10000;let status;
            do {
              await new Promise(resolve=>setTimeout(resolve,50));
              status=controller.status("iracing").items[0];
              if(status?.source?.displaying && (!item.wheelLights || status.ledConnected))break;
            } while(Date.now()<until);
            if(!status?.source?.displaying || (item.wheelLights && !status.ledConnected))throw Error(wheel.name+": "+JSON.stringify(controller.status("iracing")));
            if(!wheel.screenHeight && electron.BrowserWindow.getAllWindows().some(win=>win.webContents.getURL()===new URL(url).href))
              throw Error("Screenless wheel loaded the saved dashboard URL.");
            if(wheel.id==="hyper-p1") {
              const browserId=()=>electron.BrowserWindow.getAllWindows().find(win=>win.webContents.getURL()===new URL(url).href)?.id;
              const original=browserId();
              if(!original)throw Error("Wheel browser not found.");
              item.fps=5;item.wheelLights=false;
              controller.update(document);
              const until=Date.now()+3000;
              while(Date.now()<until && electron.BrowserWindow.fromId(original)?.webContents.getFrameRate()!==5)
                await new Promise(resolve=>setTimeout(resolve,30));
              if(!original || browserId()!==original || electron.BrowserWindow.fromId(original)?.webContents.getFrameRate()!==5)
                throw Error("Changing FPS or LED settings restarted the wheel source.");
              // The same physical model can serve a second cockpit overlay.
              p.spatialOverlays.items.push({...item,id:crypto.randomUUID(),x:.4});
              controller.update(document);
              const readyBy=Date.now()+5000;
              while(Date.now()<readyBy && !controller.status("iracing").items.every(s=>s.source?.displaying))
                await new Promise(resolve=>setTimeout(resolve,50));
              if(!controller.status("iracing").items.every(s=>s.source?.displaying))throw Error("Second wheel did not display.");
            }
            reports.push(wheel.name);
          }
        } finally { clearInterval(udp);socket.close(); }
      } finally {controller.close();}
      return reports;
    },{root,dir,bin,url:"http://127.0.0.1:"+server.address().port});
    console.log("Electron, native host, stereo renderer and SimHub loopback passed for",results.join(", "));
  } finally {
    if(app)await app.close();
    if(receiver){receiver.kill();if(receiver.exitCode===null)await new Promise(resolve=>receiver.once("close",resolve));}
    await new Promise(resolve=>server.close(resolve));
    await fs.rm(dir,{recursive:true,force:true,maxRetries:8,retryDelay:100});
  }
})().catch(e=>{console.error(e);process.exitCode=1;});
