const { _electron: electron }=require('playwright-core');
const fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict');
const {spawn}=require('node:child_process');const {randomUUID}=require('node:crypto');
const {atomicWrite}=require('../desktop/store.cjs');
(async()=>{
  const dir=await fs.mkdtemp(path.join(os.tmpdir(),'opengrid-benchmark-ui-')),namespace=`benchmark-${randomUUID()}`;
  const fixture=spawn(path.resolve('native/build/Release/opengrid-telemetry-fixture.exe'),['--namespace',namespace,'--duration-ms','20000','--freeze-after-ms','19000'],{windowsHide:true,stdio:['ignore','pipe','pipe']});
  await new Promise((resolve,reject)=>{fixture.once('error',reject);fixture.stdout.once('data',resolve);});
  const env={...process.env,OPENGRIDXR_DATA_DIR:dir,OPENGRIDXR_TEST_MODE:'1',OPENGRIDXR_TELEMETRY_TEST_NAMESPACE:namespace};delete env.ELECTRON_RUN_AS_NODE;
  const app=await electron.launch({executablePath:process.env.OPENGRIDXR_TEST_EXE||require('electron'),args:process.env.OPENGRIDXR_TEST_EXE?[]:[path.resolve('.')],env});
  let timer,pending=Promise.resolve();
  try{
    const page=await app.firstWindow(),errors=[];page.on('pageerror',e=>errors.push(e.message));
    await page.getByRole('button',{name:'Lap benchmark',exact:true}).click();
    const pid=await app.evaluate(()=>process.pid),id=`${pid}-100-benchmarkfixture`,root=path.join(dir,'runtime/enhancements-data/runtime/status');
    await fs.mkdir(root,{recursive:true});let sequence=0;const frames=[];
    async function write(name,data){await atomicWrite(path.join(root,name),JSON.stringify(data));}
    async function publish(){const time=Date.now();for(let i=2;i>=0;i--)frames.push({sequence:++sequence,timestampMs:time-i*16,frameMs:16.6667,cpuMs:null,gpuMs:null});if(frames.length>1024)frames.splice(0,frames.length-1024);
      await write(id+'.json',{protocolVersion:1,sessionId:id,processId:pid,application:'iRacingSim64DX11.exe',updatedAtUnixMilliseconds:time,capabilities:{benchmark:true}});
      await write(id+'_benchmark.json',{sessionId:id,timestampMs:time,frames,counters:{application:sequence,generated:sequence*2,repeated:0,submitted:sequence*3,displayed:null},framegen:{state:'active',mode:'interpolate',multiplier:3,refreshHz:180}});
    }
    await publish();timer=setInterval(()=>{pending=pending.then(publish);pending.catch(()=>{});},50);
    await page.getByRole('button',{name:'Start recording',exact:true}).click();
    await page.getByRole('button',{name:'Stop recording',exact:true}).waitFor();
    const deadline=Date.now()+10000;let live;
    do{live=await page.evaluate(()=>window.openGrid.benchmarkStatus());if(live.frameCount>30&&live.telemetryCount>10)break;await new Promise(resolve=>setTimeout(resolve,50));}while(Date.now()<deadline);
    assert(live.frameCount>30&&live.telemetryCount>10,JSON.stringify(live));
    await page.getByRole('button',{name:'Stop recording',exact:true}).click();
    await page.getByRole('button',{name:'Export HTML report',exact:true}).waitFor();
    const report=await page.evaluate(()=>window.openGrid.benchmarkReport());
    assert(report.summary.counters.generated>0,JSON.stringify({summary:report.summary,reason:report.stopReason,warnings:report.warnings}));assert(report.summary.timing.frame.sampleCount>30);
    assert.equal(report.summary.counters.displayed,null);assert.equal(report.summary.context.carsVisible.max,null);
    assert(report.summary.context.airTempC.sampleCount>0);assert(report.laps.every(l=>l.complete===false));
    await app.evaluate(({dialog},dir)=>{dialog.showSaveDialog=async(_win,options)=>({canceled:false,filePath:dir+'/'+options.defaultPath});},dir);
    await page.getByRole('button',{name:'Export HTML report',exact:true}).click();
    await page.getByRole('button',{name:'Export CSV data',exact:true}).click();
    await page.getByRole('button',{name:'Export CSV data',exact:true}).waitFor({state:'visible'});
    assert.equal(await page.getByRole('alert').count(),0,await page.locator('.benchmark-error').allTextContents());
    const html=await fs.readFile(path.join(dir,'OpenGridXR-iracing-benchmark.html'),'utf8');assert.match(html,/Lap comparison/);assert.match(html,/Generated|generated/);assert.doesNotMatch(html,/<script/);
    const csv=await fs.readFile(path.join(dir,'OpenGridXR-iracing-benchmark.csv'),'utf8');assert.match(csv,/"frame"/);assert.match(csv,/"telemetry"/);
    assert.equal((await fs.readdir(path.join(dir,'benchmarks'))).length,1);
    await page.screenshot({path:path.resolve('test-results/benchmark-report.png'),fullPage:true});
    assert.deepEqual(errors,[]);console.log('Real desktop benchmark start/stop, native telemetry, actual feed counters, partial-lap report, local save and HTML/CSV exports passed.');
  }finally{clearInterval(timer);await pending;await app.close();fixture.kill();await fs.rm(dir,{recursive:true,force:true});}
})().catch(e=>{console.error(e);process.exitCode=1;});
