const { _electron } = require('playwright-core');
const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const assert = require('node:assert/strict');
(async()=>{
 const dir=await fs.mkdtemp(path.join(os.tmpdir(),'opengrid-menu-renderer-'));
 const entry=path.join(dir,'main.cjs');
 const html=path.resolve('desktop/quick-menu.html'),preload=path.resolve('desktop/quick-menu-preload.cjs');
 await fs.writeFile(entry,`const{app,BrowserWindow,ipcMain}=require('electron');global.calls=[];ipcMain.handle('opengrid:quickMenuAction',(e,id)=>{global.calls.push(id);if(id==='fail')throw new Error('Fixture action failed');return {message:'Applied '+id};});app.whenReady().then(()=>{global.win=new BrowserWindow({width:1024,height:768,useContentSize:true,frame:false,show:false,webPreferences:{preload:${JSON.stringify(preload)},contextIsolation:true,nodeIntegration:false,sandbox:true}});win.loadFile(${JSON.stringify(html)});});`);
 await fs.access(html); await fs.access(preload);
 const env={...process.env};delete env.ELECTRON_RUN_AS_NODE;let app;
 try{
  app=await _electron.launch({executablePath:require('electron'),args:[entry],env});const page=await app.firstWindow();page.setDefaultTimeout(3000);
  // Windows may round a content-sized native window at fractional display DPI.
  await page.setViewportSize({width:1024,height:768});
  await page.waitForFunction(()=>!!window.openGridMenu);
  assert.deepEqual(await page.evaluate(()=>Object.keys(window.openGridMenu).sort()),['onState','runAction']);
  assert.equal(await page.evaluate(()=>typeof window.openGrid),'undefined');
  const send=state=>app.evaluate(({BrowserWindow},s)=>BrowserWindow.getAllWindows()[0].webContents.send('opengrid:quickMenuState',s),state);
  const actions=[{id:'colorCorrection.cycle',label:'Cycle VR color',disabled:false,reason:'',detail:'Live control'},{id:'framegen.enabled',label:'Frame generation',disabled:true,reason:'Enable at startup'},{id:'fail',label:'Fail action',disabled:false,reason:''},...Array.from({length:8},(_,i)=>({id:'overlay-'+i,label:'Overlay '+i,disabled:false,reason:''})),{id:'close',label:'Close menu',disabled:false,reason:''}];
  const state={profileName:'Fixture profile',game:'iRacing',busy:false,message:'Ready',actions,pointer:{valid:true,u:.4,v:.5}};
  await send(state);await page.getByRole('heading',{name:'OpenGrid menu',exact:true}).waitFor();if(process.env.OPENGRID_MENU_SCREENSHOT)await page.screenshot({path:path.resolve(process.env.OPENGRID_MENU_SCREENSHOT)});
  await page.getByRole('button',{name:'Cycle VR color',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('[role="status"]').textContent.includes('Applied'));
  assert.deepEqual(await app.evaluate(()=>global.calls),['colorCorrection.cycle']);
  assert.equal(await page.getByRole('button',{name:'Frame generation',exact:true}).isDisabled(),true);
  assert.match(await page.locator('[data-action="framegen.enabled"]').innerText(),/Enable at startup/);
  assert.equal(await page.locator('#head-cursor').isVisible(),true);
  const color=page.locator('[data-action="colorCorrection.cycle"]');const box=await color.boundingBox();
  await app.evaluate(({BrowserWindow},point)=>BrowserWindow.getAllWindows()[0].webContents.sendInputEvent({type:'mouseMove',x:point.x,y:point.y}),{x:Math.round(box.x+box.width/2),y:Math.round(box.y+box.height/2)});
  assert.equal(await color.evaluate(el=>el.matches(':hover')),true,'Electron source mouse input drives hover');
  await page.getByRole('button',{name:'Fail action',exact:true}).click();await page.waitForFunction(()=>document.querySelector('[role="status"]').textContent.includes('Fixture action failed'));
  await send({...state,busy:true,pointer:{valid:false}});await page.waitForFunction(()=>document.querySelector('[data-action="colorCorrection.cycle"]').disabled);
  assert.equal(await page.locator('#head-cursor').isVisible(),false);assert.equal(await page.getByRole('button',{name:'Close menu',exact:true}).isDisabled(),true);
  await send({...state,message:'<script>unsafe</script>'});await page.waitForFunction(()=>!document.querySelector('[data-action="colorCorrection.cycle"]').disabled);assert.equal(await page.locator('script:not([src])').count(),0);
  // Derive glyph ink bounds from the actual loaded font, rather than trusting
  // scrollHeight (which remains "contained" when overflow cuts off descenders).
  const glyphActions=[
   {label:'Toggle'}, {label:'gypqj'},
   {label:'Toggle performance HUD'},
   {label:'A very long saved overlay source name gypqj '.repeat(3),detail:'A long explanation gypqj that still needs to fit in its button '.repeat(3)},
   {label:'A very long disabled overlay source name gypqj '.repeat(3),disabled:true,reason:'A long reason gypqj explaining why this control is unavailable '.repeat(3)},
  ].map((action,i)=>({id:'glyph-'+i,disabled:false,...action}));
  await send({...state,actions:glyphActions,pointer:{valid:false}});
  await page.waitForFunction(()=>document.querySelector('[data-action="glyph-0"]')?.textContent==='Toggle');
  await page.evaluate(()=>document.fonts.ready);
  const glyphBounds=await page.locator('.action-label,.action-detail').evaluateAll(nodes=>nodes.filter(el=>el.textContent).map(el=>{
   const style=getComputedStyle(el),box=el.getBoundingClientRect(),button=el.parentElement.getBoundingClientRect();
   const canvas=document.createElement('canvas').getContext('2d');canvas.font=`${style.fontWeight} ${style.fontSize} ${style.fontFamily}`;
   const metrics=canvas.measureText(el.textContent),range=document.createRange();range.selectNodeContents(el);
   const lines=[...range.getClientRects()].filter((line,i,all)=>all.findIndex(other=>Math.abs(other.top-line.top)<.1)===i);
   const glyphs=lines.map(line=>({top:line.top+metrics.fontBoundingBoxAscent-metrics.actualBoundingBoxAscent,bottom:line.top+metrics.fontBoundingBoxAscent+metrics.actualBoundingBoxDescent}));
   return {text:el.textContent,kind:el.className,box:{top:box.top,bottom:box.bottom,height:box.height},button:{top:button.top,bottom:button.bottom},ink:glyphs.slice(0,2),hiddenInk:glyphs.slice(2),lineHeight:parseFloat(style.lineHeight)};
  }));
  for(const result of glyphBounds){
   for(const ink of result.ink){
    assert.ok(ink.top>=result.box.top-.1&&ink.bottom<=result.box.bottom+.1,`${result.kind} retains full Segoe UI glyph ink: ${JSON.stringify(result)}`);
    assert.ok(ink.top>=result.button.top&&ink.bottom<=result.button.bottom,'glyphs remain inside their button');
   }
   assert.ok(result.box.height<result.lineHeight*3,'long text stays clamped to two lines');
   assert.ok(result.hiddenInk.every(ink=>ink.top>=result.box.bottom),'no ink from a third line leaks through the descender space');
  }
  assert.equal(await page.locator('.action').evaluateAll(nodes=>nodes.every(el=>{const a=el.children[0].getBoundingClientRect(),b=el.children[1].getBoundingClientRect();return a.bottom<=b.top&&b.bottom<=el.getBoundingClientRect().bottom;})),true,'labels and reasons do not overlap or spill');
  await send(state);await page.waitForFunction(()=>!!document.querySelector('[data-action="colorCorrection.cycle"]'));
  await send({...state,actions:state.actions.map((action,index)=>index===0?{...action,label:'A very long saved overlay source name '.repeat(3),detail:'A detailed explanation that still needs to fit in its button without covering another control.'}:action)});
  await page.waitForFunction(()=>document.querySelector('[data-action="colorCorrection.cycle"]').textContent.includes('very long'));
  assert.equal(await color.evaluate(el=>el.scrollHeight<=el.clientHeight),true,'long source names stay inside their button');
  const assertPage=async(ids)=>{
   assert.deepEqual(await page.locator('#actions > button').evaluateAll(nodes=>nodes.map(node=>node.dataset.action)),ids,'page contains exactly its own controls in order');
   assert.equal(await page.locator('#actions').evaluate(el=>el.scrollHeight<=el.clientHeight&&Array.from(el.children).every(node=>node.getBoundingClientRect().bottom<=el.getBoundingClientRect().bottom)),true,'page controls do not overflow their grid');
  };
  await assertPage(['colorCorrection.cycle','framegen.enabled','fail','overlay-0','overlay-1','overlay-2']);
  await page.getByRole('button',{name:'Next controls',exact:true}).click();
  await assertPage(['overlay-3','overlay-4','overlay-5','overlay-6','overlay-7']);
  await send({...state,message:'Refreshed page two'});await page.waitForFunction(()=>document.querySelector('[role="status"]').textContent==='Refreshed page two');
  await assertPage(['overlay-3','overlay-4','overlay-5','overlay-6','overlay-7']);
  await page.getByRole('button',{name:'Previous controls',exact:true}).click();
  await assertPage(['colorCorrection.cycle','framegen.enabled','fail','overlay-0','overlay-1','overlay-2']);
  await page.getByRole('button',{name:'Next controls',exact:true}).click();
  await send({...state,message:'Fewer controls',actions:[actions[0],actions[2],actions.at(-1)]});await page.waitForFunction(()=>document.querySelector('[role="status"]').textContent==='Fewer controls');
  await assertPage(['colorCorrection.cycle','fail']);
  assert.equal(await page.locator('#page-number').innerText(),'1 / 1');
  await page.getByRole('button',{name:'Close menu',exact:true}).click();await page.waitForFunction(()=>document.querySelector('[role="status"]').textContent.includes('Applied close'));
  console.log('Sandboxed menu renderer, IPC, trusted Electron hover, actions, errors, pagination, busy and cursor verified.');
 }finally{if(app)await app.close();await fs.rm(dir,{recursive:true,force:true});}
})().catch(e=>{console.error(e);process.exitCode=1;});
