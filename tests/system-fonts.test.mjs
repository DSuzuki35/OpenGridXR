import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';

let createSystemFonts;
try { ({createSystemFonts}=createRequire(import.meta.url)('../desktop/system-fonts.cjs')); } catch {}
function service(options={}) {
  assert.equal(typeof createSystemFonts,'function','Windows font enumeration service exists');
  return createSystemFonts({platform:'win32',...options});
}
const output=families=>({stdout:JSON.stringify(families),stderr:''});

test('system font query is lazy and uses the fixed hidden bounded PowerShell process',async()=>{
  const calls=[];
  const fonts=service({run:async(...args)=>{calls.push(args);return output(['Segoe UI']);}});
  assert.equal(calls.length,0);
  assert.deepEqual(await fonts.list(),{available:true,families:['Segoe UI'],error:''});
  assert.equal(calls.length,1);
  const [executable,args,options]=calls[0];
  assert.match(executable,/^[a-z]:\\.+\\System32\\WindowsPowerShell\\v1\.0\\powershell\.exe$/i);
  assert.deepEqual(args.slice(0,-1),['-NoProfile','-NonInteractive','-WindowStyle','Hidden','-EncodedCommand']);
  const script=Buffer.from(args.at(-1),'base64').toString('utf16le');
  assert.match(script,/PresentationCore/);
  assert.match(script,/\[System\.Windows\.Media\.Fonts\]::SystemFontFamilies/);
  assert.equal(options.windowsHide,true);
  assert.equal(options.timeout,5000);
  assert.equal(options.maxBuffer,1024*1024);
  assert.equal(options.encoding,'utf8');
  assert.equal(options.shell,undefined);
});

test('family names are sorted and unique while quoted and Unicode families survive',async()=>{
  const fonts=service({run:async()=>output(['Zulu','Segoe UI','Arial','Arial','字体','O\'Brien "Display"','serif','Space, Sans'])});
  assert.deepEqual(await fonts.list(),{available:true,families:['Arial','O\'Brien "Display"','Segoe UI','Space, Sans','Zulu','serif','字体'],error:''});
});

test('malformed records cannot become CSS font or filesystem references',async()=>{
  const fonts=service({run:async()=>output([null,42,{},'', '   ','A\nB','A\u0000B','A\u202eB','C:\\private\\font.ttf','../font','file:font','#Font','A'.repeat(161),'Segoe UI'])});
  assert.deepEqual(await fonts.list(),{available:true,families:['Segoe UI'],error:''});
});

test('oversized, invalid, non-array and wholly invalid output fail clearly',async()=>{
  for(const stdout of ['invalid','{}','"Arial"','[]',JSON.stringify([null]),JSON.stringify(Array(8193).fill('Arial')),' '.repeat(1024*1024+1)]){
    const result=await service({run:async()=>({stdout,stderr:''})}).list();
    assert.equal(result.available,false);
    assert.deepEqual(result.families,[]);
    assert.match(result.error,/font.*(invalid|empty|large|limit|read)/i);
  }
});

test('cached and simultaneous requests share work, refresh bypasses cache, results cannot mutate it',async()=>{
  let calls=0,clock=100,resolve;
  const fonts=service({now:()=>clock,run:()=>{calls++;return new Promise(done=>resolve=done);}});
  const first=fonts.list(),second=fonts.list(),refresh=fonts.list({refresh:true});
  assert.equal(calls,1);
  resolve(output(['Segoe UI']));
  const results=await Promise.all([first,second,refresh]);
  results[0].families.push('Injected');
  assert.deepEqual((await fonts.list()).families,['Segoe UI']);
  assert.equal(calls,1);
  const refreshed=fonts.list({refresh:true});
  assert.equal(calls,2);
  resolve(output(['Arial']));
  assert.deepEqual((await refreshed).families,['Arial']);
  clock+=5*60*1000+1;
  const expired=fonts.list();
  assert.equal(calls,3);
  resolve(output(['Verdana']));
  assert.deepEqual((await expired).families,['Verdana']);
});

test('process failures are safe, cached briefly, and refresh can retry immediately',async()=>{
  let calls=0,clock=100;
  const fonts=service({now:()=>clock,run:async()=>{calls++;if(calls===1)throw Object.assign(new Error('sensitive process stderr'),{killed:true,code:'ETIMEDOUT'});return output(['Arial']);}});
  const failed=await fonts.list();
  assert.equal(failed.available,false);
  assert.deepEqual(failed.families,[]);
  assert.match(failed.error,/font.*(time|unavailable|read)/i);
  assert.doesNotMatch(failed.error,/sensitive/);
  await fonts.list();assert.equal(calls,1);
  assert.deepEqual((await fonts.list({refresh:true})).families,['Arial']);
  assert.equal(calls,2);
});

test('failure cache expires and unsupported platforms never execute a process',async()=>{
  let calls=0,clock=0;
  const fonts=service({now:()=>clock,run:async()=>{calls++;throw new Error('unavailable');}});
  await fonts.list();clock=6000;await fonts.list();assert.equal(calls,2);
  for(const platform of ['linux','darwin','browser']) {
    const result=await service({platform,run:async()=>{throw new Error('must not execute');}}).list({refresh:true});
    assert.equal(result.available,false);assert.deepEqual(result.families,[]);assert.match(result.error,/Windows/i);
  }
});

test('real Windows WPF enumeration includes the installed Segoe UI family',{skip:process.platform!=='win32'},async()=>{
  const result=await service().list({refresh:true});
  assert.equal(result.available,true,result.error);
  assert.equal(result.error,'');
  assert.ok(result.families.includes('Segoe UI'));
  for(const alias of ['Global Monospace','Global Sans Serif','Global Serif','Global User Interface'])assert.ok(!result.families.includes(alias),'WPF composite aliases are not physical Windows fonts');
});
