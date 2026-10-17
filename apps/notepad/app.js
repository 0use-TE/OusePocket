"use strict";
const MAX=12,ROWS=12;
let notes=[],page="home",editing=-1,loaded=false,saving=false,dirty=false;
function visible(id,show){if(show)Ouse.ui.show(id);else Ouse.ui.hide(id);}
function text(id,value){Ouse.ui.setText(id,String(value));}
function now(){const clock=Ouse.clock&&Ouse.clock.get?Ouse.clock.get():null;return clock&&clock.utcMs?clock.utcMs:0;}
function short(value,limit){
 let out="",bytes=0;value=String(value||"");
 for(let i=0;i<value.length;i++){
  const ch=value.charAt(i),code=value.charCodeAt(i);
  const size=code<128?1:code<2048?2:3;
  if(bytes+size>limit)return out+"…";
  out+=ch;bytes+=size;
 }
 return out;
}
function preview(note){
 const title=String(note.title||"").trim()||"无标题";
 const body=String(note.body||"").trim();
 return short(body?title+"\n"+body:title,90);
}
function persist(done){
 dirty=true;if(saving)return;saving=true;dirty=false;
 Ouse.storage.set("notes",{version:1,items:notes}).then(()=>{
  saving=false;if(dirty)persist(done);else if(done)done(true);
 },()=>{saving=false;dirty=false;if(done)done(false);});
}
function paintHome(){
 // Wait for storage before showing the empty hint, otherwise opens flash "暂无".
 const empty=loaded&&!notes.length;
 text("status",empty?"还没有笔记，点右上角新建":"");visible("status",empty);
 for(let row=0;row<ROWS;row++){
  const note=loaded?notes[row]:null;
  if(note){visible("item"+row,true);text("item"+row,preview(note));}
  else visible("item"+row,false);
 }
 Ouse.ui.setEnabled("add",loaded&&notes.length<MAX);
}
function fillEdit(){
 const note=editing>=0&&editing<notes.length?notes[editing]:{title:"",body:""};
 Ouse.ui.setText("name",note.title||"");
 Ouse.ui.setText("body",note.body||"");
 text("editTitle",editing<0?"新建":"编辑");
 text("editStatus","");visible("editStatus",false);
 Ouse.ui.setEnabled("remove",editing>=0);
 Ouse.ui.setEnabled("save",loaded);
}
function openEdit(index){
 editing=index;
 if(page!=="edit")Ouse.navigation.push("edit");
 else fillEdit();
}
function bindPage(){
 if(page==="home"){
  Ouse.ui.on("add","click",()=>{
   if(!loaded)return;
   if(notes.length>=MAX){text("status","最多 "+MAX+" 条笔记");visible("status",true);return;}
   openEdit(-1);
  });
  for(let row=0;row<ROWS;row++)Ouse.ui.on("item"+row,"click",()=>{if(row<notes.length)openEdit(row);});
  paintHome();
  return;
 }
 Ouse.ui.on("back","click",()=>Ouse.navigation.back());
 Ouse.ui.on("save","click",()=>{
  if(!loaded||saving)return;
  const title=Ouse.ui.getText("name").trim().slice(0,48);
  const body=Ouse.ui.getText("body").slice(0,256);
  if(!title&&!body.trim()){text("editStatus","请填写标题或正文");visible("editStatus",true);return;}
  const note={title:title||"无标题",body:body,updated:now()};
  if(editing<0)notes.unshift(note);
  else notes[editing]=note;
  if(notes.length>MAX)notes=notes.slice(0,MAX);
  Ouse.ui.setEnabled("save",false);
  persist(ok=>{
   Ouse.ui.setEnabled("save",true);
   text("editStatus",ok?"已保存":"保存失败");visible("editStatus",true);
   if(ok)Ouse.navigation.back();
  });
 });
 Ouse.ui.on("remove","click",()=>{
  if(editing<0||!loaded||saving)return;
  notes.splice(editing,1);editing=-1;
  Ouse.ui.setEnabled("remove",false);
  persist(ok=>{
   Ouse.ui.setEnabled("remove",true);
   if(ok)Ouse.navigation.back();
   else{text("editStatus","删除失败");visible("editStatus",true);}
  });
 });
 fillEdit();
}
Ouse.navigation.onChange(event=>{page=event.page;bindPage();});
globalThis.app={onResume(){if(page==="home")paintHome();}};
bindPage();
Ouse.storage.get("notes").then(value=>{
 if(value&&value.version===1&&Array.isArray(value.items)){
  notes=value.items.filter(item=>item&&typeof item.title==="string"&&typeof item.body==="string").slice(0,MAX).map(item=>({
   title:String(item.title).slice(0,48),
   body:String(item.body).slice(0,256),
   updated:Number(item.updated)||0
  }));
 }
 loaded=true;if(page==="home")paintHome();else bindPage();
},()=>{loaded=true;if(page==="home")paintHome();else bindPage();});
