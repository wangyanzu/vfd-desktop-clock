const assert=require('node:assert/strict');
const fs=require('node:fs');
const {JSDOM}=require('jsdom');
async function until(check){for(let i=0;i<100;i++){if(check())return;await new Promise(r=>setTimeout(r,5));}throw Error('DOM did not settle');}
(async()=>{
 const dom=new JSDOM(fs.readFileSync('web/index.html','utf8'),{url:'http://device.test',runScripts:'outside-only'});
 const w=dom.window,get=id=>w.document.getElementById(id);let poll;
 const state={ssid:'Desk',ntp:'ntp.aliyun.com',csrf:'fixture',brightness:2,offset:480,storageReady:true,connected:true,
 temperature:null,humidity:null,sensorEnabled:true,sampleFresh:false,uptime:100,sensor:{error:6,samples:0,failures:3,consecutiveFailures:3,recoveries:1}};
 const timeout=w.setTimeout.bind(w);w.TextEncoder=TextEncoder;
 w.setTimeout=(fn,ms,...args)=>ms===3000?(poll=fn,0):timeout(fn,ms,...args);
 w.fetch=async()=>({ok:true,status:200,json:async()=>state});
 try{
  w.eval(fs.readFileSync('web/app.js','utf8'));
  await until(()=>get('sensor-note').textContent.includes('空测量帧'));
  assert.match(get('sensor-note').textContent,/连续失败 3 次/);assert.match(get('sensor-note').textContent,/暂无有效读数/);
  assert.equal(get('temperature').textContent,'—');
  state.temperature=25.6;state.humidity=50;await poll();
  assert.match(get('sensor-note').textContent,/保留最近有效数据/);assert.equal(get('temperature').textContent,'25.6°C');
  state.sensor.error=0;state.sensor.consecutiveFailures=0;state.sampleFresh=true;await poll();
  assert.match(get('sensor-note').textContent,/数据正常/);
  state.sensor.error=8;state.sampleFresh=false;await poll();assert.match(get('sensor-note').textContent,/总线恢复失败/);
  console.log('PASS: sensor failure reasons/counters, unavailable vs last real readings, recovery and bus fault display');
 }finally{w.close();}
})().catch(e=>{console.error(e);process.exitCode=1});
