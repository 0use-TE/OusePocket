"use strict";
const WMO={
 0:"晴",1:"大部晴朗",2:"少云",3:"多云",
 45:"雾",48:"雾凇",
 51:"小毛毛雨",53:"毛毛雨",55:"大毛毛雨",
 56:"冻毛毛雨",57:"强冻毛毛雨",
 61:"小雨",63:"中雨",65:"大雨",
 66:"冻雨",67:"强冻雨",
 71:"小雪",73:"中雪",75:"大雪",77:"雪粒",
 80:"小阵雨",81:"阵雨",82:"强阵雨",
 85:"小阵雪",86:"强阵雪",
 95:"雷暴",96:"雷暴伴小冰雹",99:"雷暴伴大冰雹"
};
const GEO_IP="https://get.geojs.io/v1/ip/geo.json";
let busy=false,taskId=0,mode="auto",place=null,hits=[],snapshot=null,closed=false,revision=0;
function validPlace(value){return !!value&&typeof value.name==="string"&&!!value.name.trim()&&Number.isFinite(value.lat)&&Math.abs(value.lat)<=90&&Number.isFinite(value.lon)&&Math.abs(value.lon)<=180;}
function text(id,value){Ouse.ui.setText(id,String(value));}
function visible(id,show){if(show)Ouse.ui.show(id);else Ouse.ui.hide(id);}
function nowMs(){const clock=Ouse.clock&&Ouse.clock.get?Ouse.clock.get():null;return clock&&clock.utcMs?clock.utcMs:0;}
function describe(code){return WMO[code]||("天气代码 "+code);}
function enc(value){return encodeURIComponent(String(value));}
function paintStatus(message){
 message=message?String(message):"";
 text("status",message);
 visible("status",!!message);
}
function paintDate(){
 const ms=nowMs();
 if(!ms){text("date","日期不可用");return;}
 const local=ms+8*3600*1000,day=new Date(local);
 const week=["日","一","二","三","四","五","六"][day.getUTCDay()];
 text("date",day.getUTCFullYear()+"年"+(day.getUTCMonth()+1)+"月"+day.getUTCDate()+"日 星期"+week);
}
function placeLabel(){
 if(!place||!place.name)return mode==="auto"?"自动定位":"请搜索城市";
 return mode==="auto"?("自动 · "+place.name):place.name;
}
function paintSnapshot(data){
 text("place",data&&data.name?data.name:placeLabel());
 if(!data){
  text("temp","--°");
  text("desc",mode==="auto"?"尚未定位":"尚未获取");
  text("detail","湿度 -- · 风速 --");
  return;
 }
 text("temp",data.temp+"°");
 text("desc",data.desc);
 text("detail","湿度 "+data.humidity+"% · 风速 "+data.wind+" km/h");
}
function persist(){
 Ouse.storage.set("weather",{version:3,mode:mode,place:place,snapshot:snapshot});
}
function paintHits(){
 const show=hits.length>0;
 visible("hits",show);
 for(let i=0;i<5;i++){
  const item=hits[i];
  if(item){visible("hit"+i,true);text("hit"+i,item.label);}
  else visible("hit"+i,false);
 }
}
function request(url){
 ++revision;
 return Ouse.net.request({url:url,method:"GET",headers:{Accept:"application/json"},timeoutMs:url===GEO_IP?8000:20000,responseType:"text"});
}
function receive(req,success,failure){
 const current=revision;
 req.then(response=>{if(!closed&&revision===current)success(response);},error=>{if(!closed&&revision===current)failure(error);});
}
function networkError(prefix,error){
 const message=error&&error.message?String(error.message):"网络错误";
 return message.indexOf("TIMEOUT")>=0?prefix+"超时，请重试":prefix+"失败："+message;
}
function selectPlace(value,nextMode){
 if(!place||place.lat!==value.lat||place.lon!==value.lon)snapshot=null;
 place=value;mode=nextMode;paintSnapshot(snapshot);persist();
}
function weatherUrl(lat,lon){
 return "https://api.open-meteo.com/v1/forecast?latitude="+lat+"&longitude="+lon+"&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m&timezone=auto";
}
function searchUrl(name){
 return "https://geocoding-api.open-meteo.com/v1/search?name="+enc(name)+"&count=5&language=zh&countryCode=CN";
}
function finishBusy(message){
 busy=false;taskId=0;
 Ouse.ui.setEnabled("search",true);Ouse.ui.setEnabled("auto",true);Ouse.ui.setEnabled("refresh",true);
 if(message!=null)paintStatus(message);
}
function parseWeather(body,label){
 let data=null;
 try{data=JSON.parse(body);}catch(error){return null;}
 if(data&&data.simulated===true)return {mode:mode,name:label||placeLabel(),temp:23,humidity:58,wind:9,desc:"晴",atMs:nowMs(),simulated:true};
 if(!data||!data.current)return null;
 const current=data.current;
 const temp=current.temperature_2m,humidity=current.relative_humidity_2m,wind=current.wind_speed_10m,code=current.weather_code;
 if(typeof temp!=="number"||typeof humidity!=="number"||typeof wind!=="number"||typeof code!=="number")return null;
 return {mode:mode,name:label||placeLabel(),temp:Math.round(temp),humidity:Math.round(humidity),wind:Math.round(wind),desc:describe(code|0),atMs:nowMs()};
}
function fetchWeather(fallback=false){
 if(!validPlace(place)){finishBusy("请先定位或搜索城市");return;}
 paintStatus("正在获取天气…");
 let req=null;
 try{req=request(weatherUrl(place.lat,place.lon));}
 catch(error){finishBusy("无法发起天气请求");return;}
 taskId=req.taskId||0;
 receive(req,response=>{
  if(!response||!response.ok){finishBusy("天气请求失败"+(response?" HTTP "+response.status:""));return;}
  const parsed=parseWeather(response.body||"",placeLabel());
  if(!parsed){finishBusy("天气数据无法解析");return;}
  snapshot=parsed;paintSnapshot(snapshot);persist();finishBusy(fallback?"定位失败，已更新已有城市":"");
 },error=>{finishBusy(networkError("天气请求",error));});
}
function parseGeoIp(body){
 let data=null;
 try{data=JSON.parse(body);}catch(error){return null;}
 if(data&&data.simulated===true)return {name:"杭州",lat:30.27,lon:120.15};
 if(data&&data.status&&data.status!=="success")return null;
 if(!data||(data.lat==null&&data.latitude==null)||(data.lon==null&&data.longitude==null))return null;
 const lat=Number(data.lat!=null?data.lat:data.latitude);
 const lon=Number(data.lon!=null?data.lon:data.longitude);
 if(!Number.isFinite(lat)||Math.abs(lat)>90||!Number.isFinite(lon)||Math.abs(lon)>180)return null;
 return {name:String(data.city||"当前位置"),lat:lat,lon:lon};
}
function parseSearch(body){
 let data=null;
 try{data=JSON.parse(body);}catch(error){return [];}
 if(data&&data.simulated===true)return [{name:"杭州",lat:30.27,lon:120.15,label:"杭州 · 浙江省"}];
 const list=data&&Array.isArray(data.results)?data.results:[];
 const out=[];
 for(let i=0;i<list.length&&out.length<5;i++){
  const item=list[i];
  const lat=Number(item.latitude),lon=Number(item.longitude);
  if(!Number.isFinite(lat)||!Number.isFinite(lon)||!item.name)continue;
  const admin=[item.admin2,item.admin1].filter(Boolean).join(" · ");
  out.push({name:String(item.name),lat:lat,lon:lon,label:admin?String(item.name)+" · "+admin:String(item.name)});
 }
 return out;
}
function locationFailed(message){
 if(validPlace(place)){fetchWeather(true);return;}
 finishBusy(message.replace("，请重试","")+"，请搜索城市");
}
function locateAuto(){
 if(busy)return;
 busy=true;hits=[];paintHits();
 Ouse.ui.setEnabled("search",false);Ouse.ui.setEnabled("auto",false);Ouse.ui.setEnabled("refresh",false);
 paintStatus("正在定位…");paintDate();
 let req=null;
 try{req=request(GEO_IP);}
 catch(error){locationFailed("无法发起定位请求");return;}
 taskId=req.taskId||0;
 receive(req,response=>{
  if(!response||!response.ok){locationFailed("定位失败"+(response?" HTTP "+response.status:""));return;}
  const found=parseGeoIp(response.body||"");
  if(!found){locationFailed("定位数据无法解析");return;}
  selectPlace(found,"auto");fetchWeather();
 },error=>{locationFailed(networkError("定位",error));});
}
function searchCity(){
 if(busy)return;
 const name=Ouse.ui.getText("query").trim();
 if(!name){paintStatus("请输入城市名");return;}
 busy=true;hits=[];paintHits();
 Ouse.ui.setEnabled("search",false);Ouse.ui.setEnabled("auto",false);Ouse.ui.setEnabled("refresh",false);
 paintStatus("正在搜索…");
 let req=null;
 try{req=request(searchUrl(name));}
 catch(error){finishBusy("无法发起搜索");return;}
 taskId=req.taskId||0;
 receive(req,response=>{
  if(!response||!response.ok){finishBusy("搜索失败"+(response?" HTTP "+response.status:""));return;}
  hits=parseSearch(response.body||"");
  paintHits();
  if(!hits.length){finishBusy("未找到中国城市");return;}
  finishBusy("请选择城市");
 },error=>{finishBusy("搜索失败："+(error&&error.message?error.message:"网络错误"));});
}
function pickHit(index){
 if(busy||!hits[index])return;
 selectPlace({name:hits[index].name,lat:hits[index].lat,lon:hits[index].lon},"manual");
 hits=[];paintHits();
 busy=true;Ouse.ui.setEnabled("search",false);Ouse.ui.setEnabled("auto",false);Ouse.ui.setEnabled("refresh",false);
 fetchWeather();
}
function refresh(){
 if(busy)return;
 if(!validPlace(place)){locateAuto();return;}
 busy=true;Ouse.ui.setEnabled("search",false);Ouse.ui.setEnabled("auto",false);Ouse.ui.setEnabled("refresh",false);
 paintDate();fetchWeather();
}
Ouse.ui.on("auto","click",locateAuto);
Ouse.ui.on("search","click",searchCity);
Ouse.ui.on("refresh","click",refresh);
for(let i=0;i<5;i++)Ouse.ui.on("hit"+i,"click",()=>pickHit(i));
paintDate();paintSnapshot(null);paintStatus("");paintHits();
Ouse.storage.get("weather").then(value=>{
 if(closed||revision)return;
 if(value&&(value.version===2||value.version===3)){
  mode=value.mode==="manual"?"manual":"auto";
  if(validPlace(value.place))place=value.place;
  else if(validPlace(value.autoPlace))place=value.autoPlace;
  if(value.snapshot&&typeof value.snapshot.temp==="number")snapshot=value.snapshot;
 }
 paintDate();paintSnapshot(snapshot);paintStatus("");
 if(place)refresh();else locateAuto();
},()=>{if(!closed&&!revision)locateAuto();});
Ouse.timers.setInterval(paintDate,30000);
globalThis.app={onResume(){paintDate();},onClose(){closed=true;++revision;if(taskId)Ouse.tasks.cancel(taskId);}};
