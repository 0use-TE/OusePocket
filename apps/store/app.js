"use strict";
const home="http://store.0usecraft.cn:8888/",categories=["all","tools","life","media","games","development","other"];
let base="",category=0,items=[],start=0,next=null,selected=null,busy=false,requestId=0,iconRequest=0,installId=0,closed=false,screen="catalog",generation=0,iconGeneration=0,installed=Object.create(null),seenPhase="",settle=0,failed=false;
const iconCache=new Map(),slotIcons=Array(12).fill(""),placeholder=Ouse.assets.readBytes("placeholder");
const ICON_CACHE_LIMIT=24,ICON_CACHE_BYTES=224*1024;let iconCacheBytes=0;
function iconKey(app,root=base){return app&&/^files\/[a-f0-9]{64}\.bin$/.test(app.icon||"")?root+app.icon:"";}
function cachedIcon(key){const data=iconCache.get(key);if(data){iconCache.delete(key);iconCache.set(key,data);}return data;}
function cacheIcon(key,body){
 if(!(body instanceof ArrayBuffer)||body.byteLength<12)return null;
 const bytes=new Uint8Array(body),width=bytes[4]|bytes[5]<<8,height=bytes[6]|bytes[7]<<8;
 if(bytes[0]!==79||bytes[1]!==73||bytes[2]!==67||bytes[3]!==49||!width||!height||width>96||height>96||body.byteLength!==8+width*height*4)return null;
 let data=body;if(width!==48||height!==48){data=new ArrayBuffer(8+48*48*4);new Uint8Array(data).set([79,73,67,49,48,0,48,0]);const input=new Uint32Array(body,8),output=new Uint32Array(data,8);for(let y=0;y<48;++y)for(let x=0;x<48;++x)output[y*48+x]=input[Math.floor(y*height/48)*width+Math.floor(x*width/48)];}
 const previous=iconCache.get(key);if(previous)iconCacheBytes-=previous.byteLength;iconCache.delete(key);iconCache.set(key,data);iconCacheBytes+=data.byteLength;
 while(iconCache.size>ICON_CACHE_LIMIT||iconCacheBytes>ICON_CACHE_BYTES){const oldest=iconCache.keys().next().value;iconCacheBytes-=iconCache.get(oldest).byteLength;iconCache.delete(oldest);}return data;
}
function applyIcon(index,key,data){if(Ouse.ui.setImageData("icon"+index,data)!==false)slotIcons[index]=key;}
function syncIcons(){for(let index=0;index<12;++index){const app=items[index];if(!app){slotIcons[index]="";continue;}const key=iconKey(app);if(key&&slotIcons[index]===key)continue;const data=key&&cachedIcon(key);applyIcon(index,data?key:"",data||placeholder);}}
function short(value,limit=230){let output="",bytes=0;for(const letter of String(value)){const point=letter.codePointAt(0),length=point<128?1:point<2048?2:point<65536?3:4;if(bytes+length>limit)return output+"…";output+=letter;bytes+=length;}return output;}
function text(id,value){Ouse.ui.setText(id,short(value));}
function show(id,value){if(value)Ouse.ui.show(id);else Ouse.ui.hide(id);}
function status(value){text("status",value);show("status",!!value);}
function view(value){screen=value;show("catalog",value==="catalog");show("categoryRow",value==="catalog");show("settings",value==="settings");show("detail",value==="detail");Ouse.navigation.setBackEnabled(value!=="catalog");}
function source(value){value=String(value).trim();if(value.length>240||!/^https?:\/\/[A-Za-z0-9.\[\]:-]+(?:\/[A-Za-z0-9._~!$&'()*+,;=:@%/-]*)?$/.test(value)||value.includes("@")||/%(?:00|0a|0d)/i.test(value)||value.split("/").some(part=>part==="."||part===".."))return "";return value.replace(/\/+$/,"")+"/";}
function asset(value){return typeof value==="string"&&/^files\/[a-f0-9]{64}\.bpk$/.test(value)?base+value:"";}
function valid(app){return !!app&&/^user\.[a-z0-9._-]{1,42}$/.test(app.id)&&typeof app.name==="string"&&app.name.length>0&&app.name.length<=47&&typeof app.versionName==="string"&&app.versionName.length<32&&Number.isInteger(app.versionCode)&&app.versionCode>0&&app.versionCode<=2147483647&&Number.isInteger(app.minNativeApi)&&app.minNativeApi>0&&Number.isInteger(app.size)&&app.size>0&&app.size<=524288&&/^[a-f0-9]{64}$/.test(app.sha256)&&asset(app.url)===base+"files/"+app.sha256+".bpk";}
function inventory(){installed=Object.create(null);for(let offset=0;offset<4096;offset+=12){const page=Ouse.packages.list(offset,12);if(!page||!Array.isArray(page.items))break;for(const app of page.items)installed[app.id]=app;if(!page.more)break;}}
function render(){for(let row=0;row<6;++row)show("row"+row,!!items[row*2]);for(let index=0;index<12;++index){const app=items[index];show("card"+index,!!app);if(index%2===1)show("empty"+index,!!items[index-1]&&!app);if(app){const local=installed[app.id];text("app"+index,short(app.name,45));text("state"+index,local?(local.versionCode<app.versionCode?"可更新":"打开"):"获取");}}Ouse.ui.setEnabled("refresh",!busy&&!installId);}
function cancelNetwork(){++generation;++iconGeneration;if(requestId)Ouse.tasks.cancel(requestId);if(iconRequest)Ouse.tasks.cancel(iconRequest);requestId=iconRequest=0;busy=false;}
async function images(){const revision=++iconGeneration,sourceBase=base;for(let index=0;index<items.length&&!closed&&revision===iconGeneration;++index){const app=items[index],key=iconKey(app,sourceBase);if(!key||slotIcons[index]===key)continue;try{let data=cachedIcon(key);if(!data){const pending=Ouse.net.request({url:key,method:"GET",timeoutMs:6000,responseType:"bytes"});iconRequest=pending.taskId||0;const result=await pending;if(closed)return;if(result&&result.ok)data=cacheIcon(key,result.body);}if(revision!==iconGeneration)return;iconRequest=0;if(data&&items[index]===app&&base===sourceBase)applyIcon(index,key,data);}catch(error){if(revision!==iconGeneration)return;iconRequest=0;}}}
function updateActions(){if(!selected)return;const local=installed[selected.id],current=!!local&&local.versionCode>=selected.versionCode; text("install",current?"打开":local?"更新":"安装");show("open",!!local&&!current);Ouse.ui.setEnabled("open",!installId);Ouse.ui.setEnabled("install",!installId&&(current||selected.minNativeApi<=Ouse.system.info().nativeApi));}
function details(app,index){selected=app;text("name",app.name);text("release",app.versionName+" · "+(app.size/1024).toFixed(1)+" KB");text("byline",(app.author||"未署名")+(app.releasedAt?" · "+app.releasedAt:""));text("description",app.description||"");Ouse.ui.setImage("detailIcon","slot"+index);const local=installed[app.id],nativeApi=Ouse.system.info().nativeApi;updateActions();status((!local||local.versionCode<app.versionCode)&&app.minNativeApi>nativeApi?"需要更新系统固件":"");view("detail");}
async function json(path){const pending=Ouse.net.request({url:base+path,method:"GET",headers:{Accept:"application/json"},timeoutMs:20000,responseType:"text"});requestId=pending.taskId||0;const response=await pending;if(!response||!response.ok)throw Error("请求失败"+(response?" HTTP "+response.status:""));const result=JSON.parse(response.body);if(result.protocol!==1)throw Error("商店协议不兼容");return result;}
async function load(offset=0,action="reset",verify=false){
 if(busy||installId||!base||screen!=="catalog")return;busy=true;failed=false;const revision=++generation;++iconGeneration;if(iconRequest)Ouse.tasks.cancel(iconRequest);iconRequest=0;const sourceBase=base;status(action==="reset"?"正在连接…":"");render();
 try{if(verify){const info=await json("api/store/v1/info");if(closed||revision!==generation||sourceBase!==base)return;if(typeof info.name!=="string")throw Error("商店信息无效");}
  const result=await json("api/store/v1/apps?limit=6&cursor="+offset+"&category="+categories[category]);if(closed||revision!==generation||screen!=="catalog")return;
  if(!Array.isArray(result.items)||result.items.length>6||!result.items.every(valid)||(result.nextCursor!==null&&(!/^\d{1,8}$/.test(result.nextCursor)||Number(result.nextCursor)<=offset)))throw Error("商店索引无效");
  const scroll=Ouse.ui.getScroll("catalog");if(action==="append"){items=items.concat(result.items);next=result.nextCursor;if(items.length>12){items.splice(0,6);start+=6;Ouse.ui.scrollTo("catalog",Math.max(0,scroll.y-3*144));}}
  else if(action==="prepend"){items=result.items.concat(items).slice(0,12);start=offset;next=String(start+items.length);Ouse.ui.scrollTo("catalog",Math.max(0,scroll.y)+Math.ceil(result.items.length/2)*144);}
  else{items=result.items;start=offset;next=result.nextCursor;Ouse.ui.scrollTo("catalog",0);}
  syncIcons();settle=3;inventory();status(items.length?"":"暂无应用");render();images();
 }catch(error){if(!closed&&revision===generation){failed=true;status(String(error.message||error));}}
 finally{if(revision===generation){busy=false;requestId=0;if(!closed)render();}}
}
function configure(){cancelNetwork();Ouse.ui.setText("address",base);view("settings");show("cancel",false);status("");}
function back(){if(screen!=="catalog"){view("catalog");status("");if(base&&!items.length)load(0,"reset",true);else if(base&&!busy&&!installId){syncIcons();images();}}}
function tick(){
 if(installId){const state=Ouse.packages.state();if(!state||state.taskId!==installId){installId=0;status("安装状态已变化");show("cancel",false);return;}const percent=state.total?Math.min(100,Math.floor(state.received*100/state.total)):0,key=state.phase+":"+percent;if(key!==seenPhase){seenPhase=key;const phases={queued:"等待安装",downloading:"正在下载 "+percent+"%",confirmation:"请确认系统安装提示",installing:"正在安装",succeeded:"安装完成",cancelled:"已取消安装",failed:"安装失败："+state.error};if(screen!=="settings")status(phases[state.phase]||state.phase);Ouse.ui.setEnabled("cancel",state.busy);show("cancel",state.busy&&screen!=="settings");if(!state.busy){installId=0;inventory();render();updateActions();}}return;}
 if(screen!=="catalog"||busy||failed||!base)return;if(settle){--settle;return;}const scroll=Ouse.ui.getScroll("catalog");if(!scroll.height||scroll.pending)return;if(start>0&&scroll.y<60)load(Math.max(0,start-6),"prepend");else if(next!==null&&scroll.bottom<150)load(Number(next),"append");
}
Ouse.navigation.onBack(back);Ouse.ui.on("back","click",back);Ouse.ui.on("configure","click",configure);
Ouse.ui.on("connect","click",()=>{const value=source(Ouse.ui.getText("address"));if(!value){status("请输入 HTTP 或 HTTPS 商店地址");return;}cancelNetwork();base=value;items=[];next=null;start=0;Ouse.storage.set("source",base);view("catalog");load(0,"reset",true);});
Ouse.ui.on("refresh","click",()=>load(0,"reset",true));Ouse.ui.on("category","change",()=>{const value=Ouse.ui.getValue("category");if(!Number.isInteger(value)||value<0||value>=categories.length||value===category)return;cancelNetwork();category=value;items=[];next=null;start=0;render();load();});
for(let index=0;index<12;++index)Ouse.ui.on("card"+index,"click",()=>{if(items[index])details(items[index],index);});
Ouse.ui.on("install","click",()=>{if(!selected||installId||busy)return;const local=installed[selected.id];if(local&&local.versionCode>=selected.versionCode){Ouse.openApp(selected.id);return;}++iconGeneration;if(iconRequest)Ouse.tasks.cancel(iconRequest);iconRequest=0;const id=Ouse.packages.install({url:asset(selected.url),id:selected.id,size:selected.size,sha256:selected.sha256,versionCode:selected.versionCode});if(!id){status("安装服务繁忙或内存不足");return;}installId=id;seenPhase="";Ouse.ui.setEnabled("install",false);tick();});
Ouse.ui.on("cancel","click",()=>{if(installId)Ouse.tasks.cancel(installId);});Ouse.ui.on("open","click",()=>{if(selected)Ouse.openApp(selected.id);});Ouse.timers.setInterval(tick,350);
globalThis.app={onResume(){if(base&&!busy&&!installId){inventory();render();updateActions();syncIcons();images();}tick();},onClose(){closed=true;cancelNetwork();iconCache.clear();iconCacheBytes=0;if(installId)Ouse.tasks.cancel(installId);}};
view("catalog");render();Ouse.storage.get("source").then(value=>{if(closed||screen==="settings"||base)return;base=source(typeof value==="string"?value:"")||home;load(0,"reset",true);}).catch(()=>{if(!closed&&screen!=="settings"&&!base){base=home;load(0,"reset",true);}});
