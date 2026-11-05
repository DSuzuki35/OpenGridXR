'use strict';
const SHORTCUT=/^(?:Ctrl\+)?(?:Alt\+)?(?:Shift\+)?F(?:[1-9]|1[0-2])$/;
function createDesktopShortcuts({globalShortcut,editorState,applyAction}) {
  const registrations=new Map();let closed=false,signature='',errors=[],inFlight=false;
  const clear=()=>{for(const key of registrations.keys())globalShortcut.unregister(key);registrations.clear();};
  return {
    update(profile,{enabled=true}={}) {
      const items=enabled&&profile?.spatialOverlays?.enabled?profile.spatialOverlays.items.flatMap(item=>{
        if(!['desktop','menu'].includes(item.kind))return [];
        return [...(item.shortcut?[{id:item.id,shortcut:item.shortcut,action:'enabled'}]:[]),...(item.kind==='menu'&&item.selectShortcut?[{id:item.id,shortcut:item.selectShortcut,action:'leftClick'}]:[])];
      }):[];
      const next=JSON.stringify([profile?.id,items.map(item=>[item.id,item.shortcut,item.action])]);
      if(closed||next===signature)return;
      signature=next;clear();errors=[];
      const counts=new Map();for(const item of items)counts.set(item.shortcut.toLowerCase(),(counts.get(item.shortcut.toLowerCase())||0)+1);
      for(const item of items){
        const key=item.shortcut,record={id:item.id,profileId:profile.id};
        if(!SHORTCUT.test(key)||counts.get(key.toLowerCase())!==1){errors.push({id:item.id,message:'Choose a unique desktop shortcut.'});continue;}
        let registered=false;
        try{registered=globalShortcut.register(key,()=>{
          const editor=editorState();
          if(closed||inFlight||registrations.get(key)!==record||editor.dirty||editor.busy||editor.profileId!==record.profileId)return;
          inFlight=true;
          Promise.resolve().then(()=>{
            const latest=editorState();
            if(closed||registrations.get(key)!==record||latest.dirty||latest.busy||latest.profileId!==record.profileId)return;
            return applyAction(record.profileId,`spatialOverlays.${record.id}.${item.action}`);
          }).catch(error=>{errors=errors.filter(e=>e.id!==record.id);errors.push({id:record.id,message:error.message});}).finally(()=>{inFlight=false;});
        });}catch{}
        if(registered)registrations.set(key,record);else errors.push({id:item.id,message:`Shortcut ${key} is unavailable. Choose another shortcut.`});
      }
    },
    status(){return {errors:structuredClone(errors),registered:registrations.size};},
    close(){if(closed)return;closed=true;clear();},
  };
}
module.exports={createDesktopShortcuts};
