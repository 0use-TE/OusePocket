import {object} from "../../../lib/store.js";
export const runtime="nodejs";
export async function GET(request,{params}){try{const {name}=await params,bytes=await object(name);if(!bytes)return new Response(null,{status:404});return new Response(new Uint8Array(bytes),{headers:{"Content-Type":"application/octet-stream","Content-Length":String(bytes.length),"Cache-Control":"public, max-age=31536000, immutable","X-Content-Type-Options":"nosniff"}});}catch{return new Response(null,{status:503});}}
