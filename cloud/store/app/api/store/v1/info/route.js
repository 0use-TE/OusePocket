import {json} from "../../../../../lib/http.js";
export const runtime="nodejs";
export function GET(){return json({protocol:1,name:process.env.STORE_NAME||"Ouse 应用商店",maxPackageBytes:524288});}
