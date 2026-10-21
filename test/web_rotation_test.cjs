const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {JSDOM} = require('jsdom');
const root = path.join(__dirname, '..');
async function until(check) { for(let i=0;i<100;++i) { if(check()) return; await new Promise(r=>setTimeout(r,5)); } throw new Error('DOM did not settle'); }
async function scenario({reject=false,storage=true}={}) {
  const dom = new JSDOM(fs.readFileSync(path.join(root,'web/index.html'),'utf8'), {url:'http://device.test',runScripts:'outside-only'});
  const w=dom.window, get=id=>w.document.getElementById(id), sent=[];
  const state={ssid:'Desk',ntp:'ntp.aliyun.com',csrf:'fixture-csrf',brightness:2,offset:480,storageReady:storage,
    connected:true,temperature:25,humidity:50,sensorEnabled:true,sampleFresh:true,uptime:100,
    rotation:{pages:3,hold:8},contentPage:'indoor',rotationPaused:true};
  w.TextEncoder=TextEncoder;
  let nextPoll; const originalTimeout=w.setTimeout.bind(w);
  w.setTimeout=(fn,delay,...args)=>delay===3000?(nextPoll=fn,0):originalTimeout(fn,delay,...args);
  w.fetch=async(url,options)=>{
    if(url==='/api/state') return {ok:true,status:200,json:async()=>state};
    sent.push({url,options});
    if(!reject) state.rotation={pages:Number(options.body.get('pages')),hold:Number(options.body.get('hold'))};
    return {ok:!reject,status:reject?503:200,json:async()=>reject?{error:'保存失败，当前轮播配置未改变。'}:{ok:true}};
  };
  try {
    w.eval(fs.readFileSync(path.join(root,'web/app.js'),'utf8'));
    await until(()=>get('rotation-hold').value==='8' && get('rotation-status').textContent.includes('室内温湿度'));
    assert.match(get('rotation-status').textContent,/已暂停/);
    assert.equal(get('rotation-fields').disabled,!storage);
    if(!storage) return;
    assert.equal(get('page-date').checked,true); assert.equal(get('page-indoor').checked,true);
    assert.equal(get('page-temperature').checked,false);
    for(const page of ['date','indoor','temperature','humidity']) get(`page-${page}`).checked=false;
    get('rotation-settings').dispatchEvent(new w.Event('submit',{cancelable:true}));
    assert.equal(sent.length,0); assert.match(get('rotation-result').textContent,/至少选择/);
    get('page-humidity').checked=true; get('rotation-hold').value='12';
    await nextPoll();
    assert.equal(get('page-humidity').checked,true); assert.equal(get('page-date').checked,false);
    assert.equal(get('rotation-hold').value,'12'); // live status cannot overwrite edits
    get('rotation-settings').dispatchEvent(new w.Event('submit',{cancelable:true}));
    get('rotation-settings').dispatchEvent(new w.Event('submit',{cancelable:true}));
    await until(()=>get('rotation-save').textContent==='保存轮播设置 ↗');
    assert.equal(sent.length,1); assert.equal(sent[0].url,'/api/rotation');
    assert.equal(sent[0].options.headers['X-CSRF-Token'],'fixture-csrf');
    assert.equal(sent[0].options.body.toString(),'pages=8&hold=12');
    assert.equal(get('rotation-result').className,reject?'error':'success');
    assert.equal(get('page-humidity').checked,true); assert.equal(get('rotation-hold').value,'12');
    assert.equal(get('rotation-fields').disabled,false);
  } finally { w.close(); }
}
(async()=>{ await scenario(); await scenario({reject:true}); await scenario({storage:false}); console.log('PASS: rotation hydration, empty-selection guard, independent save/CSRF, duplicate guard, edit preservation and read-only storage'); })().catch(error=>{console.error(error);process.exitCode=1;});
