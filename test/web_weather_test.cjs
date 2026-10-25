const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {JSDOM} = require('jsdom');
const root = path.join(__dirname,'..');
async function until(check) { for (let i=0;i<100;++i) { if (check()) return; await new Promise(r=>setTimeout(r,5)); } throw new Error('DOM did not settle'); }
async function scenario({reject=false,storage=true}={}) {
 const dom=new JSDOM(fs.readFileSync(path.join(root,'web/index.html'),'utf8'),{url:'http://device.test',runScripts:'outside-only'});
 const w=dom.window,get=id=>w.document.getElementById(id),sent=[];
 const state={ssid:'Desk',ntp:'ntp.aliyun.com',csrf:'fixture-csrf',brightness:2,offset:480,storageReady:storage,connected:true,
  temperature:25.6,humidity:50,sensorEnabled:true,sampleFresh:true,uptime:100,rotation:{pages:3,hold:8},
  weather:{enabled:false,location:'beijing',refreshMinutes:15,publicKeySet:false,privateKeySet:false},
  outdoor:{state:'online',temperature:18,humidity:null,fresh:true,city:'Beijing',text:'Cloudy',updated:'2026-10-25T12:00:00+08:00'}};
 let nextPoll,offline=false; const timeout=w.setTimeout.bind(w);
 w.TextEncoder=TextEncoder; w.setTimeout=(fn,delay,...args)=>delay===3000?(nextPoll=fn,0):timeout(fn,delay,...args);
 w.fetch=async(url,options)=>{
  if (url==='/api/state') { if (offline) throw Error('offline'); return {ok:true,status:200,json:async()=>state}; }
  sent.push({url,options});
  if (!reject) state.weather={enabled:true,location:options.body.get('location'),refreshMinutes:Number(options.body.get('refresh')),publicKeySet:true,privateKeySet:true};
  return {ok:!reject,status:reject?503:200,json:async()=>reject?{error:'Save failed'}:{ok:true}};
 };
 try {
  w.eval(fs.readFileSync(path.join(root,'web/app.js'),'utf8'));
  await until(()=>get('weather-location').value==='beijing');
  assert.equal(get('weather-fields').disabled,!storage);
  assert.equal(get('temperature-difference'),null);
  assert.equal(get('indoor-comparison').textContent,'25.6°C');
  assert.equal(get('outdoor-temperature').textContent,'18.0°C');
  assert.match(get('weather-observation').textContent,/接口未提供/);
  state.outdoor.temperature=30;
  await nextPoll(); assert.equal(get('outdoor-temperature').textContent,'30.0°C');
  state.sampleFresh=false; await nextPoll(); assert.match(get('indoor-comparison').textContent,/旧/);
  state.sampleFresh=true; state.outdoor.fresh=false; await nextPoll();
  assert.match(get('weather-status').textContent,/过期/);
  state.outdoor.fresh=true; state.temperature=null;
  await nextPoll(); assert.equal(get('indoor-comparison').textContent,'—');
  state.outdoor.state='retrying'; state.outdoor.httpStatus=404; state.outdoor.errorCode='AP010010';
  await nextPoll(); assert.match(get('weather-status').textContent,/找不到天气地点/); assert.match(get('weather-status').textContent,/404.*AP010010/);
  state.outdoor.state='online'; state.outdoor.httpStatus=200; state.outdoor.errorCode='';
  if (!storage) return;
  get('weather-location').value='shanghai';
  await nextPoll(); assert.equal(get('weather-location').value,'shanghai'); // polling preserves edits
  get('weather-enabled').value='1';
  for (const key of ['public','private']) {
   get(`weather-${key}-mode`).value='replace';get(`weather-${key}-mode`).dispatchEvent(new w.Event('change'));
   get(`weather-${key}`).value=key+'-fixture';
  }
  get('weather-settings').dispatchEvent(new w.Event('submit',{cancelable:true}));
  get('weather-settings').dispatchEvent(new w.Event('submit',{cancelable:true}));
  await until(()=>get('weather-save').textContent==='保存天气设置 ↗');
  assert.equal(sent.length,1); assert.equal(sent[0].url,'/api/weather');
  assert.equal(sent[0].options.headers['X-CSRF-Token'],'fixture-csrf');
  assert.equal(sent[0].options.body.get('private_key'),'private-fixture');
  assert.equal(sent[0].options.body.get('public_key'),'public-fixture');
  assert.equal(sent[0].options.body.get('location'),'shanghai');
  if (!reject) {
   assert.equal(get('weather-private').value,''); assert.equal(get('weather-public').value,'');
   assert.equal(get('weather-private-mode').value,'keep'); assert.equal(get('weather-public-mode').value,'keep');
  } else assert.equal(get('weather-private').value,'private-fixture');
  assert.equal(get('weather-result').className,reject?'error':'success');
  state.temperature=25;
  offline=true; await nextPoll();
  assert.match(get('weather-status').textContent,/连接中断/);
 } finally {w.close();}
}
(async()=>{await scenario();await scenario({reject:true});await scenario({storage:false});console.log('PASS: weather credentials save/clear/CSRF, edit preservation, independent indoor/outdoor missing/stale values, failed saves and connection loss');})().catch(e=>{console.error(e);process.exitCode=1});
