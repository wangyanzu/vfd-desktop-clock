const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { JSDOM } = require('jsdom');
const root=path.join(__dirname,'..');
async function until(check){for(let i=0;i<100;++i){if(check())return;await new Promise(r=>setTimeout(r,5));}throw new Error('DOM state did not settle');}
async function scenario(rejected){
  const dom=new JSDOM(fs.readFileSync(path.join(root,'web/index.html'),'utf8'),{url:'http://device.test',runScripts:'outside-only'});
  const w=dom.window, get=id=>w.document.getElementById(id), sent=[];
  const state={ssid:'Desk', ntp:'ntp.aliyun.com',csrf:'fixture-csrf',brightness:2,offset:480,storageReady:true,
    connected:true,setupActive:false,temperature:25,humidity:50,sensorEnabled:true,sampleFresh:true,uptime:100,
    mqttState:'online',messageCount:2,messageId:16,messageVisible:true,messagesDropped:1,messagesDuplicate:3};
  w.TextEncoder=TextEncoder;
  w.fetch=async (url,options)=>{
    if(url==='/api/state')return{ok:true,status:200,json:async()=>state};
    sent.push({url,options});
    return{ok:!rejected,status:rejected?409:202,json:async()=>rejected?{error:'当前消息已变化'}:{accepted:true}};
  };
  try{
    w.eval(fs.readFileSync(path.join(root,'web/app.js'),'utf8'));
    await until(()=>!get('ack-message').disabled);
    assert.match(get('message-status').textContent,/未确认 2 条/);
    assert.match(get('mqtt-status').textContent,/已连接/);
    get('ack-message').click(); get('ack-message').click();
    await until(()=>get('ack-result').textContent.length>0);
    assert.equal(sent.length,1);assert.equal(sent[0].url,'/api/messages/ack');
    assert.equal(sent[0].options.body.toString(),'id=16');
    assert.equal(sent[0].options.headers['X-CSRF-Token'],'fixture-csrf');
    assert.equal(get('ack-message').disabled,true);
    if(rejected){assert.equal(get('ack-result').className,'error');assert.match(get('ack-result').textContent,/消息已变化/);}
    else{assert.match(get('ack-result').textContent,/请求已提交/);assert.doesNotMatch(get('ack-result').textContent,/已清除/);}
  }finally{w.close();}
}
(async()=>{await scenario(false);await scenario(true);console.log('PASS: visible-message confirmation, stable ID/CSRF, double-click guard, accepted vs completed and stale-message conflict');})().catch(e=>{console.error(e);process.exitCode=1});
