#!/usr/bin/env node
'use strict';
const fs=require('fs'),path=require('path'),vm=require('vm');
const html=fs.readFileSync(path.join(__dirname,'..','ui','index.html'),'utf8');
let failed=0;
const ok=(cond,msg)=>{console.log((cond?'PASS':'FAIL')+': '+msg);if(!cond)failed++;};

const ids=new Set([...html.matchAll(/id="([^"]+)"/g)].map(m=>m[1]));
const used=[...html.matchAll(/\$\('([^']+)'\)/g)].map(m=>m[1]);
const missing=[...new Set(used)].filter(id=>!ids.has(id));
ok(missing.length===0,'every referenced id exists ('+ids.size+' ids, '+new Set(used).size+' used)'+(missing.length?' missing: '+missing:''));

const script=html.match(/<script>([\s\S]*?)<\/script>/g).join('\n');
const nl=script.match(/function neutralized\(x,y\)\{[\s\S]*?\n\}/);
ok(!!nl,'neutralized() found in shipped HTML');
if(nl){
 const rd=script.match(/const round=([^;]+);/)[0];
 const ctx={Math,Number};
 vm.createContext(ctx);
 vm.runInContext(
  "'use strict';"+rd+";var __round=round;"
  +"var PI=3.14159265358979323846,TAU=6.2831853071795864769;"
  +"var lround=function(v){return Math.sign(v)*Math.floor(Math.abs(v)+0.5);};"
  +"var model=function(x,y,radius,angle,width){"
  +" if(x*x+y*y>radius*radius)return 0;"
  +" if(width===360||(!x&&!y))return 1;"
  +" var a=(angle-width/2)*PI/180,b=(angle+width/2)*PI/180;"
  +" var v=[radius*radius|0,lround(-Math.sin(a)*1024),lround(Math.cos(a)*1024),lround(Math.sin(b)*1024),lround(-Math.cos(b)*1024)];"
  +" var start=Math.atan2(-v[1],v[2]),end=Math.atan2(v[3],-v[4]);"
  +" var at=(Math.atan2(y,x)-start)%TAU;if(at<0)at+=TAU;"
  +" var span=(end-start)%TAU;if(span<0)span+=TAU;"
  +" return at<=span+1e-10||at>TAU-1e-10;};"
  +nl[0]+";var __ship=neutralized;"
  +"var sweep=function(angle,width,draft){"
  +" var m=0;"
  +" for(var xy=0;xy<65536;xy++){var x=(xy&255)-128,y=(xy>>8)-128;"
  +"  if(+__ship(x,y)!==+model(x,y,draft,angle,width))m++;}"
  +" return m;};"
  ,ctx);
 let mism=0,total=0,shown=0;
 const angles=[0,1,30,89,90,179,180,181,225,270,359],widths=[1,2,30,60,90,120,179,180,360],radii=[0,1,64,128,129,182];
 outer:
 for(const a of angles)for(const w of widths)for(const r of radii){
  vm.runInContext('width='+w+';angle='+a+';draft='+r+';',ctx);
  const m=vm.runInContext('sweep('+a+','+w+','+r+')',ctx);
  total+=65536;mism+=m;
  if(m&&shown<5){console.log('  config a'+a+' w'+w+' r'+r+': '+m+' mismatches');shown++;}
  if(mism>500)break outer;
 }
 ok(mism===0,'shipped neutralized() matches the asm-validated model on '+total+' XY points ('+mism+' mismatches)');
}

ok(script.includes("'/api/state'")&&script.includes("'/api/apply'")&&script.includes("'/api/disable'"),'API endpoints unchanged');
ok(script.includes("'X-Deadzone-Request':'1'"),'CSRF header kept');
ok(['radius=','&angle=','&width=','&rightRadius=','&rightAngle=','&rightWidth='].every(k=>script.includes(k)),'apply body carries both sticks in the order parse_cone() expects');
ok(html.includes('lang="en"'),'lang=en');
ok(!html.includes('http://')&&!html.includes('https://'),'fully self-contained (no external URLs)');
ok(html.includes('<canvas')&&html.includes('viewBox'),'canvas plot present');
process.exit(failed?1:0);
