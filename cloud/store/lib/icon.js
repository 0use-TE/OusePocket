import {deflateSync} from "node:zlib";
import {crc32} from "./bpk.js";
function chunk(type,data){const name=Buffer.from(type),size=Buffer.alloc(4),checksum=Buffer.alloc(4);size.writeUInt32BE(data.length);checksum.writeUInt32BE(crc32(Buffer.concat([name,data])));return Buffer.concat([size,name,data,checksum]);}
export function iconPNG(bytes){
 if(bytes.length<12||bytes.subarray(0,4).toString()!=="OIC1")throw Error("Invalid OIC1");const width=bytes.readUInt16LE(4),height=bytes.readUInt16LE(6);
 if(!width||!height||width>96||height>96||bytes.length!==8+width*height*4)throw Error("Invalid icon dimensions");
 const pixels=Buffer.alloc(height*(width*4+1));for(let row=0;row<height;++row)for(let column=0;column<width;++column){const source=8+(row*width+column)*4,target=row*(width*4+1)+1+column*4;pixels[target]=bytes[source+2];pixels[target+1]=bytes[source+1];pixels[target+2]=bytes[source];pixels[target+3]=bytes[source+3];}
 const header=Buffer.alloc(13);header.writeUInt32BE(width);header.writeUInt32BE(height,4);header[8]=8;header[9]=6;return Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),chunk("IHDR",header),chunk("IDAT",deflateSync(pixels)),chunk("IEND",Buffer.alloc(0))]);
}
