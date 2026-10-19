import {object} from "../../../lib/store.js";
import {iconPNG} from "../../../lib/icon.js";
export const runtime="nodejs";
export async function GET(request,{params}){const {name}=await params;if(!/^[a-f0-9]{64}\.png$/.test(name))return new Response(null,{status:404});try{const bytes=await object(name.slice(0,-4)+".bin");if(!bytes)return new Response(null,{status:404});return new Response(new Uint8Array(iconPNG(bytes)),{headers:{"Content-Type":"image/png","Cache-Control":"public, max-age=31536000, immutable","X-Content-Type-Options":"nosniff"}});}catch{return new Response(null,{status:422});}}
