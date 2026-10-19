import {mkdir,readFile,open,link,unlink} from "node:fs/promises";
import path from "node:path";
import {createHash,randomUUID} from "node:crypto";
import {release} from "./bpk.js";
import {database,dataDirectory,decodeApp,saveApp} from "./database.js";
export const validId=id=>typeof id==="string"&&/^user\.[a-z0-9._-]{1,42}$/.test(id);
const file=name=>path.join(dataDirectory(),name);
export const categories={tools:"实用工具",life:"生活日常",media:"影音娱乐",games:"游戏",development:"开发工具",other:"其他"};
export async function list({cursor="0",limit="12",q="",category="all",sort="name"}={}){
 if(!/^\d{1,8}$/.test(cursor)||!/^\d{1,2}$/.test(limit)||+limit<1||+limit>12||q.length>64)throw new Error("Invalid pagination");
 if(category!=="all"&&!Object.hasOwn(categories,category)||!["name","newest","size"].includes(sort))throw new Error("Invalid filter");
 const db=await database();try{
  const where="instr(lower(name||' '||id||' '||author||' '||description),lower(?))>0 AND (?='all' OR category=?)",args=[q,category,category],order={newest:"updatedAt DESC,id",size:"size,id",name:"name COLLATE NOCASE,id"}[sort];
  const total=db.prepare("SELECT count(*) AS total FROM apps WHERE "+where).get(...args).total;
  const items=db.prepare("SELECT * FROM apps WHERE "+where+" ORDER BY "+order+" LIMIT ? OFFSET ?").all(...args,Number(limit),Number(cursor)).map(decodeApp);
  const offset=Number(cursor);return {protocol:1,items,total,nextCursor:offset+items.length<total?String(offset+items.length):null};
 }finally{db.close();}
}
export async function detail(id){if(!validId(id))return null;const db=await database();try{return decodeApp(db.prepare("SELECT * FROM apps WHERE id=?").get(id));}finally{db.close();}}
export async function object(name){if(!/^[a-f0-9]{64}\.(bpk|bin)$/.test(name))return null;try{return await readFile(file("objects/"+name));}catch(error){if(error.code==="ENOENT")return null;throw error;}}
async function immutable(name,bytes){const temporary=file("objects/"+randomUUID()+".tmp"),target=file("objects/"+name);let handle;try{handle=await open(temporary,"wx");await handle.writeFile(bytes);await handle.sync();await handle.close();handle=null;try{await link(temporary,target);}catch(error){if(error.code!=="EEXIST")throw error;const stored=await readFile(target);if(!stored.equals(bytes))throw new Error("Object collision");}}finally{if(handle)await handle.close();await unlink(temporary).catch(error=>{if(error.code!=="ENOENT")throw error;});}}
export async function publish(bytes,metadata){
 if(typeof metadata.description!=="string"||!metadata.description.trim()||Buffer.byteLength(metadata.description)>512||metadata.description.includes("\0"))throw new Error("请填写应用介绍，最多 512 字节");
 if(typeof metadata.author!=="string"||!metadata.author.trim()||Buffer.byteLength(metadata.author)>64||metadata.author.includes("\0"))throw new Error("请填写作者，最多 64 字节");
 if(!Object.hasOwn(categories,metadata.category))throw new Error("请选择应用分类");
 if(typeof metadata.releasedAt!=="string"||!/^\d{4}-\d{2}-\d{2}$/.test(metadata.releasedAt)||Number(metadata.releasedAt.slice(0,4))<1||!Number.isFinite(Date.parse(metadata.releasedAt))||new Date(metadata.releasedAt).toISOString().slice(0,10)!==metadata.releasedAt)throw new Error("请选择有效发布日期");
 const packed=release(bytes,metadata.versionCode,metadata.versionName,metadata.minNativeApi,metadata);
 await mkdir(file("objects"),{recursive:true});const iconHash=createHash("sha256").update(packed.icon).digest("hex"),manifest=packed.manifest;
 await immutable(packed.sha256+".bpk",packed.bytes);await immutable(iconHash+".bin",packed.icon);
 const db=await database();let transaction=false;
 try{
  db.exec("BEGIN IMMEDIATE");transaction=true;const previous=db.prepare("SELECT versionCode,publishedAt FROM apps WHERE id=?").get(manifest.id);
  if(previous&&previous.versionCode>=metadata.versionCode)throw new Error("versionCode must increase");
  const now=new Date().toISOString();const app={id:manifest.id,name:manifest.names?.zh_CN||manifest.name,versionCode:manifest.versionCode,versionName:manifest.versionName,minNativeApi:manifest.minNativeApi,size:packed.bytes.length,sha256:packed.sha256,url:"files/"+packed.sha256+".bpk",icon:"files/"+iconHash+".bin",iconPng:"icons/"+iconHash+".png",description:manifest.description,author:manifest.author,releasedAt:manifest.releasedAt,category:metadata.category,capabilities:Object.keys(manifest.capabilities||{}).filter(key=>manifest.capabilities[key]),publishedAt:previous?previous.publishedAt:now,updatedAt:now};
  saveApp(db,app);db.exec("COMMIT");transaction=false;return app;
 }catch(error){if(transaction)db.exec("ROLLBACK");throw error;}finally{db.close();}
}
