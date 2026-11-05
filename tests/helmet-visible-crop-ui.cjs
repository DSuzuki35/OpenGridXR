const { _electron: electron } = require('playwright-core');
const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const assert = require('node:assert/strict');

(async () => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'opengrid-visible-crop-'));
  const env = { ...process.env, OPENGRIDXR_DATA_DIR: dir, OPENGRIDXR_TEST_MODE: '1' };
  delete env.ELECTRON_RUN_AS_NODE;
  const app = await electron.launch({ executablePath: process.env.OPENGRIDXR_TEST_EXE || require('electron'), args: process.env.OPENGRIDXR_TEST_EXE ? [] : [path.resolve('.')], env });
  try {
    const page = await app.firstWindow(), errors = [];
    page.on('pageerror', error => errors.push(error.message));
    const input = name => page.getByRole('spinbutton', { name: name + ' value', exact: true });
    const ready = () => page.waitForFunction(() => document.querySelector('.helmet-render')?.dataset.rendering === 'false' && document.querySelector('.helmet-render > img')?.complete && !document.querySelector('.helmet-preview-error'));
    const settled = async () => { await page.getByText('Opening and crop updated together. Save to apply in the headset.', {exact:true}).waitFor(); await ready(); };
    const save = async () => {
      await page.getByRole('button', {name:'Save profile',exact:true}).click();
      await page.getByRole('button', {name:'Profile saved',exact:true}).waitFor();
      return page.evaluate(async () => { const d=(await window.openGrid.load()).document; return d.profiles.find(p=>p.id===d.selectedProfileId); });
    };
    const section = name => page.getByRole('group',{name:'Helmet editor section'}).getByRole('button',{name,exact:true}).click();
    await page.getByRole('button',{name:'Helmet',exact:true}).click();
    await page.getByRole('switch',{name:'Show helmet',exact:true}).click();
    await input('Vertical visor opening').fill('75'); await settled();
    await section('Sunstrip');
    await input('Sunstrip depth').fill('45');
    await input('Sunstrip curve').fill('0');
    await input('Sunstrip opacity').fill('0'); await ready();
    // Explicit fit reproduces the old failure independently of auto-fit wiring.
    await page.getByRole('button',{name:'Fit crop to visor opening',exact:true}).click();
    await page.getByText('Shared 2D opening fitted with a conservative margin. Geometry controls keep this fit up to date.',{exact:true}).waitFor();
    const clear = await save();
    await input('Sunstrip opacity').fill('100'); await ready();
    await page.getByRole('button',{name:'Fit crop to visor opening',exact:true}).click();
    await page.getByText('Shared 2D opening fitted with a conservative margin. Geometry controls keep this fit up to date.',{exact:true}).waitFor();
    const opaque = await save();
    assert.ok(opaque.view.crop.top > clear.view.crop.top + 5, 'Opaque sunstrip must reduce the visible aperture');
    // Auto fit must reopen the view as soon as scene becomes visible again.
    await input('Sunstrip opacity').fill('99'); await settled();
    const translucent = await save();
    assert.ok(translucent.view.crop.top <= clear.view.crop.top + 1 && translucent.view.crop.top < opaque.view.crop.top - 5, '99% opacity still needs the scene behind the strip (allow one percent for rounded edge alpha)');
    await input('Sunstrip opacity').fill('100'); await settled();
    await section('Interior'); await input('Helmet blur').fill('4'); await settled();
    const blurred = await save();
    assert.ok(blurred.view.crop.top <= opaque.view.crop.top, 'Blur must preserve the newly translucent strip edge');
    const clipped = await page.locator('.helmet-render > img').evaluate((img, profile) => {
      const canvas=document.createElement('canvas');canvas.width=img.naturalWidth;canvas.height=img.naturalHeight;
      const ctx=canvas.getContext('2d');ctx.drawImage(img,0,0);const data=ctx.getImageData(0,0,canvas.width,canvas.height).data;
      const polygon=profile.mask.left,crop=profile.view.crop;let count=0;
      for(let y=0;y<canvas.height;y+=3)for(let x=0;x<canvas.width;x+=3){
        if(data[(y*canvas.width+x)*4+3]===255)continue;
        const px=(x+.5)/canvas.width,py=(y+.5)/canvas.height;
        let pos=false,neg=false;
        for(let i=0;i<polygon.length;i++){const a=polygon[i],b=polygon[(i+1)%polygon.length],d=(b.x-a.x)*(py-a.y)-(b.y-a.y)*(px-a.x);pos ||= d>1e-7;neg ||= d< -1e-7;}
        if((pos&&neg)||px<crop.left/100||px>1-crop.right/100||py<crop.top/100||py>1-crop.bottom/100)count++;
      }return count;
    },blurred);
    assert.equal(clipped,0,'No sampled translucent pixel may be clipped by either contour or rectangular crop');
    await input('Helmet blur').fill('0'); await settled();
    await page.getByRole('button',{name:'Original vertical opening',exact:true}).click(); await settled();
    let saved = await save();
    assert.equal(saved.helmet.openingWidth,100);
    assert.equal(saved.helmet.openingAngle,undefined);
    assert.equal(saved.view.crop.left,0); assert.equal(saved.view.crop.right,0);
    await input('Visor opening width').fill('120'); await settled();
    await input('Vertical visor opening').fill('50'); await settled();
    await page.getByRole('button',{name:'Original vertical opening',exact:true}).click(); await settled();
    saved = await save(); assert.equal(saved.helmet.openingWidth,120,'Reset must preserve a customized width');
    await page.getByRole('group',{name:'Helmet style'}).getByRole('button',{name:'Level 3',exact:true}).click(); await settled();
    assert.equal(await page.locator('.helmet-render > img').getAttribute('data-source'),'level3.png');
    saved = await save(); assert.equal(saved.helmet.style,'level3'); assert.equal(saved.helmet.openingAngle,25);
    await section('Sunstrip'); await page.getByRole('switch',{name:'Upper sunstrip',exact:true}).click(); await settled();
    await input('Visor opening width').fill('100'); await settled();
    const centered = await page.locator('.helmet-render > img').evaluate(img=>{
      const c=document.createElement('canvas');c.width=img.naturalWidth;c.height=img.naturalHeight;const ctx=c.getContext('2d');ctx.drawImage(img,0,0);
      const d=ctx.getImageData(0,Math.floor(c.height/2),c.width,1).data;let l=c.width,r=-1;
      for(let x=0;x<c.width;x++)if(d[x*4+3]<254){l=Math.min(l,x);r=x;}
      return {center:(l+r+1)/2,expected:c.width/2};
    });
    assert.ok(Math.abs(centered.center-centered.expected)<3,'Level 3 width normalization must stay centered');
    await page.getByRole('button',{name:'Original vertical opening',exact:true}).click(); await settled();
    saved = await save();
    const preview=await page.locator('.helmet-render > img').getAttribute('src');
    const baked=await fs.readFile(path.join(dir,'runtime','OpenGridXR-helmet-iracing-left.png'));
    await require('./helmet-image-comparison.cjs')(page, preview, baked);
    await fs.mkdir('test-results/level3',{recursive:true});
    await page.locator('.helmet-render').screenshot({path:'test-results/level3/preview.png'});
    await page.reload(); await page.getByRole('button',{name:'Helmet',exact:true}).click(); await ready();
    assert.equal(await page.locator('.helmet-render > img').getAttribute('data-source'),'level3.png');
    assert.deepEqual(errors,[]);
    console.log('Visible crop, opaque/translucent sunstrip, blur margin, width-preserving reset and Level 3 bake/reload passed.',{clear:clear.view.crop,opaque:opaque.view.crop,blurred:blurred.view.crop});
  } finally {
    await app.evaluate(({BrowserWindow})=>{for(const w of BrowserWindow.getAllWindows())w.destroy();}).catch(()=>{});
    await app.close(); await fs.rm(dir,{recursive:true,force:true});
  }
})().catch(error=>{console.error(error);process.exitCode=1;});
