'use strict';
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
for(const folder of ['klotski','minesweeper','mole','tictactoe']){
 const data=JSON.parse(fs.readFileSync(`apps/${folder}/page.json`,'utf8'));
 const pages=data.pages||[{id:'main',root:data.root}],source=fs.readFileSync(`apps/${folder}/app.js`,'utf8').replace(/\r/g,'');
 const snippet=source.split('// BEGIN responsive board\n')[1].split('// END responsive board')[0];
 for(const page of pages){
  const nodes=new Map(),parents=new Map();function walk(n,p){nodes.set(n.id,n);if(p)parents.set(n.id,p.id);for(const c of n.children||[])walk(c,n);}walk(page.root);
  for(const [w,h]of [[320,416],[320,400],[480,256],[480,240]]){
   const frames=new Map(),Ouse={navigation:{get:()=>({page:page.id})},display:{get:()=>({contentWidthDp:w,contentHeightDp:h}),onChange(){}},ui:{layout(batch){assert(batch.length<=64);for(const f of batch){assert(nodes.has(f.id));frames.set(f.id,f);}}}};
   vm.runInNewContext(snippet,{Ouse});
   if(!nodes.has('board')){assert.equal(frames.size,0);continue;}
   for(const[id,f]of frames){const parent=frames.get(parents.get(id))||{width:w,height:h};assert(f.width>0&&f.height>0&&f.x>=0&&f.y>=0&&f.x+f.width<=parent.width&&f.y+f.height<=parent.height,JSON.stringify({folder,page:page.id,w,h,id,f,parent}));}
   const cells=nodes.get('board').children.flatMap(r=>r.children);
   for(const c of cells){const f=frames.get(c.id);assert.equal(f.width,f.height);assert(f.width>=20);}
  }
 }
 console.log('PASS square-cell viewport geometry, all board pages and both navigation heights:',folder);
}
for(const folder of ['dice','rps','reaction','guess']){
 const data=JSON.parse(fs.readFileSync(`apps/${folder}/page.json`,'utf8')),nodes=new Map(),parents=new Map();
 function walk(n,p){nodes.set(n.id,n);if(p)parents.set(n.id,p.id);for(const c of n.children||[])walk(c,n);}walk(data.root);
 const source=fs.readFileSync(`apps/${folder}/app.js`,'utf8').replace(/\r/g,''),snippet=source.split('// BEGIN responsive simple game\n')[1].split('// END responsive simple game')[0];
 for(const[w,h]of [[320,416],[320,400],[480,256],[480,240]]){
  const frames=new Map(),Ouse={display:{get:()=>({contentWidthDp:w,contentHeightDp:h}),onChange(){}},ui:{layout(batch){for(const f of batch){assert(nodes.has(f.id));frames.set(f.id,f);}}}};vm.runInNewContext(snippet,{Ouse});
  for(const[id,f]of frames){const parent=frames.get(parents.get(id))||{width:w,height:h};assert(f.x>=0&&f.y>=0&&f.x+f.width<=parent.width&&f.y+f.height<=parent.height,JSON.stringify({folder,id,f,parent,w,h}));}
 }
 console.log('PASS complete game controls across four viewports:',folder);
}
