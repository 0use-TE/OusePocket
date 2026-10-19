export function readPackage(buffer){
 const bytes=new Uint8Array(buffer),view=new DataView(buffer),decoder=new TextDecoder("utf-8",{fatal:true}),files=new Map();if(bytes.length<22||bytes.length>524288)throw Error("请选择 512 KB 以内的 BPK 安装包");let position=0;
 while(position+30<=bytes.length&&view.getUint32(position,true)===0x04034b50){
  const size=view.getUint32(position+18,true),length=view.getUint16(position+26,true),extra=view.getUint16(position+28,true),start=position+30+length+extra;
  if(view.getUint16(position+8,true)!==0||(view.getUint16(position+6,true)&~0x800)||extra||!length||length>95||start+size>bytes.length||files.size>=20)throw Error("BPK 结构无效");
  const name=decoder.decode(bytes.subarray(position+30,position+30+length));if(files.has(name))throw Error("BPK 包含重复文件");files.set(name,bytes.slice(start,start+size));position=start+size;
 }
 if(!files.has("manifest.json")||!files.has("icon.bin")||files.get("manifest.json").length>8192)throw Error("缺少 BPK 清单或图标");
 const manifest=JSON.parse(decoder.decode(files.get("manifest.json")));if(manifest.runtime!=="ouse-core"||manifest.api!==1||typeof manifest.id!=="string"||!/^user\.[a-z0-9._-]{1,42}$/.test(manifest.id))throw Error("不是 OusePocket 应用");
 return {manifest,icon:files.get("icon.bin")};
}
export function iconPixels(bytes){const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);if(bytes.length<12||String.fromCharCode(...bytes.slice(0,4))!=="OIC1")throw Error("图标无效");const width=view.getUint16(4,true),height=view.getUint16(6,true);if(!width||!height||width>96||height>96||bytes.length!==8+width*height*4)throw Error("图标尺寸无效");const pixels=new Uint8ClampedArray(width*height*4);for(let index=0;index<pixels.length;index+=4){pixels[index]=bytes[index+10];pixels[index+1]=bytes[index+9];pixels[index+2]=bytes[index+8];pixels[index+3]=bytes[index+11];}return {width,height,pixels};}
