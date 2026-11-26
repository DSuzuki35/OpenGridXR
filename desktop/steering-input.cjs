'use strict';
const {STEERING_AXES,DEVICE_ID}=require('./input-host.cjs');

function normalizeSettings(value={}) {
  return {
    source:value?.source==='device'?'device':'telemetry',
    deviceId:typeof value?.deviceId==='string' && DEVICE_ID.test(value.deviceId)?value.deviceId:'',
    axis:STEERING_AXES.includes(value?.axis)?value.axis:'x',
    lockDegrees:Number.isFinite(value?.lockDegrees)?Math.round(Math.max(90,Math.min(2520,value.lockDegrees))):900,
    invert:value?.invert===true,
  };
}
function steeringSettingsToken(value) {
  const s=normalizeSettings(value);
  const text=JSON.stringify([s.source,s.deviceId,s.axis,s.lockDegrees,s.invert]);
  let hash=0x811c9dc5;
  for(let i=0;i<text.length;++i) hash=Math.imul(hash^text.charCodeAt(i),0x01000193)>>>0;
  return hash || 1;
}
function createSteeringInputController({createHost}={}) {
  if(typeof createHost!=='function') throw new TypeError('A steering input host factory is required.');
  let settings=normalizeSettings(),active=false,arturaDials=false,closed=false,leaseUntil=0,leaseTimer=null,lastActive=null;
  let hostState={available:false,devices:[],error:''};
  const samples=new Map();
  const host=createHost({onAxes(report) {
    if(closed || !hostState.available || !hostState.devices.some(d=>d.id===report.deviceId)) return;
    if(!Object.keys(report.axes).length) samples.delete(report.deviceId);
    else samples.set(report.deviceId,{axes:{...report.axes},sampledAt:report.sampledAt});
  },onStatus(state) {
    hostState=state;
    for(const id of samples.keys()) if(!state.available || !state.devices.some(d=>d.id===id)) samples.delete(id);
  }});
  function synchronize() {
    if(closed) return;
    const enabled=active && settings.source==='device' && !!settings.deviceId;
    const discover=leaseUntil>Date.now();
    host.configureSteering({...settings,enabled,discover,token:enabled?steeringSettingsToken(settings):0});
    const running=enabled || discover || arturaDials;
    if(running!==lastActive) {lastActive=running;host.setActive(running);}
    if(!running) samples.clear();
  }
  function releaseDiscovery() {
    leaseUntil=0;clearTimeout(leaseTimer);leaseTimer=null;synchronize();
  }
  return {
    configure(value,isActive,dials=false) {
      if(closed) return;
      if(typeof isActive!=='boolean') throw new TypeError('Steering activation must be a boolean.');
      if(typeof dials!=='boolean') throw new TypeError('Artura dial activation must be a boolean.');
      settings=normalizeSettings(value);active=isActive;arturaDials=dials;synchronize();
    },
    status({discover=false}={}) {
      if(discover && !closed) {
        leaseUntil=Date.now()+30000;clearTimeout(leaseTimer);
        leaseTimer=setTimeout(releaseDiscovery,30000);leaseTimer.unref?.();synchronize();
      }
      const now=Date.now(),current={};
      for(const [id,sample] of samples) {
        if(sample.sampledAt>now || now-sample.sampledAt>250) samples.delete(id);
        else current[id]={...sample.axes};
      }
      let error=lastActive?(hostState.error || ''):'';
      if(active && settings.source==='device' && settings.deviceId && hostState.available) {
        const device=hostState.devices.find(d=>d.id===settings.deviceId);
        if(!device) error='Selected steering device is unavailable.';
        else if(!device.axes?.includes(settings.axis)) error='Selected steering axis is unavailable.';
      }
      return {devices:closed?[]:hostState.devices.map(d=>({id:d.id,name:d.name,axes:[...(d.axes || [])]})),
        samples:current,available:!closed && !!hostState.available,error:closed?'':error};
    },
    releaseDiscovery,
    close() {
      if(closed) return;
      active=false;arturaDials=false;releaseDiscovery();closed=true;samples.clear();host.close();
    },
  };
}
module.exports={createSteeringInputController,steeringSettingsToken};
