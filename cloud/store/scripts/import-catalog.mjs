import {readFile} from "node:fs/promises";
import path from "node:path";
import {createHash} from "node:crypto";
import {database,dataDirectory,saveApp} from "../lib/database.js";
import {validate} from "../lib/bpk.js";
const entries=JSON.parse(await readFile(path.join(dataDirectory(),"catalog.json"),"utf8"));
if(!Array.isArray(entries))throw Error("Invalid source catalog");
for(const app of entries){if(!/^user\.[a-z0-9._-]{1,42}$/.test(app.id)||!/^[a-f0-9]{64}$/.test(app.sha256))throw Error("Invalid source identity");const bytes=await readFile(path.join(dataDirectory(),"objects",app.sha256+".bpk"));if(createHash("sha256").update(bytes).digest("hex")!==app.sha256)throw Error("Source checksum mismatch");const {manifest}=validate(bytes);if(manifest.id!==app.id||manifest.versionCode!==app.versionCode||bytes.length!==app.size)throw Error("Source manifest mismatch");}
const db=await database();try{db.exec("BEGIN IMMEDIATE");if(db.prepare("SELECT count(*) AS count FROM apps").get().count)throw Error("Destination is not empty; refusing to overwrite");for(const app of entries)saveApp(db,app);db.exec("COMMIT");console.log("Imported",entries.length,"applications; source JSON retained");}catch(error){db.exec("ROLLBACK");throw error;}finally{db.close();}
