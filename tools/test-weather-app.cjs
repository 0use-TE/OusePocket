const assert=require("node:assert/strict"),fs=require("node:fs"),vm=require("node:vm");
const place={name:"湖州",lat:30.87,lon:120.09};
const cached={version:3,mode:"auto",place,snapshot:{name:"湖州",temp:20,humidity:96,wind:5,desc:"多云"}};
const success={ok:true,status:200,body:JSON.stringify({current:{temperature_2m:22.4,relative_humidity_2m:70,wind_speed_10m:8,weather_code:1}})};
const flush=async()=>{for(let index=0;index<5;++index)await Promise.resolve();};
function load(value,deferred=false){
    const texts={},enabled={},shown=new Set(),handlers={},requests=[],writes=[],cancelled=[];
    let restore;
    const storage=new Promise(resolve=>{restore=resolve;});if(!deferred)restore(value);
    const api={ui:{setText:(id,text)=>{texts[id]=text;},getText:id=>texts[id]||"",show:id=>shown.add(id),hide:id=>shown.delete(id),setEnabled:(id,state)=>{enabled[id]=state;},on:(id,event,fn)=>{handlers[id]=fn;}},
        clock:{get:()=>({utcMs:1792371600000})},storage:{get:()=>storage,set:(key,data)=>{writes.push(JSON.parse(JSON.stringify(data)));return Promise.resolve();}},
        timers:{setInterval(){}},tasks:{cancel:id=>cancelled.push(id)},net:{request:options=>{
            let resolve,reject;const promise=new Promise((yes,no)=>{resolve=yes;reject=no;});promise.taskId=requests.length+1;requests.push({options,resolve,reject});return promise;
        }}};
    const context=vm.createContext({Ouse:api});vm.runInContext(fs.readFileSync("apps/weather/app.js","utf8"),context);
    return {texts,enabled,shown,requests,writes,cancelled,restore,click:id=>handlers[id](),close:()=>context.app.onClose()};
}
(async()=>{
    {
        const app=load(cached);await flush();assert.equal(app.requests.length,1);assert(app.requests[0].options.url.includes("api.open-meteo.com/v1/forecast"));assert.equal(app.requests[0].options.timeoutMs,20000);
        app.requests[0].resolve(success);await flush();assert.equal(app.texts.temp,"22°");assert.equal(app.texts.status,"");assert.equal(app.writes.at(-1).place.name,"湖州");
        app.click("refresh");assert.equal(app.requests.length,2);assert(app.requests[1].options.url.includes("latitude=30.87"));
        app.requests[1].reject(new Error("ESP_ERR_TIMEOUT"));await flush();assert.equal(app.texts.temp,"22°");assert.equal(app.texts.status,"天气请求超时，请重试");assert(app.enabled.refresh);
    }
    {
        const app=load({...cached,mode:"manual"});await flush();app.requests[0].resolve(success);await flush();
        app.click("auto");assert(app.requests[1].options.url.includes("geojs.io"));assert.equal(app.requests[1].options.timeoutMs,8000);
        app.requests[1].reject(new Error("ESP_ERR_TIMEOUT"));await flush();assert.equal(app.requests.length,3);assert(app.requests[2].options.url.includes("latitude=30.87"));
        app.requests[2].resolve(success);await flush();assert.equal(app.writes.at(-1).mode,"manual");assert(app.texts.status.includes("已更新已有城市"));assert(app.enabled.auto);
    }
    {
        const app=load(null);await flush();app.requests[0].reject(new Error("ESP_ERR_TIMEOUT"));await flush();assert(app.texts.status.includes("请搜索城市"));assert.equal(app.requests.length,1);assert.equal(app.texts.temp,"--°");assert(app.enabled.search);
        app.texts.query="杭州";app.click("search");app.requests[1].resolve({ok:true,body:JSON.stringify({results:[{name:"杭州",latitude:30.27,longitude:120.15}]})});await flush();app.click("hit0");
        assert(app.requests[2].options.url.includes("latitude=30.27"));app.requests[2].resolve(success);await flush();assert.equal(app.writes.at(-1).mode,"manual");
    }
    {
        const app=load(cached);await flush();app.close();const before=JSON.stringify(app.texts);app.requests[0].resolve(success);await flush();assert.equal(JSON.stringify(app.texts),before);assert.deepEqual(app.cancelled,[1]);assert.equal(app.writes.length,0);
    }
    {
        const app=load(cached,true);app.texts.query="杭州";app.click("search");app.restore(cached);await flush();assert.equal(app.requests.length,1);assert(app.requests[0].options.url.includes("geocoding-api"));
    }
    {
        const app=load({version:2,mode:"auto",autoPlace:{name:"坏坐标",lat:300,lon:120}});await flush();assert(app.requests[0].options.url.includes("geojs.io"));
        app.requests[0].resolve({ok:true,body:"null"});await flush();assert(app.texts.status.includes("定位数据无法解析"));assert.equal(app.requests.length,1);
    }
    console.log("PASS weather: cached auto/manual refresh bypasses geolocation, bounded location fallback, preserved cache/preferences, search recovery, friendly timeout, close/late-storage isolation and invalid coordinates");
})().catch(error=>{console.error(error);process.exitCode=1;});
