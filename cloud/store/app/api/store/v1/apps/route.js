import {list} from "../../../../../lib/store.js";
import {json} from "../../../../../lib/http.js";
export const runtime="nodejs";
export async function GET(request){try{const query=new URL(request.url).searchParams;return json(await list({cursor:query.get("cursor")||"0",limit:query.get("limit")||"12",q:query.get("q")||"",category:query.get("category")||"all",sort:query.get("sort")||"name"}));}catch{return json({error:"Invalid catalog request"},400);}}
