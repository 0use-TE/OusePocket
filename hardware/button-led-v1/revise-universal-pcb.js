const mm=v=>v/.0254;
await eda.dmt_EditorControl.openDocument('d5000e3a199f0770');
await eda.pcb_Document.clearRouting();
const comps=await eda.pcb_PrimitiveComponent.getAll();
const componentIds=comps.map(c=>c.getState_PrimitiveId());
for(const line of await eda.pcb_PrimitivePolyline.getAll())if(line.getState_Layer()===11)await eda.pcb_PrimitivePolyline.delete(line.getState_PrimitiveId());
for(const arc of await eda.pcb_PrimitiveArc.getAll())if(arc.getState_Layer()===11)await eda.pcb_PrimitiveArc.delete(arc.getState_PrimitiveId());
for(const fill of await eda.pcb_PrimitiveFill.getAll())if(fill.getState_Layer()===12)await eda.pcb_PrimitiveFill.delete(fill.getState_PrimitiveId());
for(const pad of await eda.pcb_PrimitivePad.getAll())if(!componentIds.some(id=>pad.getState_PrimitiveId().startsWith(id)))await eda.pcb_PrimitivePad.delete(pad.getState_PrimitiveId());
const layout={J1:[24,0,90,2],SW1:[0,-22,0,1],R2:[-7,-22,0,1],D1:[0,22,90,1],R1:[0,17,90,1]};
for(const c of comps){const [x,y,rotation,layer]=layout[c.getState_Designator()];await eda.pcb_PrimitiveComponent.modify(c.getState_PrimitiveId(),{x:mm(x),y:mm(y),rotation,layer});}
// Fixed platform: 56x78 mm, R4 corners, camera aperture diameter 22 mm.
const outline=[];
for(const [cx,cy,a] of [[24,35,0],[-24,35,90],[-24,-35,180],[24,-35,270]])for(let k=0;k<=16;k++){
 const t=(a+k*90/16)*Math.PI/180;outline.push(mm(cx+4*Math.cos(t)),mm(cy+4*Math.sin(t)));
}
outline.push(outline[0],outline[1]);
await eda.pcb_PrimitivePolyline.create('',11,eda.pcb_MathPolygon.createPolygon([outline[0],outline[1],'L',...outline.slice(2)]),4,true);
await eda.pcb_PrimitiveFill.create(12,eda.pcb_MathPolygon.createPolygon(['CIRCLE',mm(-5.8),0,mm(11)]),'',0,0,true);
for(const x of [-23,23])for(const y of [-33,33])await eda.pcb_PrimitivePad.create(12,'',mm(x),mm(y),0,['ELLIPSE',mm(2.4),mm(2.4)],'', ['ROUND',mm(2.4)],0,0,0,false,0);
for(const t of await eda.pcb_PrimitiveString.getAll()){
 const s=t.getState_Text();
 if(s==='BTN LED V1')await eda.pcb_PrimitiveString.modify(t.getState_PrimitiveId(),{x:0,y:mm(30),text:'OUSE UNIVERSAL 56x78',fontSize:40});
 if(s==='PRESS')await eda.pcb_PrimitiveString.modify(t.getState_PrimitiveId(),{x:0,y:mm(-28)});
 if(s==='1')await eda.pcb_PrimitiveString.modify(t.getState_PrimitiveId(),{x:mm(24),y:mm(-23)});
}
await eda.pcb_PrimitiveString.create(3,mm(-5.8),mm(13),'CAMERA KEEP CLEAR','default',30,5,5,0,false,0,false,false);
await eda.pcb_Document.save();await eda.pcb_Document.zoomToBoardOutline();
return {parts:comps.length,board:[56,78],camera:[-5.8,0,22],holes:[46,66,2.4]};
