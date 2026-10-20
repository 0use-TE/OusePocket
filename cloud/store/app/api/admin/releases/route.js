import {publish,updateReleaseNotes} from "../../../../lib/store.js";
import {authorized,boundedBody,json} from "../../../../lib/http.js";
export const runtime="nodejs";
export async function POST(request){
 if(!authorized(request))return json({error:"Unauthorized or admin token not configured (minimum 24 characters)"},401);
 try{const query=new URL(request.url).searchParams,bytes=await boundedBody(request,524288);const app=await publish(bytes,{versionCode:Number(query.get("versionCode")),versionName:query.get("versionName"),minNativeApi:Number(query.get("minNativeApi")),description:query.get("description")||"",author:query.get("author"),releasedAt:query.get("releasedAt"),category:query.get("category"),releaseNotes:query.get("releaseNotes")??""});return json({app},201);}catch(error){return json({error:error.message.slice(0,160)},400);}
}
export async function PATCH(request){
 if(!authorized(request))return json({error:"Unauthorized"},401);
 try{const body=JSON.parse((await boundedBody(request,4096)).toString("utf8"));const app=await updateReleaseNotes(body.id,body.versionCode,body.releaseNotes);return json({app});}catch(error){return json({error:error.message.slice(0,160)},400);}
}
