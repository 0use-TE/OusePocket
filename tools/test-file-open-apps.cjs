const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
function harness(app){
 const state={ready:true,opened:[],played:[],cancelled:[],callbacks:{},listeners:{},next:10};
 const noop=()=>{},ui=new Proxy({on:(id,event,callback)=>state.callbacks[id]=callback,getScroll:()=>({y:0,height:400,bottom:300}),getText:()=>''},{get:(target,key)=>target[key]||noop});
 let restore;state.restore=new Promise(resolve=>restore=resolve);state.finishRestore=restore;
 const Ouse={ui,keys:{on:(key,callback)=>state.listeners[key]=callback},power:{get:()=>({asleep:false})},camera:{get:()=>({running:true,busy:false,error:"ESP_OK"}),capture:()=>{state.played.push('capture');return ++state.next;},stop:noop},files:{get:()=>({ready:state.ready,present:state.ready,revision:1}),list:()=>[],onOpen:callback=>state.listeners.open=callback,open:path=>{state.opened.push(path);return true;}},
 photos:{get:()=>({entries:[],playing:false}),list:()=>++state.next,open:path=>{state.opened.push(path);return ++state.next;},stop:noop},
 tasks:{onComplete:callback=>state.listeners.task=callback,cancel:id=>state.cancelled.push(id),get:()=>null},
 navigation:{onChange:callback=>state.listeners.navigation=callback,onBack:noop,setBackEnabled:noop,push:page=>state.listeners.navigation?.({page}),back:()=>state.listeners.navigation?.({page:app==='gallery'?'album':'player'})},
 timers:{setInterval:()=>1,clear:noop},storage:{get:()=>state.restore,set:()=>Promise.resolve()},
 media:{get:()=>null,play:request=>{state.played.push(request);return ++state.next;},setInfo:noop,stop:()=>true},background:{begin:()=>true,end:noop,isActive:()=>true},notifications:{onOpen:noop}};
 state.context=vm.createContext({Ouse,console});vm.runInContext(fs.readFileSync(`apps/${app}/app.js`,'utf8'),state.context);return state;
}
(async()=>{
 const gallery=harness('gallery');gallery.listeners.open({path:'/photos/one.jpg'});assert.equal(gallery.cancelled.length,1);gallery.listeners.task({id:11,state:'cancelled'});assert.deepEqual(gallery.opened,['/photos/one.jpg']);gallery.listeners.task({id:12,state:'succeeded'});gallery.listeners.open({path:'/movies/two.avi'});assert.equal(gallery.opened.at(-1),'/movies/two.avi');
 const music=harness('music');music.listeners.open({path:'/Recordings/one.wav'});assert.equal(music.played.length,0);music.finishRestore(null);await new Promise(setImmediate);assert.equal(music.played[0].path,'/Recordings/one.wav');music.listeners.open({path:'/other/two.wav'});assert.equal(music.played.at(-1).path,'/other/two.wav');music.ready=false;const before=music.played.length;music.listeners.open({path:'/missing.wav'});assert.equal(music.played.length,before);
 const camera=harness('camera');assert.equal(typeof camera.listeners.pwr,'function');camera.listeners.pwr();camera.listeners.pwr();assert.equal(camera.played.length,1);const missing=harness('camera');missing.ready=false;missing.listeners.pwr();assert.equal(missing.played.length,0);
 console.log('PASS file-open apps and camera key: cold/warm music delivery, missing SD refusal, gallery cancellation handoff, exact paths and shutter busy/SD guards');
})().catch(error=>{console.error(error);process.exitCode=1;});
