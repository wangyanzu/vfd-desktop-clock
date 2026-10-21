const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { JSDOM } = require('jsdom');
const dom = new JSDOM(fs.readFileSync(path.join(__dirname,'../docs/animation-preview.html'),'utf8'),
  {runScripts:'dangerously', pretendToBeVisual:true});
try {
  const {window} = dom;
  const get = id => window.document.getElementById(id);
  const change = id => get(id).dispatchEvent(new window.Event('change'));
  const end = () => { get('timeline').value = get('timeline').max; get('timeline').dispatchEvent(new window.Event('input')); };
  assert.equal(get('scene').options.length,9);
  assert.equal(get('mode').options.length,3);
  assert.equal(window.document.querySelectorAll('.cell').length,40);
  get('scene').selectedIndex=2; change('scene'); end();
  assert.equal(get('state').textContent,'ASLEEP'); assert(get('screen').classList.contains('off'));
  get('scene').selectedIndex=1; change('scene'); end();
  assert.equal(get('state').textContent,'AWAKE'); assert(!get('screen').classList.contains('off'));
  assert.match(get('row0').textContent,/14:35:02/);
  for (const mode of ['关闭','轻量','标准']) {
    get('mode').value=mode; change('mode');
    get('scene').selectedIndex=4; change('scene'); end();
    assert.match(get('row1').textContent,/T:24.6C RH:51.2%/);
  }
  get('scene').selectedIndex=6; change('scene');
  get('timeline').value=2000; get('timeline').dispatchEvent(new window.Event('input'));
  assert.match(get('row1').textContent,/^记得喝水/);
  get('scene').selectedIndex=7; change('scene'); end();
  assert.match(get('row1').textContent,/T:24.6C/);
  get('scene').selectedIndex=8; change('scene'); end();
  assert.match(get('row1').textContent,/2026-10-21 WED/);
  console.log('PASS: generated animation preview scenes, motion modes, scrubber, asleep/wake state and final data');
} finally { dom.window.close(); }
