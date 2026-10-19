import {detail} from "../../../../../../lib/store.js";
import {json} from "../../../../../../lib/http.js";
export const runtime="nodejs";
export async function GET(request,{params}){try{const app=await detail((await params).id);return app?json({protocol:1,app}):json({error:"Not found"},404);}catch{return json({error:"Catalog unavailable"},503);}}
