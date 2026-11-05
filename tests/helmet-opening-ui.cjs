const { _electron: electron } = require('playwright-core');
const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const assert = require('node:assert/strict');
(async () => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'opengrid-opening-ui-'));
  const env = { ...process.env, OPENGRIDXR_DATA_DIR: dir, OPENGRIDXR_TEST_MODE: '1' };
  delete env.ELECTRON_RUN_AS_NODE;
  const app = await electron.launch({ executablePath: process.env.OPENGRIDXR_TEST_EXE || require('electron'), args: process.env.OPENGRIDXR_TEST_EXE ? [] : [path.resolve('.')], env });
  try {
    const page = await app.firstWindow(), errors = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.getByRole('button', {name:'Helmet', exact:true}).click();
    await page.getByRole('switch', {name:'Show helmet', exact:true}).click();
    const ready = async () => {
      try { await page.waitForFunction(() => document.querySelector('.helmet-render')?.dataset.rendering === 'false' && document.querySelector('.helmet-render > img')?.complete); }
      catch (error) {
        console.error('Helmet preview state', await page.evaluate(() => ({ rendering: document.querySelector('.helmet-render')?.dataset.rendering, imageComplete: document.querySelector('.helmet-render > img')?.complete, error: document.querySelector('.helmet-preview-error')?.textContent })), errors);
        throw error;
      }
    };
    const settled = async () => {
      await page.getByText('Opening and crop updated together. Save to apply in the headset.', {exact:true}).waitFor();
      await ready();
    };
    const input = name => page.getByRole('spinbutton', {name: name+' value', exact:true});
    const save = async () => { await page.getByRole('button', {name:'Save profile', exact:true}).click(); await page.getByRole('button', {name:'Profile saved', exact:true}).waitFor(); return page.evaluate(async () => {const d=(await window.openGrid.load()).document;return d.profiles.find(p=>p.id===d.selectedProfileId);}); };
    await fs.mkdir('test-results/visor-opening', {recursive:true});
    const stats = () => page.locator('.helmet-render > img').evaluate(img => {
      const c=document.createElement('canvas');c.width=img.naturalWidth;c.height=img.naturalHeight;
      const ctx=c.getContext('2d');ctx.drawImage(img,0,0);const data=ctx.getImageData(0,0,c.width,c.height).data;
      let count=0,sum=0,opaque=0,center=[];
      for(let i=0;i<data.length;i+=4){if(data[i+3]<254)count++;else{opaque++;sum+=data[i]+data[i+1]+data[i+2];}}
      for(let y=0;y<c.height;y++)if(data[(y*c.width+Math.floor(c.width/2))*4+3]<254)center.push(y);
      return {transparent:count,mean:sum/Math.max(1,opaque)/3,centerTop:Math.min(...center),centerBottom:Math.max(...center),source:img.src};
    });
    await ready();
    await page.evaluate(() => { window.originalImageDecode = Image.prototype.decode; Image.prototype.decode = async function() { await new Promise(resolve => setTimeout(resolve, 600)); return window.originalImageDecode.call(this); }; });
    await input('Vertical visor opening').fill('25');
    assert.equal(await page.getByRole('button', {name:'View & crop',exact:true}).isDisabled(), true);
    assert.equal(await page.getByLabel('Game',{exact:true}).isDisabled(), true);
    assert.equal(await page.getByLabel('Active profile',{exact:true}).isDisabled(), true);
    assert.equal(await page.getByRole('button',{name:'Adjusting opening…',exact:true}).isDisabled(), true);
    await settled();
    await page.evaluate(() => { Image.prototype.decode = window.originalImageDecode; });
    const narrow = await stats();
    await page.locator('.helmet-render').screenshot({path:'test-results/visor-opening/narrow.png'});
    let saved = await save();
    assert.equal(saved.helmet.openingAngle,25);assert.equal(saved.helmet.openingWidth,100);
    assert.equal(saved.view.crop.left,0);assert.equal(saved.view.crop.right,0);
    assert.ok(saved.view.crop.top+saved.view.crop.bottom<=70);
    assert.ok(saved.view.crop.top>20 && saved.view.crop.bottom>20);
    await input('Vertical visor opening').fill('60');await settled();
    const middle = await stats();assert.ok(middle.transparent>narrow.transparent);
    await page.locator('.helmet-render').screenshot({path:'test-results/visor-opening/middle.png'});
    await input('Vertical visor opening').fill('60.5');await settled();
    assert.notEqual((await stats()).source,middle.source);
    await input('Vertical visor opening').fill('100');await settled();
    const open = await stats();assert.ok(open.transparent>middle.transparent);
    await page.locator('.helmet-render').screenshot({path:'test-results/visor-opening/open.png'});
    saved=await save();assert.equal(saved.view.crop.left+saved.view.crop.right+saved.view.crop.top+saved.view.crop.bottom,0);
    await input('Vertical visor opening').fill('40');await input('Visor opening width').fill('70');await settled();
    saved=await save();assert.equal(saved.helmet.openingAngle,40);assert.equal(saved.helmet.openingWidth,70);assert.ok(saved.view.crop.left>0 && saved.view.crop.right>0);
    await input('Visor opening width').fill('100');await settled();
    await page.getByRole('group',{name:'Helmet editor section'}).getByRole('button',{name:'Interior',exact:true}).click();
    const before=await stats();
    await input('Interior brightness').fill('25');await ready();
    const darker=await stats();assert.ok(darker.mean<before.mean*.6);assert.equal(darker.transparent,before.transparent);
    await input('Interior contrast').fill('120');await input('Interior saturation').fill('0');await ready();
    saved=await save();assert.equal(saved.helmet.shellBrightness,25);assert.equal(saved.helmet.shellContrast,120);assert.equal(saved.helmet.shellSaturation,0);
    await page.locator('.helmet-render').screenshot({path:'test-results/visor-opening/dark.png'});
    await page.reload();await page.getByRole('button',{name:'Helmet',exact:true}).click();await ready();
    assert.equal(await input('Vertical visor opening').inputValue(),'40');assert.equal(await input('Visor opening width').inputValue(),'100');
    await page.getByRole('group',{name:'Helmet editor section'}).getByRole('button',{name:'Interior',exact:true}).click();
    assert.equal(await input('Interior brightness').inputValue(),'25');
    await page.getByRole('button',{name:'Reset interior colors',exact:true}).click();await ready();
    assert.equal(await input('Interior brightness').inputValue(),'100');
    await page.getByRole('group',{name:'Helmet editor section'}).getByRole('button',{name:'Sunstrip',exact:true}).click();
    await page.getByRole('button',{name:'Dark / white',exact:true}).click();
    await input('Text curvature').fill('0');
    await input('Sunstrip opacity').fill('100');
    await page.getByRole('textbox',{name:'Sunstrip text',exact:true}).fill('OPEN GRID');
    const letteringBounds = () => page.locator('.helmet-render > img').evaluate(img => {
      const c=document.createElement('canvas');c.width=img.naturalWidth;c.height=img.naturalHeight;
      const ctx=c.getContext('2d');ctx.drawImage(img,0,0);const d=ctx.getImageData(0,0,c.width,c.height).data;
      let minX=c.width,maxX=-1,minY=c.height,maxY=-1;
      for(let y=0;y<c.height;y++)for(let x=0;x<c.width;x++){const a=(y*c.width+x)*4;if(d[a]>200&&d[a+1]>200&&d[a+2]>200&&d[a+3]>180){minX=Math.min(minX,x);maxX=Math.max(maxX,x);minY=Math.min(minY,y);maxY=Math.max(maxY,y);}}
      let edge=0;
      for(let y=Math.max(0,minY-1);y<=Math.min(c.height-1,maxY+1);y++)for(let x=Math.max(1,minX-1);x<=Math.min(c.width-1,maxX+1);x++){const a=(y*c.width+x)*4;edge=Math.max(edge,Math.abs(d[a]-d[a-4]));}
      return {edge,cy:(minY+maxY)/2,width:maxX-minX+1,height:maxY-minY+1,ratio:(maxX-minX+1)/(maxY-minY+1)};
    });
    await input('Vertical visor opening').fill('100');await settled();const openPrint=await letteringBounds();
    await input('Vertical visor opening').fill('43');await settled();const narrowPrint=await letteringBounds();
    console.log('Lettering bounds:',{openPrint,narrowPrint});
    assert.ok(Math.abs(narrowPrint.ratio/openPrint.ratio-1)<.18,'Opening must not flatten the print');
    await page.locator('.helmet-render').screenshot({path:'test-results/visor-opening/print-43.png'});
    await page.getByText('Print position & softness',{exact:true}).click();
    await input('Print opacity').fill('0');await ready();const noPrint=(await stats()).source;
    await input('Print opacity').fill('2');await ready();const dimPrint=(await stats()).source;
    const dimBounds=await page.evaluate(async ({a,b})=>{
      const pixels=async src=>{const i=new Image();i.src=src;await i.decode();const c=document.createElement('canvas');c.width=i.width;c.height=i.height;const ctx=c.getContext('2d');ctx.drawImage(i,0,0);return {data:ctx.getImageData(0,0,c.width,c.height).data,w:c.width,h:c.height};};
      const first=await pixels(a),second=await pixels(b);let min=first.h,max=-1;
      for(let y=0;y<first.h;y++)for(let x=0;x<first.w;x++){const at=(y*first.w+x)*4;if(second.data[at]>first.data[at]||second.data[at+1]>first.data[at+1]||second.data[at+2]>first.data[at+2]){min=Math.min(min,y);max=Math.max(max,y);}}
      return {height:max-min+1,cy:(min+max)/2};
    },{a:noPrint,b:dimPrint});
    assert.ok(dimBounds.height>20 && Math.abs(dimBounds.cy-narrowPrint.cy)<12,'Low-opacity print must stay in its fitted position');
    await input('Print opacity').fill('100');await ready();

    await page.getByRole('group',{name:'Helmet editor section'}).getByRole('button',{name:'Interior',exact:true}).click();
    await input('Vertical position').fill('-21');await settled();await ready();
    const bottomAlpha=await page.locator('.helmet-render > img').evaluate(img=>{const c=document.createElement('canvas');c.width=img.naturalWidth;c.height=img.naturalHeight;const ctx=c.getContext('2d');ctx.drawImage(img,0,0);return ctx.getImageData(Math.floor(c.width/2),c.height-1,1,1).data[3];});
    assert.equal(bottomAlpha,255,'Moved helmet must cover the newly exposed bottom edge');
    saved=await save();assert.equal(saved.helmet.offsetY,-21);assert.ok(saved.view.crop.bottom>=35,'Auto crop follows the translated opening');
    await page.locator('.helmet-render').screenshot({path:'test-results/visor-opening/position-up.png'});
    await input('Vertical position').fill('21');await settled();saved=await save();assert.equal(saved.helmet.offsetY,21);assert.ok(saved.view.crop.top>=35);
    await page.locator('.helmet-render').screenshot({path:'test-results/visor-opening/position-down.png'});
    await input('Vertical position').fill('0');await settled();
    await input('Vertical visor opening').fill('25');await settled();const tinyPrint=await letteringBounds();
    assert.ok(tinyPrint.height>10);assert.ok(Math.abs(tinyPrint.ratio/openPrint.ratio-1)<.18,'25 degree print must shrink proportionally');
    await input('Vertical visor opening').fill('43');await settled();
    const sharp=await stats(), sharpText=await letteringBounds();saved=await save();const sharpCrop=saved.view.crop;
    await input('Helmet blur').fill('3');await settled();const soft=await stats();
    const softText=await letteringBounds();
    assert.ok(softText.width>sharpText.width*.95,'Blur must not shrink the text layout');
    assert.ok(softText.edge<sharpText.edge*.6,'Helmet blur must soften letter edges too');
    assert.ok(Math.abs(softText.cy-sharpText.cy)<3,'Blur keeps text position');
    assert.notEqual(soft.source,sharp.source);assert.ok(soft.transparent>=sharp.transparent,'Blurred aperture must include newly translucent edge pixels');
    saved=await save();assert.equal(saved.helmet.shellBlur,3);assert.ok(saved.view.crop.top<=sharpCrop.top && saved.view.crop.bottom<=sharpCrop.bottom);
    await page.locator('.helmet-render').screenshot({path:'test-results/visor-opening/blur.png'});
    await input('Helmet blur').fill('0');await settled();

    await page.getByRole('group',{name:'Helmet editor section'}).getByRole('button',{name:'Sunstrip',exact:true}).click();
    const fonts=new Set();
    for(const font of ['arial','arialNarrow','impact','georgia','consolas','trebuchet']) {
      await page.getByRole('combobox',{name:'Letter font',exact:true}).selectOption(font);await ready();
      const raster=(await stats()).source;console.log('Font raster:',font,raster.length);assert.ok(!fonts.has(raster),'Each font renders distinct glyphs: '+font);fonts.add(raster);
    }
    saved=await save();assert.equal(saved.helmet.visor.font,'trebuchet');
    const preview=(await stats()).source;
    const baked=await fs.readFile(path.join(dir,'runtime','OpenGridXR-helmet-iracing-left.png'));
    await require('./helmet-image-comparison.cjs')(page, preview, baked);
    await page.reload();await page.getByRole('button',{name:'Helmet',exact:true}).click();await ready();
    assert.equal(await page.getByRole('combobox',{name:'Letter font',exact:true}).inputValue(),'trebuchet');
    await page.locator('.helmet-render').screenshot({path:'test-results/visor-opening/font-trebuchet.png'});
    await page.getByRole('button',{name:'Original vertical opening',exact:true}).click();await settled();
    saved=await save();assert.equal(saved.helmet.openingAngle,undefined);assert.equal(saved.helmet.openingWidth,100);
    assert.deepEqual(errors,[]);
    console.log('Opening 25/60/60.5/100, width 70/100, atomic crop, grading alpha invariance, reset and save/reload passed.', {narrow:narrow.transparent,middle:middle.transparent,open:open.transparent});
  } finally {
    await app.evaluate(({BrowserWindow})=>{for(const w of BrowserWindow.getAllWindows())w.destroy();}).catch(()=>{});
    await app.close();await fs.rm(dir,{recursive:true,force:true});
  }
})().catch(error=>{console.error(error);process.exitCode=1;});
