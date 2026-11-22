const {_electron:electron}=require('playwright-core');
const fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict');
const {pathToFileURL}=require('node:url');
(async()=>{
  const {createDocument}=await import(pathToFileURL(path.resolve('src/model.mjs')).href);
  const fixture=createDocument(), p=fixture.profiles[0];
  Object.assign(p.helmet,{enabled:true,visorTint:25});
  Object.assign(p.helmet.visor,{opacity:100,height:55,text:'OPEN GRID',color:'#eeeeee',textColor:'#bb2222'});
  const dir=await fs.mkdtemp(path.join(os.tmpdir(),'opengrid-visor-ui-'));
  await fs.writeFile(path.join(dir,'profiles.json'),JSON.stringify(fixture));
  const env={...process.env,OPENGRIDXR_DATA_DIR:dir,OPENGRIDXR_TEST_MODE:'1'};delete env.ELECTRON_RUN_AS_NODE;
  const app=await electron.launch({executablePath:process.env.OPENGRIDXR_TEST_EXE||require('electron'),args:process.env.OPENGRIDXR_TEST_EXE?[]:[path.resolve('.')],env,timeout:60000});
  try {
    const page=await app.firstWindow(),errors=[];page.on('pageerror',e=>errors.push(e.message));
    await page.getByRole('navigation', {name:'Main navigation'}).getByRole('button', {name:'View & crop', exact:true}).click(); await page.getByRole('group', {name:'View & crop section'}).getByRole('button', {name:'Helmet', exact:true}).click();
    const ready=()=>page.waitForFunction(()=>document.querySelector('.helmet-render')?.dataset.rendering==='false'&&document.querySelector('.helmet-render > img')?.complete);
    const settled=async()=>{await page.getByText('Opening and crop updated together. Save to apply in the headset.',{exact:true}).waitFor();await ready();};
    const save=async()=>{await page.getByRole('button',{name:'Save profile',exact:true}).click();await page.getByRole('button',{name:'Profile saved',exact:true}).waitFor({timeout:60000});return page.evaluate(async()=>{const d=(await window.openGrid.load()).document;return d.profiles.find(p=>p.id===d.selectedProfileId);});};
    const position=async name=>{await page.getByRole('group',{name:'Visor position',exact:true}).getByRole('button',{name,exact:true}).click();await settled();};
    const image=()=>page.locator('.helmet-render > img').getAttribute('src');
    async function expectNewRaster(previous) {
      // The crop fit and helmet raster finish asynchronously. The last render
      // can still be marked ready before the new profile reaches the preview.
      await page.waitForFunction(old => {
        const preview=document.querySelector('.helmet-render'), img=preview?.querySelector(':scope > img');
        return preview?.dataset.rendering==='false' && img?.complete && img.naturalWidth>0 &&
          img.getAttribute('src')!==old;
      },previous,{timeout:30000});
    }
    await ready();assert.equal(await page.getByRole('button',{name:'Sim-QVFR · original project',exact:true}).count(),1);
    await page.getByRole('button',{name:'Fit crop to visor opening',exact:true}).click();
    await page.getByText('Shared 2D opening fitted with a conservative margin. Geometry controls keep this fit up to date.',{exact:true}).waitFor();
    const closed=await save(),closedImage=await image();
    await fs.mkdir('test-results/visor-states',{recursive:true});
    await page.locator('.helmet-render').screenshot({path:'test-results/visor-states/closed.png'});
    await position('Fully open');const open=await save();await expectNewRaster(closedImage);const openImage=await image();
    assert.ok(open.view.crop.top<closed.view.crop.top,'Opening a solid strip exposes more upper scene');
    await page.locator('.helmet-render').screenshot({path:'test-results/visor-states/open.png'});
    await position('Half open');await expectNewRaster(openImage);
    await page.locator('.helmet-render').screenshot({path:'test-results/visor-states/half.png'});
    await page.getByRole('switch',{name:'Switch visor while driving',exact:true}).click();await settled();
    const live=await save();assert.deepEqual(live.view.crop,open.view.crop);assert.deepEqual(live.mask.left,open.mask.left);
    await position('Closed');const liveClosed=await save();assert.deepEqual(liveClosed.view.crop,open.view.crop);
    await page.getByRole('group',{name:'Helmet editor section'}).getByRole('button',{name:'Visor',exact:true}).click();
    assert.equal(await page.getByRole('spinbutton',{name:'Scene brightness value',exact:true}).count(),0);
    await page.getByRole('group',{name:'Helmet editor section'}).getByRole('button',{name:'Interior',exact:true}).click();
    const lighting=page.getByRole('spinbutton',{name:'Scene lighting in VR value',exact:true});
    await lighting.fill('65');await lighting.press('Tab');
    assert.equal(await page.getByRole('spinbutton',{name:/^(Head|Vehicle) movement response value$/}).count(),0);
    await page.getByRole('group',{name:'Helmet texture detail'}).getByRole('button',{name:'Detail · 4096',exact:true}).click();
    const saved=await save();assert.equal(saved.helmet.ambientLight,65);assert.equal(saved.helmet.detail,'detail');
    await page.reload();await page.getByRole('navigation', {name:'Main navigation'}).getByRole('button', {name:'View & crop', exact:true}).click(); await page.getByRole('group', {name:'View & crop section'}).getByRole('button', {name:'Helmet', exact:true}).click();await ready();
    assert.equal(await page.getByRole('switch',{name:'Switch visor while driving',exact:true}).getAttribute('aria-checked'),'true');
    assert.deepEqual(errors,[]);console.log('PASS: visor positions, live-safe crop, lighting, detail, persistence and visible credit');
  } finally {await app.evaluate(({BrowserWindow})=>{for(const win of BrowserWindow.getAllWindows())win.destroy();}).catch(()=>{});await app.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
