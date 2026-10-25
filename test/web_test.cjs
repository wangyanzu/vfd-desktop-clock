// DOM interaction tests with an explicit device API fixture; no physical device.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { JSDOM } = require('jsdom');
const root = path.resolve(__dirname, '..');
const initial = {ssid:'Desk WiFi', ntp:'ntp.aliyun.com', ip:'192.168.1.40', csrf:'test-csrf',
  passwordSet:true, storageReady:true, brightness:2, offset:480, connected:true,
  setupActive:false, timeValid:true, sensorEnabled:true, sampleFresh:true,
  uptime:3661, temperature:24.6, humidity:51.2, clock:'14:32:08'};
async function until(check) {
  const deadline = Date.now() + 1000;
  while (!check()) {
    if (Date.now() > deadline) throw new Error('DOM state did not settle');
    await new Promise(resolve => setTimeout(resolve, 5));
  }
}
function page({state=initial, reject=false, offline=false}={}) {
  const dom = new JSDOM(fs.readFileSync(path.join(root,'web/index.html'),'utf8'),
    {url:'http://device.test/', runScripts:'outside-only'});
  const {window} = dom;
  const sent = [];
  window.TextEncoder = TextEncoder;
  window.fetch = async (url, options) => {
    if (offline) throw new Error('Connection unavailable');
    if (url === '/api/state') return {ok:true, status:200, json:async()=>state};
    sent.push({url, options, body:new URLSearchParams(options.body.toString())});
    return {ok:!reject, status:reject?503:200, json:async()=>reject?{error:'保存失败，当前配置未改变。'}:{ok:true}};
  };
  window.eval(fs.readFileSync(path.join(root,'web/app.js'),'utf8'));
  return {dom, window, sent, get:id=>window.document.getElementById(id)};
}
(async () => {
  const p = page();
  try {
    await until(()=>!p.get('fields').disabled);
    assert.equal(p.get('ssid').value,'Desk WiFi');
    assert.equal(p.get('clock').textContent,'14:32:08');
    assert.equal(p.get('password').value,'');
    assert.equal(p.get('password-mode').value,'keep');
    assert.equal(p.get('password-field').hidden,true);
    p.get('brightness').value='3';
    p.get('settings').dispatchEvent(new p.window.Event('submit',{cancelable:true}));
    await until(()=>p.get('result').className==='success');
    assert.equal(p.sent.length,1);
    assert.equal(p.sent[0].body.get('brightness'),'3');
    assert.equal(p.sent[0].body.get('password_mode'),'keep');
    assert.equal(p.sent[0].body.get('password'),'');
    assert.equal(p.sent[0].options.headers['X-CSRF-Token'],'test-csrf');
    p.get('password-mode').value='replace';
    p.get('password-mode').dispatchEvent(new p.window.Event('change'));
    assert.equal(p.get('password-field').hidden,false);
    p.get('password').value='fixture-password';
    p.get('toggle-password').click(); assert.equal(p.get('password').type,'text');
    p.get('settings').dispatchEvent(new p.window.Event('submit',{cancelable:true}));
    await until(()=>p.sent.length===2 && !p.get('fields').disabled);
    assert.equal(p.sent[1].body.get('password'),'fixture-password');
    assert.equal(p.get('password').value,'');
    p.get('ssid').value='桌'.repeat(11);
    p.get('settings').dispatchEvent(new p.window.Event('submit',{cancelable:true}));
    assert.equal(p.get('result').className,'error'); assert.equal(p.sent.length,2);
  } finally {p.dom.window.close();}
  const legacy = page({state:{...initial,brightness:4}});
  try {
    await until(()=>!legacy.get('fields').disabled);
    assert.equal(legacy.get('brightness').value,'3');
    assert.equal(legacy.get('brightness').options.length,3);
    assert.match(legacy.window.document.getElementById('display-title').parentElement.textContent,/60 秒/);
    assert.equal(legacy.sent.length,0); // showing a migrated choice does not rewrite NVS
  } finally {legacy.dom.window.close();}
  const failure = page({reject:true});
  try {
    await until(()=>!failure.get('fields').disabled);
    failure.get('settings').dispatchEvent(new failure.window.Event('submit',{cancelable:true}));
    await until(()=>failure.get('result').className==='error');
    assert.match(failure.get('result').textContent,/保存失败/);
    assert.equal(failure.get('fields').disabled,false);
  } finally {failure.dom.window.close();}
  const readonly = page({state:{...initial,storageReady:false,temperature:null,humidity:null}});
  try {
    await until(()=>readonly.get('result').className==='error');
    assert.equal(readonly.get('fields').disabled,true);
    assert.equal(readonly.get('temperature').textContent,'—');
  } finally {readonly.dom.window.close();}
  const offline = page({offline:true});
  try {
    await until(()=>offline.get('network-label').textContent==='UNREACHABLE');
    assert.equal(offline.get('fields').disabled,true);
    assert.equal(offline.get('result').className,'error');
  } finally {offline.dom.window.close();}
  console.log('PASS: page hydration, password controls, encoded save/CSRF, UTF-8 limits, storage failure, offline state');
})().catch(error=>{ console.error(error); process.exitCode=1; });
