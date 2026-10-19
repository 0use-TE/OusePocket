import {createHash} from "node:crypto";
import {Script} from "node:vm";
const maximum=524288;
export function crc32(bytes){let crc=0xffffffff;for(const byte of bytes){crc^=byte;for(let bit=0;bit<8;bit++)crc=(crc>>>1)^((crc&1)?0xedb88320:0);}return (crc^0xffffffff)>>>0;}
function check(condition,message){if(!condition)throw new Error(message);}
const decode=bytes=>new TextDecoder("utf-8",{fatal:true}).decode(bytes);
const idValid=id=>typeof id==="string"&&/^user\.[a-z0-9._-]+$/.test(id)&&id.length<48;
function image(bytes){check(bytes.length>=8&&bytes.subarray(0,4).toString()==="OIC1","Invalid icon");const width=bytes.readUInt16LE(4),height=bytes.readUInt16LE(6);check(width>0&&height>0&&width<=96&&height<=96&&bytes.length===8+width*height*4,"Invalid icon dimensions");}
export function unpack(bytes){
 check(Buffer.isBuffer(bytes)&&bytes.length>=22&&bytes.length<=maximum,"BPK must be <=512 KiB");
 const end=bytes.length-22;check(bytes.readUInt32LE(end)===0x06054b50&&bytes.readUInt16LE(end+4)===0&&bytes.readUInt16LE(end+6)===0&&bytes.readUInt16LE(end+20)===0,"Invalid ZIP end");
 const count=bytes.readUInt16LE(end+10),central=bytes.readUInt32LE(end+16);check(count>=4&&count<=20&&bytes.readUInt16LE(end+8)===count&&central<=end&&bytes.readUInt32LE(end+12)===end-central,"Invalid directory");
 const files=new Map();let position=0,directory=central;
 for(let index=0;index<count;index++){
  check(position+30<=central&&directory+46<=end&&bytes.readUInt32LE(position)===0x04034b50&&bytes.readUInt32LE(directory)===0x02014b50,"Invalid ZIP entry");
  const length=bytes.readUInt32LE(position+18),nameLength=bytes.readUInt16LE(position+26),name=decode(bytes.subarray(position+30,position+30+nameLength));
  check(bytes.readUInt16LE(position+4)>0&&bytes.readUInt16LE(position+4)<=20&&(bytes.readUInt16LE(position+6)===0||bytes.readUInt16LE(position+6)===0x800),"Encrypted ZIP not supported");
  check(bytes.readUInt16LE(position+8)===0&&bytes.readUInt16LE(position+28)===0&&bytes.readUInt32LE(position+22)===length&&position+30+nameLength+length<=central,"Only ZIP_STORED supported");
  check(/^[a-zA-Z0-9._/-]{1,95}$/.test(name)&&!name.startsWith("/")&&!name.split("/").some(part=>!part||part==="."||part==="..")&&!files.has(name),"Invalid file path");
  const data=Buffer.from(bytes.subarray(position+30+nameLength,position+30+nameLength+length));
  check(crc32(data)===bytes.readUInt32LE(position+14)&&bytes.readUInt32LE(directory+16)===crc32(data)&&bytes.readUInt32LE(directory+20)===length&&bytes.readUInt32LE(directory+24)===length&&bytes.readUInt32LE(directory+42)===position,"ZIP checksum mismatch");
  check(bytes.readUInt16LE(directory+10)===0&&bytes.readUInt16LE(directory+30)===0&&bytes.readUInt16LE(directory+32)===0&&bytes.readUInt16LE(directory+34)===0&&bytes.readUInt16LE(directory+28)===nameLength&&decode(bytes.subarray(directory+46,directory+46+nameLength))===name,"ZIP directory mismatch");
  check(bytes.readUInt16LE(directory+6)===bytes.readUInt16LE(position+4)&&bytes.readUInt16LE(directory+8)===bytes.readUInt16LE(position+6)&&bytes.readUInt32LE(directory+12)===bytes.readUInt32LE(position+10),"ZIP header mismatch");
  const mode=(bytes.readUInt32LE(directory+38)>>>16)&0o170000;check((mode===0||mode===0o100000)&&!(bytes.readUInt32LE(directory+38)&0x10),"Regular files only");
  files.set(name,data);position+=30+nameLength+length;directory+=46+nameLength;
 }
 check(position===central&&directory===end,"Trailing ZIP bytes");return files;
}
export function zip(files){
 const locals=[],directory=[];let offset=0;
 for(const [name,bytes] of files){const label=Buffer.from(name),crc=crc32(bytes),local=Buffer.alloc(30),entry=Buffer.alloc(46);local.writeUInt32LE(0x04034b50);local.writeUInt16LE(20,4);local.writeUInt32LE(crc,14);local.writeUInt32LE(bytes.length,18);local.writeUInt32LE(bytes.length,22);local.writeUInt16LE(label.length,26);
  entry.writeUInt32LE(0x02014b50);entry.writeUInt16LE(20,4);entry.writeUInt16LE(20,6);entry.writeUInt32LE(crc,16);entry.writeUInt32LE(bytes.length,20);entry.writeUInt32LE(bytes.length,24);entry.writeUInt16LE(label.length,28);entry.writeUInt32LE(offset,42);locals.push(local,label,bytes);directory.push(entry,label);offset+=30+label.length+bytes.length;
 }
 const catalog=Buffer.concat(directory),end=Buffer.alloc(22);end.writeUInt32LE(0x06054b50);end.writeUInt16LE(files.size,8);end.writeUInt16LE(files.size,10);end.writeUInt32LE(catalog.length,12);end.writeUInt32LE(offset,16);return Buffer.concat([...locals,catalog,end]);
}
export function validate(bytes){
 const files=unpack(bytes);for(const name of ["manifest.json","page.json","app.js","icon.bin"])check(files.has(name),"Missing "+name);
 check(files.get("manifest.json").length<=8192,"Manifest too large");const manifest=JSON.parse(decode(files.get("manifest.json")));
 check(manifest.runtime==="ouse-core"&&manifest.api===1&&!Object.hasOwn(manifest,"format")&&idValid(manifest.id),"Invalid manifest identity");
 check(typeof manifest.name==="string"&&Buffer.byteLength(manifest.name)>0&&Buffer.byteLength(manifest.name)<=47&&!manifest.name.includes("\0")&&manifest.icon==="icon.bin","Invalid name/icon");
 const caps=manifest.capabilities||{},allowed=["background","notifications","network","media","alarms","files","sdcard","camera","photos","bluetooth","hid","packages"];
 check(caps&&typeof caps==="object"&&!Array.isArray(caps)&&Object.entries(caps).every(([key,value])=>allowed.includes(key)&&typeof value==="boolean"),"Invalid capabilities");
 const fileTypes=manifest.fileTypes||[];
 check(Array.isArray(fileTypes)&&new Set(fileTypes).size===fileTypes.length&&fileTypes.every(type=>["image/jpeg","audio/wav","video/x-msvideo"].includes(type)&&(type==="audio/wav"?caps.media&&caps.files:caps.photos)),"Invalid fileTypes or missing capabilities");
 check(!fileTypes.length||manifest.minNativeApi>=38,"fileTypes requires native API 38");
 for(const key of ["versionCode","minNativeApi"])if(Object.hasOwn(manifest,key))check(Number.isInteger(manifest[key])&&manifest[key]>0&&manifest[key]<=2147483647,"Invalid "+key);
 if(Object.hasOwn(manifest,"versionName"))check(typeof manifest.versionName==="string"&&Buffer.byteLength(manifest.versionName)>0&&Buffer.byteLength(manifest.versionName)<32&&!manifest.versionName.includes("\0"),"Invalid versionName");
 for(const [key,maximum] of [["description",512],["author",64]])if(Object.hasOwn(manifest,key))check(typeof manifest[key]==="string"&&Buffer.byteLength(manifest[key])>0&&Buffer.byteLength(manifest[key])<=maximum&&!manifest[key].includes("\0"),"Invalid "+key);
 if(Object.hasOwn(manifest,"releasedAt"))check(typeof manifest.releasedAt==="string"&&/^\d{4}-\d{2}-\d{2}$/.test(manifest.releasedAt)&&Number(manifest.releasedAt.slice(0,4))>0&&Number.isFinite(Date.parse(manifest.releasedAt))&&new Date(manifest.releasedAt).toISOString().slice(0,10)===manifest.releasedAt,"Invalid releasedAt");
 image(files.get("icon.bin"));const assets=manifest.assets||[];check(Array.isArray(assets)&&assets.length<=16&&files.size===4+assets.length,"Invalid assets");const ids=new Set(),paths=new Set();
 let pixels=0,dataBytes=0;
 for(const asset of assets){check(asset&&Object.keys(asset).length===3&&/^[A-Za-z][A-Za-z0-9_-]{0,30}$/.test(asset.id)&&["image","text","binary"].includes(asset.type)&&typeof asset.path==="string"&&asset.path.startsWith("assets/")&&files.has(asset.path)&&!ids.has(asset.id)&&!paths.has(asset.path),"Invalid asset");ids.add(asset.id);paths.add(asset.path);const data=files.get(asset.path);if(asset.type==="image"){check(data.length>=8&&data.subarray(0,4).toString()==="OIC1","Invalid image");const width=data.readUInt16LE(4),height=data.readUInt16LE(6);check(width>0&&height>0&&width<=512&&height<=512&&data.length===8+width*height*4,"Invalid image dimensions");pixels+=data.length-8;}else{dataBytes+=data.length;if(asset.type==="text")check(!decode(data).includes("\0"),"Invalid text asset");}}
 check(pixels<=131072&&dataBytes<=524288,"Asset budget exceeded");
 if(manifest.names!==undefined)check(manifest.names&&!Array.isArray(manifest.names)&&typeof manifest.names==="object"&&Object.entries(manifest.names).every(([key,value])=>["zh_CN","en"].includes(key)&&typeof value==="string"&&Buffer.byteLength(value)>0&&Buffer.byteLength(value)<48&&!value.includes("\0")),"Invalid localized name");
 check(files.get("page.json").length>0&&files.get("page.json").length<=32768&&files.get("app.js").length>0&&files.get("app.js").length<=32768,"Sources too large");const page=decode(files.get("page.json")),script=decode(files.get("app.js"));check(!page.includes("\0")&&!script.includes("\0"),"NUL in source");JSON.parse(page);new Script(script,{filename:"app.js"});
 return {files,manifest};
}
export function release(bytes,versionCode,versionName,minNativeApi,fields={}){
 const {files,manifest}=validate(bytes);check(Number.isInteger(versionCode)&&versionCode>0&&versionCode<=2147483647,"Invalid versionCode");check(Number.isInteger(minNativeApi)&&minNativeApi>0&&minNativeApi<=2147483647,"Invalid minNativeApi");check(typeof versionName==="string"&&Buffer.byteLength(versionName)>0&&Buffer.byteLength(versionName)<32&&!versionName.includes("\0"),"Invalid versionName");
 manifest.versionCode=versionCode;manifest.versionName=versionName;manifest.minNativeApi=minNativeApi;for(const key of ["description","author","releasedAt"])if(fields[key]!==undefined)manifest[key]=fields[key];files.set("manifest.json",Buffer.from(JSON.stringify(manifest)));const output=zip(files);validate(output);
 return {bytes:output,manifest,sha256:createHash("sha256").update(output).digest("hex"),icon:files.get("icon.bin")};
}
