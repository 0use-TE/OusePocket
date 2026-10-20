import {DatabaseSync} from "node:sqlite";
import {mkdir} from "node:fs/promises";
import path from "node:path";
export const dataDirectory=()=>path.resolve(process.env.STORE_DATA_DIR||"./data");
const columns="id,name,versionCode,versionName,minNativeApi,size,sha256,url,icon,iconPng,description,author,releasedAt,category,capabilities,publishedAt,updatedAt,releaseNotes";
const keys=columns.split(",");
export async function database(){
 const directory=dataDirectory();await mkdir(directory,{recursive:true});const db=new DatabaseSync(path.join(directory,"store.sqlite"));
 try{db.exec(`PRAGMA busy_timeout=5000; PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA foreign_keys=ON;
 CREATE TABLE IF NOT EXISTS apps (
 id TEXT PRIMARY KEY, name TEXT NOT NULL, versionCode INTEGER NOT NULL CHECK(versionCode>0), versionName TEXT NOT NULL,
 minNativeApi INTEGER NOT NULL CHECK(minNativeApi>0),size INTEGER NOT NULL CHECK(size>0 AND size<=524288),sha256 TEXT NOT NULL,
 url TEXT NOT NULL,icon TEXT NOT NULL,iconPng TEXT NOT NULL,description TEXT NOT NULL,author TEXT NOT NULL,
 releasedAt TEXT NOT NULL,category TEXT NOT NULL,capabilities TEXT NOT NULL,publishedAt TEXT NOT NULL,updatedAt TEXT NOT NULL);
 CREATE TABLE IF NOT EXISTS releases (appId TEXT NOT NULL,versionCode INTEGER NOT NULL,sha256 TEXT NOT NULL,size INTEGER NOT NULL,publishedAt TEXT NOT NULL,PRIMARY KEY(appId,versionCode),FOREIGN KEY(appId) REFERENCES apps(id));
 CREATE INDEX IF NOT EXISTS apps_category_updated ON apps(category,updatedAt DESC);
 CREATE INDEX IF NOT EXISTS apps_updated ON apps(updatedAt DESC);
 `);
  if(db.prepare("PRAGMA user_version").get().user_version<2){
   db.exec("BEGIN IMMEDIATE");
   try{
    for(const table of ["apps","releases"]){
     if(!db.prepare(`PRAGMA table_info(${table})`).all().some(column=>column.name==="releaseNotes"))
      db.exec(`ALTER TABLE ${table} ADD COLUMN releaseNotes TEXT NOT NULL DEFAULT ''`);
    }
    db.exec("PRAGMA user_version=2; COMMIT");
   }catch(error){db.exec("ROLLBACK");throw error;}
  }
  return db;}catch(error){db.close();throw error;}
}
export function decodeApp(row){return row?{...row,capabilities:JSON.parse(row.capabilities)}:null;}
export function saveApp(db,app){
 const values=keys.map(key=>key==="capabilities"?JSON.stringify(app[key]):key==="releaseNotes"?(app[key]??""):app[key]);
 db.prepare(`INSERT INTO apps (${columns}) VALUES (${keys.map(()=>"?").join(",")}) ON CONFLICT(id) DO UPDATE SET ${keys.filter(key=>key!=="id").map(key=>key+"=excluded."+key).join(",")}`).run(...values);
 db.prepare("INSERT INTO releases (appId,versionCode,sha256,size,publishedAt,releaseNotes) VALUES (?,?,?,?,?,?)").run(app.id,app.versionCode,app.sha256,app.size,app.updatedAt,app.releaseNotes??"");
}
