"use strict";
const pad=n=>String(n).padStart(2,"0"),key=d=>d.getFullYear()+pad(d.getMonth()+1)+pad(d.getDate());
let now=new Date(),year=now.getFullYear(),month=now.getMonth(),selected=new Date(year,month,now.getDate()),note="",load=0,closed=false;
function frame(id,x,y,width,height){return {id,x:Math.floor(x),y:Math.floor(y),width:Math.max(1,Math.floor(width)),height:Math.max(1,Math.floor(height))};}
function layout(){
 const display=Ouse.display.get();if(display.keyboardVisible)return;const w=Math.floor(display.contentWidthDp),h=Math.floor(display.contentHeightDp),land=w>h;
 const grid=land?Math.floor(w*.67):w,cell=Math.floor((grid-16)/7),row=Math.max(24,Math.floor((h-(land?68:144))/6)),a=[];
 a.push(frame("previous",8,6,36,36),frame("next",grid-96,6,36,36),frame("today",grid-56,6,48,36),frame("month",48,10,grid-148,30));
 for(let i=0;i<7;i++)a.push(frame("week"+i,8+i*cell,46,cell,20));
 for(let i=0;i<42;i++)a.push(frame("day"+i,8+(i%7)*cell,68+Math.floor(i/7)*row,cell-2,row-2));
 const x=land?grid+8:8,y=land?16:68+6*row+6,width=land?w-grid-16:w-16;
 a.push(frame("selected",x,y,width,26),frame("summary",x,y+30,land?width:width-80,land?Math.max(30,h-112):24),frame("edit",land?x:w-80,land?h-52:y+27,72,40));
 Ouse.ui.layout(a);
}
function loadNote(){
 const token=++load;note="";Ouse.ui.setText("summary","");
 Ouse.storage.get("day."+key(selected)).then(value=>{if(closed||token!==load)return;note=typeof value==="string"?value:"";Ouse.ui.setText("summary",note||"暂无记事");}).catch(()=>{if(!closed&&token===load)Ouse.ui.setText("summary","读取失败");});
}
function render(){
 Ouse.ui.setText("month",year+" / "+pad(month+1));const first=new Date(year,month,1).getDay(),days=new Date(year,month+1,0).getDate(),today=new Date();
 for(let i=0;i<42;i++){
  const day=i-first+1,valid=day>0&&day<=days,id="day"+i;Ouse.ui.setEnabled(id,valid);Ouse.ui.setText(id,valid?String(day):"");
  const chosen=valid&&selected.getFullYear()===year&&selected.getMonth()===month&&selected.getDate()===day;
  const current=valid&&today.getFullYear()===year&&today.getMonth()===month&&today.getDate()===day;
  Ouse.ui.setStyle(id,{backgroundColor:chosen?"$accent":"$surface",textColor:chosen?"#ffffff":current?"$accent":"$text",borderWidth:0,radius:8});
 }
 Ouse.ui.setText("selected",selected.getFullYear()+"-"+pad(selected.getMonth()+1)+"-"+pad(selected.getDate()));layout();
}
function shift(delta){const d=new Date(year,month+delta,1);if(d.getFullYear()<1970||d.getFullYear()>2099)return;year=d.getFullYear();month=d.getMonth();render();}
Ouse.ui.on("previous","click",()=>shift(-1));Ouse.ui.on("next","click",()=>shift(1));
Ouse.ui.on("today","click",()=>{const d=new Date();year=d.getFullYear();month=d.getMonth();selected=new Date(year,month,d.getDate());render();loadNote();});
for(let i=0;i<42;i++)Ouse.ui.on("day"+i,"click",()=>{const day=i-new Date(year,month,1).getDay()+1;if(day<1||day>new Date(year,month+1,0).getDate())return;selected=new Date(year,month,day);render();loadNote();});
Ouse.ui.on("edit","click",()=>{Ouse.ui.setText("note",note);Ouse.ui.show("editor");});
Ouse.ui.on("cancel","click",()=>Ouse.ui.hide("editor"));
Ouse.ui.on("save","click",()=>{
 const value=Ouse.ui.getText("note"),date=key(selected);Ouse.ui.setEnabled("save",false);
 const action=value?Ouse.storage.set("day."+date,value):Ouse.storage.remove("day."+date);
 action.then(()=>{if(closed)return;Ouse.ui.hide("editor");loadNote();}).catch(()=>{if(!closed)Ouse.ui.setText("summary","保存失败，请重试");}).finally(()=>{if(!closed)Ouse.ui.setEnabled("save",true);});
});
Ouse.display.onChange(layout);globalThis.app={onResume(){render();loadNote();},onClose(){closed=true;++load;}};render();loadNote();
