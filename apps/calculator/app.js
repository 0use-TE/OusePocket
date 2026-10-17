"use strict";
let expr="", mode="simple", base=10, pendingBit=null, acc=null, loaded=false;
const MAX_EXPR=48;
function visible(id,show){if(show)Ouse.ui.show(id);else Ouse.ui.hide(id);}
function persist(){
 if(!loaded)return;
 Ouse.storage.set("calculator",{version:1,mode:mode,base:base===16?16:10});
}
function render(answer){
 Ouse.ui.setText("expr",expr||"0");
 Ouse.ui.setText("result",answer);
}
function paintMode(){
 const science=mode==="science",prog=mode==="prog";
 visible("science",science);visible("trig",science);visible("prog",prog);visible("hex",prog&&base===16);
 Ouse.ui.setText("b_base",base===16?"HEX":"DEC");
 render(previewAnswer());
}
function lastChar(){return expr.length?expr.charAt(expr.length-1):"";}
function trailingOperators(){let count=0;for(let i=expr.length-1;i>=0;i--){const ch=expr.charAt(i);if(ch!=="+"&&ch!=="-"&&ch!=="*"&&ch!=="/")break;count++;}return count;}
function currentNumber(){let end=expr.length;while(end>0){const ch=expr.charAt(end-1);if(!((ch>="0"&&ch<="9")||ch==="."||(ch>="a"&&ch<="f")||(ch>="A"&&ch<="F")))break;end--;}return expr.slice(end);}
function openCount(){let count=0;for(let i=0;i<expr.length;i++){const ch=expr.charAt(i);if(ch==="(")count++;else if(ch===")")count--;}return count;}
function endedValue(){const ch=lastChar();return (ch>="0"&&ch<="9")||(ch>="a"&&ch<="f")||(ch>="A"&&ch<="F")||ch===")"||ch==="%";}
function isUnaryMinus(text,index){if(text.charAt(index)!=="-")return false;if(index===0)return true;const prev=text.charAt(index-1);return prev==="+"||prev==="-"||prev==="*"||prev==="/"||prev==="(";}
function atomStart(text,percentIndex){
 let index=percentIndex-1;
 if(text.charAt(index)===")"){let depth=1;index--;while(index>=0&&depth){const ch=text.charAt(index);if(ch===")")depth++;else if(ch==="(")depth--;index--;}index++;}
 else{while(index>=0){const ch=text.charAt(index);if((ch>="0"&&ch<="9")||ch==="."||(ch>="a"&&ch<="f")||(ch>="A"&&ch<="F")){index--;continue;}break;}index++;}
 if(index>0&&isUnaryMinus(text,index-1))index--;
 return index;
}
function format(value){
 if(typeof value!=="number"||value!==value||value===Infinity||value===-Infinity)return null;
 if(mode==="prog"){
  const n=value|0;
  return base===16?("0x"+(n>>>0).toString(16).toUpperCase()):String(n);
 }
 const rounded=Math.round(value*100000000)/100000000;
 if(rounded===0)return"0";
 let text=String(rounded);
 if(text.indexOf("e")>=0||text.indexOf("E")>=0){
  text=rounded.toFixed(8);
  while(text.indexOf(".")>=0&&(text.charAt(text.length-1)==="0"||text.charAt(text.length-1)==="."))text=text.slice(0,-1);
  if(!text||text==="-")text="0";
 }
 if(text.length>16)return null;
 return text;
}
function parseNumberToken(token){
 if(!token)throw new Error("Error");
 if(mode==="prog"&&base===16)return parseInt(token,16);
 return Number(token);
}
function expandPercent(text){
 const mark=text.indexOf("%");
 if(mark<0)return text;
 const start=atomStart(text,mark);
 const atom=parse(text.slice(start,mark));
 const opIndex=start-1;
 const op=opIndex>=0?text.charAt(opIndex):"";
 const prev=opIndex>0?text.charAt(opIndex-1):"";
 const binaryAdd=(op==="+"||op==="-")&&opIndex>0&&prev!=="+"&&prev!=="-"&&prev!=="*"&&prev!=="/"&&prev!=="(";
 let from=start,shown;
 if(binaryAdd){shown=format(parse(text.slice(0,opIndex))*atom/100);from=opIndex;}
 else if(op==="*"||op==="/"){shown=format(atom/100);from=opIndex;}
 else shown=format(atom/100);
 if(shown===null)throw new Error("Error");
 const prefix=binaryAdd||op==="*"||op==="/"?op+shown:shown;
 return expandPercent(text.slice(0,from)+prefix+text.slice(mark+1));
}
function parse(text){
 const source=mode==="science"?expandPercent(text):text;
 let index=0;
 function peek(){return source.charAt(index);}
 function expression(){let value=term();while(peek()==="+"||peek()==="-"){const op=peek();index++;const right=term();value=op==="+"?value+right:value-right;}return value;}
 function term(){let value=unary();while(peek()==="*"||peek()==="/"){const op=peek();index++;const right=unary();if(op==="/"&&right===0)throw new Error("Div/0");value=op==="*"?value*right:value/right;}return value;}
 function unary(){if(peek()==="+"){index++;return unary();}if(peek()==="-"){index++;return -unary();}return primary();}
 function primary(){if(peek()==="("){index++;const value=expression();if(peek()!==")")throw new Error("Error");index++;return value;}return number();}
 function number(){
  const start=index;
  while((peek()>="0"&&peek()<="9")||(mode==="prog"&&base===16&&((peek()>="a"&&peek()<="f")||(peek()>="A"&&peek()<="F"))))index++;
  if(mode!=="prog"&&peek()==="."){index++;if(peek()<"0"||peek()>"9")throw new Error("Error");while(peek()>="0"&&peek()<="9")index++;}
  if(index===start)throw new Error("Error");
  return parseNumberToken(source.slice(start,index));
 }
 const value=expression();
 if(index!==source.length)throw new Error("Error");
 return mode==="prog"?(value|0):value;
}
function answerOf(text){
 try{const shown=format(parse(text));return shown===null?"Error":shown;}
 catch(error){return error.message==="Div/0"?"Div/0":"Error";}
}
function previewAnswer(){
 if(!expr)return "0";
 if(!endedValue()||openCount())return "0";
 return answerOf(expr);
}
function preview(){render(previewAnswer());}
function append(ch){
 if(expr.length>=MAX_EXPR)return;
 const ended=endedValue();
 const operators=trailingOperators();
 const number=currentNumber();
 const hexDigit=(ch>="a"&&ch<="f")||(ch>="A"&&ch<="F");
 if((ch>="0"&&ch<="9")||(mode==="prog"&&base===16&&hexDigit)){
  if(lastChar()===")"||lastChar()==="%")return;
  if(number==="0"&&ch!=="."&&!(mode==="prog"&&base===16)){if(ch==="0")return;expr=expr.slice(0,-1)+ch;}
  else expr+=ch;
 }else if(ch==="."){
  if(mode==="prog")return;
  if(!number||number.indexOf(".")>=0)return;
  expr+=ch;
 }else if(ch==="+"||ch==="*"||ch==="/"){
  if(!ended&&operators!==1)return;
  if(operators===1)expr=expr.slice(0,-1);
  expr+=ch;
 }else if(ch==="-"){
  if(lastChar()==="."||operators>=2)return;
  expr+=ch;
 }else if(ch==="("){
  if(ended||lastChar()===".")return;
  expr+=ch;
 }else if(ch===")"){
  if(!ended||!openCount())return;
  expr+=ch;
 }else if(ch==="%"){
  if(mode!=="science"||!ended)return;
  expr+="%";
 }
 preview();
}
function replaceCurrent(value){
 const shown=format(value);
 if(shown===null){render("Error");return;}
 expr=mode==="prog"&&base===16?shown.replace(/^0x/i,""):shown;
 render(shown);
}
function currentValue(){
 if(!expr)return 0;
 if(endedValue()&&!openCount()){
  const shown=answerOf(expr);
  if(shown==="Error"||shown==="Div/0")throw new Error(shown);
  if(mode==="prog"&&base===16)return parseInt(String(shown).replace(/^0x/i,""),16)|0;
  return Number(shown);
 }
 const number=currentNumber();
 if(!number)throw new Error("Error");
 return parseNumberToken(number);
}
function applyUnary(label,fn){
 try{
  const input=currentValue();
  const before=format(input);
  if(before===null){render("Error");return;}
  const shown=format(fn(input));
  if(shown===null){render("Error");return;}
  expr=shown;
  Ouse.ui.setText("expr",label+"("+before+")");
  Ouse.ui.setText("result",shown);
 }catch(error){render(error.message==="Div/0"?"Div/0":"Error");}
}
function toRad(deg){return deg*Math.PI/180;}
function autoParen(){
 if(endedValue()&&openCount()){append(")");return;}
 if(endedValue()){if(expr.length+2>MAX_EXPR)return;expr+="*(";preview();return;}
 append("(");
}
function clearAll(){expr="";pendingBit=null;acc=null;preview();}
function backspace(){expr=expr.slice(0,-1);preview();}
function equals(){
 if(mode==="prog"&&pendingBit&&acc!==null){
  try{
   const right=currentValue()|0;
   let value=acc;
   if(pendingBit==="AND")value=acc&right;
   else if(pendingBit==="OR")value=acc|right;
   else if(pendingBit==="XOR")value=acc^right;
   pendingBit=null;acc=null;replaceCurrent(value);
  }catch(error){render("Error");}
  return;
 }
 if(!expr||!endedValue()||openCount()){if(expr)render("Error");return;}
 const shown=answerOf(expr);
 if(shown==="Error"||shown==="Div/0"){render(shown);return;}
 expr=mode==="prog"&&base===16?shown.replace(/^0x/i,""):shown;
 render(shown);
}
function beginBit(op){
 try{acc=currentValue()|0;pendingBit=op;expr="";render(op);}
 catch(error){render("Error");}
}
function setMode(next){
 mode=next;pendingBit=null;acc=null;
 if(mode!=="prog")base=10;
 if(mode==="prog")expr=expr.replace(/\./g,"");
 Ouse.ui.hide("modeSheet");
 paintMode();
 persist();
}
function toggleBase(){
 if(mode!=="prog")return;
 try{
  const value=expr?currentValue()|0:0;
  base=base===16?10:16;
  replaceCurrent(value);
 }catch(error){base=base===16?10:16;expr="";preview();}
 paintMode();
 persist();
}
Ouse.ui.on("modeBtn","click",()=>Ouse.ui.show("modeSheet"));
Ouse.ui.on("pick_simple","click",()=>setMode("simple"));
Ouse.ui.on("pick_science","click",()=>setMode("science"));
Ouse.ui.on("pick_prog","click",()=>setMode("prog"));
const keys={b_div:"/",b_mul:"*",b_sub:"-",b_add:"+",b_dot:".",b_0:"0",b_1:"1",b_2:"2",b_3:"3",b_4:"4",b_5:"5",b_6:"6",b_7:"7",b_8:"8",b_9:"9",b_a:"A",b_b:"B",b_c:"C",b_d:"D",b_e:"E",b_f:"F"};
Object.keys(keys).forEach(id=>Ouse.ui.on(id,"click",()=>append(keys[id])));
Ouse.ui.on("b_ac","click",clearAll);
Ouse.ui.on("b_bs","click",backspace);
Ouse.ui.on("b_eq","click",equals);
Ouse.ui.on("b_paren","click",autoParen);
Ouse.ui.on("b_pct","click",()=>append("%"));
Ouse.ui.on("b_sqrt","click",()=>applyUnary("sqrt",v=>{if(v<0)throw new Error("Error");return Math.sqrt(v);}));
Ouse.ui.on("b_sq","click",()=>applyUnary("sq",v=>v*v));
Ouse.ui.on("b_inv","click",()=>applyUnary("1/",v=>{if(v===0)throw new Error("Div/0");return 1/v;}));
Ouse.ui.on("b_neg","click",()=>applyUnary("+/-",v=>-v));
Ouse.ui.on("b_pi","click",()=>{if(endedValue()){if(expr.length+4>MAX_EXPR)return;expr+="*"+format(Math.PI);preview();}else{expr+=format(Math.PI);preview();}});
Ouse.ui.on("b_sin","click",()=>applyUnary("sin",v=>Math.sin(toRad(v))));
Ouse.ui.on("b_cos","click",()=>applyUnary("cos",v=>Math.cos(toRad(v))));
Ouse.ui.on("b_tan","click",()=>applyUnary("tan",v=>Math.tan(toRad(v))));
Ouse.ui.on("b_base","click",toggleBase);
Ouse.ui.on("b_and","click",()=>beginBit("AND"));
Ouse.ui.on("b_or","click",()=>beginBit("OR"));
Ouse.ui.on("b_xor","click",()=>beginBit("XOR"));
Ouse.ui.on("b_not","click",()=>applyUnary("NOT",v=>(~(v|0))));
paintMode();
Ouse.storage.get("calculator").then(value=>{
 if(value&&value.version===1){
  if(value.mode==="science"||value.mode==="prog"||value.mode==="simple")mode=value.mode;
  if(mode==="prog"&&value.base===16)base=16;
 }
 loaded=true;paintMode();
},()=>{loaded=true;persist();});
