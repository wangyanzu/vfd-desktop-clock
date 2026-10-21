const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {JSDOM}=require('jsdom');
const root=path.join(__dirname,'..');
async function until(check){for(let i=0;i<100;++i){if(check())return;await new Promise(r=>setTimeout(r,5));}throw new Error('DOM did not settle');}
async function scenario(reject){
  const dom=new JSDOM(fs.readFileSync(path.join(root,'web/index.html'),'utf8'),{url:'http://device.test',runScripts:'outside-only'});
  const w=dom.window,get=id=>w.document.getElementById(id),sent=[];
  const state={ssid:'Desk',ntp:'ntp.aliyun.com',csrf:'fixture-csrf',brightness:2,offset:480,storageReady:true,
    connected:true,temperature:25,humidity:50,sensorEnabled:true,sampleFresh:true,uptime:100,
    mqtt:{enabled:true,host:'bemfa.com',port:9503,tls:true,topic:'desktop',username:'',clientIdSet:true,passwordSet:true}};
  w.TextEncoder=TextEncoder;
  let nextPoll; const originalTimeout=w.setTimeout.bind(w);
  w.setTimeout=(fn,delay,...args)=>delay===3000?(nextPoll=fn,0):originalTimeout(fn,delay,...args);
  w.fetch=async(url,options)=>{
    if(url==='/api/state')return{ok:true,status:200,json:async()=>state};
    sent.push({url,options});
    return{ok:!reject,status:reject?503:200,json:async()=>reject?{error:'保存失败，当前 MQTT 配置未改变。'}:{ok:true}};
  };
  try{
    w.eval(fs.readFileSync(path.join(root,'web/app.js'),'utf8'));
    await until(()=>!get('mqtt-fields').disabled);
    assert.equal(get('mqtt-host').value,'bemfa.com');
    assert.equal(get('mqtt-client-id').value,'');assert.equal(get('mqtt-password').value,'');
    assert.equal(get('mqtt-client-id-mode').value,'keep');
    assert.equal(get('mqtt-client-id-field').hidden,true);
    // Even values inserted into inactive fields must not be sent as credentials.
    get('mqtt-client-id').value='inactive-secret';get('mqtt-password').value='inactive-password';
    get('mqtt-topic').value='新主题';
    assert.ok(nextPoll); await nextPoll();
    assert.equal(get('mqtt-topic').value,'新主题');
    get('mqtt-settings').dispatchEvent(new w.Event('submit',{cancelable:true}));
    get('mqtt-settings').dispatchEvent(new w.Event('submit',{cancelable:true}));
    await until(()=>get('mqtt-result').className.length>0);
    assert.equal(sent.length,1);assert.equal(sent[0].url,'/api/mqtt');
    const body=sent[0].options.body;
    assert.equal(body.get('client_id'),'');assert.equal(body.get('password'),'');
    assert.equal(body.get('client_id_mode'),'keep');assert.equal(body.get('topic'),'新主题');
    assert.equal(body.has('ssid'),false);assert.equal(sent[0].options.headers['X-CSRF-Token'],'fixture-csrf');
    if(reject){assert.equal(get('mqtt-result').className,'error');assert.equal(get('mqtt-topic').value,'新主题');}
    else{assert.equal(get('mqtt-result').className,'success');assert.equal(get('mqtt-client-id').value,'');}
    get('mqtt-client-id-mode').value='replace'; get('mqtt-client-id-mode').dispatchEvent(new w.Event('change'));
    assert.equal(get('mqtt-client-id-field').hidden,false);assert.equal(get('mqtt-client-id').required,true);
    get('mqtt-client-id').value='new-fixture-secret';get('mqtt-password-mode').value='clear';
    get('mqtt-password-mode').dispatchEvent(new w.Event('change'));
    get('mqtt-settings').dispatchEvent(new w.Event('submit',{cancelable:true}));
    await until(()=>sent.length===2);
    assert.equal(sent[1].options.body.get('client_id'),'new-fixture-secret');
    assert.equal(sent[1].options.body.get('password_mode'),'clear');
    assert.equal(sent[1].options.body.get('password'),'');
    await until(()=>get('mqtt-save').textContent === '保存消息设置 ↗');
  }finally{w.close();}
}
(async()=>{await scenario(false);await scenario(true);console.log('PASS: MQTT independent form, no secret echo, explicit keep/replace/clear, CSRF, duplicate guard and save failure');})().catch(e=>{console.error(e);process.exitCode=1;});
