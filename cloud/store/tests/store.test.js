import test from "node:test";
import assert from "node:assert/strict";
import {mkdtemp,rm,readFile} from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import {createHash} from "node:crypto";
import {zip,unpack,validate,release} from "../lib/bpk.js";
import {publish,list,detail,object} from "../lib/store.js";
import {authorized,boundedBody} from "../lib/http.js";
import {iconPNG} from "../lib/icon.js";
import {readPackage,iconPixels} from "../lib/preview.js";
import {inflateSync} from "node:zlib";
import {database} from "../lib/database.js";
const icon=Buffer.alloc(12);icon.write("OIC1");icon.writeUInt16LE(1,4);icon.writeUInt16LE(1,6);icon[11]=255;
const original=zip(new Map([["manifest.json",Buffer.from(JSON.stringify({runtime:"ouse-core",api:1,id:"user.test-store",name:"测试应用",icon:"icon.bin"}))],["page.json",Buffer.from(JSON.stringify({api:1,root:{type:"label",id:"hello",text:"Hello"}}))],["app.js",Buffer.from('globalThis.app={};')],["icon.bin",icon]]));
test("stored BPK release preserves payload and validates metadata",()=>{
 const packed=release(original,7,"1.2.0",35);assert.equal(validate(packed.bytes).manifest.versionCode,7);assert.equal(unpack(packed.bytes).get("app.js").toString(),unpack(original).get("app.js").toString());assert.equal(createHash("sha256").update(packed.bytes).digest("hex"),packed.sha256);
 for(const metadata of [[0,"v1",35],[1,"",35],[1,"v1",0],[1,"v1",1.5]])assert.throws(()=>release(original,...metadata));
 const broken=Buffer.from(original);broken[60]^=1;assert.throws(()=>validate(broken));assert.throws(()=>validate(Buffer.concat([original,Buffer.from("x")])));
 const files=unpack(original);files.set("../attack",Buffer.from("x"));assert.throws(()=>validate(zip(files)));
});
test("file handlers preserve declared MIME types and reject unsupported or underprivileged declarations",()=>{
 const files=unpack(original),manifest=JSON.parse(files.get("manifest.json"));
 const build=changes=>{files.set("manifest.json",Buffer.from(JSON.stringify({...manifest,...changes})));return zip(files);};
 const valid={fileTypes:["image/jpeg","video/x-msvideo"],minNativeApi:38,capabilities:{photos:true}};
 const packed=release(build(valid),1,"1.0",38);assert.deepEqual(validate(packed.bytes).manifest.fileTypes,valid.fileTypes);
 for(const changes of [{...valid,minNativeApi:37},{...valid,capabilities:{}},{...valid,fileTypes:["image/png"]},{...valid,fileTypes:["image/jpeg","image/jpeg"]},{fileTypes:["audio/wav"],minNativeApi:38,capabilities:{media:true}}])assert.throws(()=>validate(build(changes)));
 assert.throws(()=>release(build(valid),1,"1.0",37));
});
test("atomic publication, increasing versions, immutable downloads and pagination",async()=>{
 const directory=await mkdtemp(path.join(os.tmpdir(),"ouse-store-test-"));process.env.STORE_DATA_DIR=directory;
 try{const metadata={versionCode:1,versionName:"1.0",minNativeApi:36,description:"测试应用",author:"测试作者",releasedAt:"2026-10-19",category:"tools"};const app=await publish(original,metadata);assert.equal((await detail(app.id)).sha256,app.sha256);assert.equal((await list()).items.length,1);assert.equal((await list({q:"不存在"})).items.length,0);assert.equal(await object("../../secret"),null);assert.equal(createHash("sha256").update(await object(app.sha256+".bpk")).digest("hex"),app.sha256);await assert.rejects(publish(original,metadata),/increase/);assert.equal((await publish(original,{...metadata,versionCode:2})).versionCode,2);assert.ok(await object(app.sha256+".bpk"));await assert.rejects(list({limit:"13"}));await assert.rejects(list({cursor:"-1"}));
  const downloaded=validate(await object(app.sha256+".bpk")).manifest;assert.equal(downloaded.author,metadata.author);assert.equal(downloaded.description,metadata.description);assert.equal(downloaded.releasedAt,metadata.releasedAt);assert.equal((await list({q:"测试作者"})).total,1);assert.equal((await list({category:"games"})).total,0);assert.equal((await detail(app.id)).publishedAt,app.publishedAt);await assert.rejects(list({sort:"unknown"}));await assert.rejects(publish(original,{...metadata,versionCode:3,releasedAt:"2026-02-30"}));await assert.rejects(publish(original,{...metadata,versionCode:3,author:"x".repeat(65)}));
  for(let index=0;index<13;++index){const files=unpack(original),manifest=JSON.parse(files.get("manifest.json"));manifest.id="user.test"+index;files.set("manifest.json",Buffer.from(JSON.stringify(manifest)));await publish(zip(files),metadata);}
  const first=await list();assert.equal(first.items.length,12);assert.equal(first.nextCursor,"12");assert.equal((await list({cursor:first.nextCursor})).items.length,2);
  assert.equal((await readFile(path.join(directory,"store.sqlite"))).subarray(0,15).toString(),"SQLite format 3");const db=await database();try{assert.equal(db.prepare("SELECT count(*) AS count FROM releases").get().count,15);assert.equal(db.prepare("PRAGMA integrity_check").get().integrity_check,"ok");}finally{db.close();}
  assert.equal((await list({q:"' OR 1=1 --"})).total,0);const attempts=await Promise.allSettled([publish(original,{...metadata,versionCode:3}),publish(original,{...metadata,versionCode:3})]);assert.equal(attempts.filter(result=>result.status==="fulfilled").length,1);assert.equal((await detail(app.id)).versionCode,3);
 }finally{delete process.env.STORE_DATA_DIR;await rm(directory,{recursive:true,force:true});}
});
test("BPK metadata preview and PNG preserve BGRA colors and alpha",()=>{
 const packed=release(original,1,"1.0",36,{description:"自动填入的介绍",author:"作者",releasedAt:"2024-02-29"}),buffer=packed.bytes.buffer.slice(packed.bytes.byteOffset,packed.bytes.byteOffset+packed.bytes.byteLength),preview=readPackage(buffer);assert.equal(preview.manifest.author,"作者");assert.equal(preview.manifest.description,"自动填入的介绍");assert.equal(iconPixels(preview.icon).pixels[3],255);
 const colored=Buffer.from(icon);colored.set([10,20,30,40],8);const png=iconPNG(colored);assert.equal(png.subarray(1,4).toString(),"PNG");const length=png.readUInt32BE(33);assert.deepEqual([...inflateSync(png.subarray(41,41+length))],[0,30,20,10,40]);assert.deepEqual([...iconPixels(colored).pixels],[30,20,10,40]);assert.throws(()=>iconPNG(Buffer.alloc(12)));assert.throws(()=>readPackage(new ArrayBuffer(2)));assert.throws(()=>release(original,1,"1.0",36,{releasedAt:"2026-02-29"}));
});
test("admin authentication and bounded streaming body",async()=>{
 process.env.STORE_ADMIN_TOKEN="a".repeat(32);assert.equal(authorized(new Request("http://localhost")),false);assert.equal(authorized(new Request("http://localhost",{headers:{Authorization:"Bearer "+"a".repeat(32)}})),true);delete process.env.STORE_ADMIN_TOKEN;
 assert.equal(authorized(new Request("http://localhost")),false);assert.equal((await boundedBody(new Request("http://localhost",{method:"POST",body:"abc"}),3)).toString(),"abc");await assert.rejects(boundedBody(new Request("http://localhost",{method:"POST",body:"abcdef"}),3),/large/);
});
