'use strict';
(() => {
  const byId = id => document.getElementById(id);
  const form = byId('settings');
  const fields = byId('fields');
  const result = byId('result');
  let csrf = '', loaded = false, saving = false, messageId = 0, ackBusy = false, mqttLoaded = false, mqttSaving = false, mqttWritable = false;
  let rotationLoaded = false, rotationSaving = false, rotationWritable = false;
  const contentPages = ['date','indoor','temperature','humidity'];
  const notice = (text, state = '') => { result.textContent = text; result.className = state; };
  async function request(path, options = {}) {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 8000);
    try {
      const response = await fetch(path, {credentials: 'same-origin', cache: 'no-store', ...options, signal: controller.signal});
      if (response.status === 401) throw new Error('登录已失效，请刷新页面并重新登录。');
      const data = await response.json();
      if (!response.ok) throw new Error(data.error || '设备暂时无法处理请求。');
      return data;
    } finally { clearTimeout(timeout); }
  }
  function passwordMode() {
    const replacing = byId('password-mode').value === 'replace';
    byId('password-field').hidden = !replacing;
    byId('password').required = replacing;
    byId('password').disabled = !replacing;
  }
  function mqttSecretMode(key) {
    const replacing = byId(`mqtt-${key}-mode`).value === 'replace';
    byId(`mqtt-${key}-field`).hidden = !replacing;
    byId(`mqtt-${key}`).disabled = !replacing;
    byId(`mqtt-${key}`).required = replacing;
  }
  function loadMqtt(mqtt) {
    for (const key of ['host','port','topic','username']) byId(`mqtt-${key}`).value = String(mqtt[key]);
    byId('mqtt-enabled').value = mqtt.enabled ? '1' : '0';
    byId('mqtt-tls').value = mqtt.tls ? '1' : '0';
    for (const [key, isSet] of [['client-id',mqtt.clientIdSet],['password',mqtt.passwordSet]]) {
      byId(`mqtt-${key}`).value = '';
      byId(`mqtt-${key}-mode`).value = isSet ? 'keep' : 'clear';
      byId(`mqtt-${key}-note`).textContent = isSet ? '已保存；为保护凭据，不回显。' : '尚未保存。';
      mqttSecretMode(key);
    }
    mqttLoaded = true;
  }
  function showState(state) {
    csrf = state.csrf;
    rotationWritable = Boolean(state.storageReady);
    if (!rotationLoaded && state.rotation) {
      for (const [index, page] of contentPages.entries()) byId(`page-${page}`).checked = Boolean(state.rotation.pages & (1 << index));
      byId('rotation-hold').value = String(state.rotation.hold);
      rotationLoaded = true;
    }
    if (!rotationSaving) byId('rotation-fields').disabled = !rotationWritable || !rotationLoaded;
    const pageLabels = {date:'日期与星期',indoor:'室内温湿度',temperature:'独立温度',humidity:'独立湿度'};
    byId('rotation-status').textContent = `${pageLabels[state.contentPage] || '等待设备'}${state.rotationPaused ? ' · 已暂停' : ''}`;
    if (!mqttLoaded && state.mqtt) loadMqtt(state.mqtt);
    mqttWritable = Boolean(state.storageReady);
    if (!mqttSaving) byId('mqtt-fields').disabled = !state.storageReady || !mqttLoaded;
    const mqttLabels = {disabled:'MQTT 未配置', invalid:'MQTT 配置无效', waiting_wifi:'等待 Wi-Fi 连接', waiting_time:'等待校时后建立安全连接', connecting:'正在连接消息服务器', online:'消息服务已连接', retrying:'消息连接失败，稍后自动重试', unavailable:'消息服务不可用'};
    byId('mqtt-status').textContent = mqttLabels[state.mqttState] || 'MQTT 未配置';
    messageId = Number(state.messageId) || 0;
    byId('ack-message').disabled = ackBusy || !state.messageVisible || !messageId;
    byId('message-status').textContent = `未确认 ${Number(state.messageCount) || 0} 条 · 拒收 ${Number(state.messagesDropped) || 0} 条 · 合并重复 ${Number(state.messagesDuplicate) || 0} 条`;

    byId('connection').textContent = state.connected ? '网络已连接 · 设置可随时调整' : state.setupActive ? '配网热点已开启 · 等待连接家庭网络' : '网络离线 · 时钟继续运行';
    byId('network-label').textContent = state.connected ? 'ONLINE' : 'OFFLINE';
    byId('clock').textContent = state.clock || '--:--:--';
    byId('temperature').textContent = state.temperature === null ? '—' : `${state.temperature.toFixed(1)}°C`;
    byId('humidity').textContent = state.humidity === null ? '—' : `${state.humidity.toFixed(1)}%`;
    byId('sensor-note').textContent = !state.sensorEnabled ? '温湿度传感器未启用' : state.temperature === null ? '等待 AHT10 首次有效读数' : state.sampleFresh ? 'AHT10 / 数据正常' : '读取暂不可用，显示最近有效数据';
    byId('address').textContent = `IP ${state.ip || '192.168.4.1（热点）'}`;
    const minutes = Math.floor(state.uptime / 60);
    byId('uptime').textContent = `运行 ${Math.floor(minutes / 60)}h ${minutes % 60}m`;
    if (!loaded) {
      byId('ssid').value = state.ssid;
      byId('brightness').value = String(state.brightness);
      byId('offset').value = String(state.offset);
      byId('ntp').value = state.ntp;
      byId('password-mode').value = state.ssid ? 'keep' : 'replace';
      passwordMode(); loaded = true;
    }
    if (!saving) fields.disabled = !state.storageReady;
    if (!state.storageReady) notice('设备存储不可用，当前无法保存设置。', 'error');
  }
  async function poll() {
    try { showState(await request('/api/state')); }
    catch (error) {
      byId('connection').textContent = '暂时无法连接设备；切换网络后请打开设备的新地址。';
      byId('network-label').textContent = 'UNREACHABLE';
      byId('ack-message').disabled = true; messageId = 0;
      byId('sensor-note').textContent = '连接中断，显示最近读取的状态';
      if (!loaded) notice(error.message, 'error');
    } finally { setTimeout(poll, 3000); }
  }
  byId('ack-message').addEventListener('click', async () => {
    if (ackBusy || !messageId) return;
    const id = messageId;
    ackBusy = true; byId('ack-message').disabled = true; byId('ack-result').className = '';
    try {
      await request('/api/messages/ack', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded', 'X-CSRF-Token':csrf}, body:new URLSearchParams({id:String(id)})});
      byId('ack-result').textContent = '确认请求已提交，请以屏幕反馈和未确认数量为准。';
    } catch (error) {
      byId('ack-result').textContent = error.name === 'AbortError' ? '未收到确认结果，请等待状态刷新后再核对。' : error.message;
      byId('ack-result').className = 'error';
    } finally { ackBusy = false; }
  });
  byId('password-mode').addEventListener('change', passwordMode);
  byId('toggle-password').addEventListener('click', () => {
    const show = byId('password').type === 'password';
    byId('password').type = show ? 'text' : 'password';
    byId('toggle-password').textContent = show ? '隐藏' : '显示';
    byId('toggle-password').setAttribute('aria-pressed', String(show));
  });
  form.addEventListener('submit', async event => {
    event.preventDefault();
    if (saving || !loaded || !form.reportValidity()) return;
    if (new TextEncoder().encode(byId('ssid').value).length > 32) { notice('Wi-Fi 名称超过 32 字节，请缩短后重试。', 'error'); return; }
    const mode = byId('password-mode').value;
    const body = new URLSearchParams({ssid: byId('ssid').value, password_mode: mode,
      password: mode === 'replace' ? byId('password').value : '', brightness: byId('brightness').value,
      offset: byId('offset').value, ntp: byId('ntp').value});
    saving = true; fields.disabled = true; byId('save').textContent = '正在保存…'; notice('正在写入设备，请稍候。');
    try {
      await request('/api/settings', {method: 'POST', headers: {'Content-Type': 'application/x-www-form-urlencoded', 'X-CSRF-Token': csrf}, body});
      byId('password').value = ''; byId('password-mode').value = 'keep'; passwordMode();
      notice('已保存。若更换了网络，设备正在尝试连接；连接失败后会重新开放配网热点。', 'success');
    } catch (error) {
      notice(error.name === 'AbortError' ? '未收到保存结果。请重新连接设备并刷新页面，核对配置后再操作。' : error.message, 'error');
    } finally {
      saving = false; fields.disabled = false; byId('save').textContent = '保存并应用 ↗';
    }
  });
  for (const key of ['client-id','password']) byId(`mqtt-${key}-mode`).addEventListener('change', () => mqttSecretMode(key));
  byId('mqtt-settings').addEventListener('submit', async event => {
    event.preventDefault();
    const mqttForm = byId('mqtt-settings'), mqttResult = byId('mqtt-result');
    const notify = (text, state = '') => { mqttResult.textContent = text; mqttResult.className = state; };
    if (mqttSaving || !mqttLoaded || !mqttForm.reportValidity()) return;
    const clientMode = byId('mqtt-client-id-mode').value, passwordMode = byId('mqtt-password-mode').value;
    const values = {enabled:byId('mqtt-enabled').value, host:byId('mqtt-host').value, port:byId('mqtt-port').value,
      tls:byId('mqtt-tls').value, topic:byId('mqtt-topic').value, username:byId('mqtt-username').value,
      client_id_mode:clientMode, client_id:clientMode === 'replace' ? byId('mqtt-client-id').value : '',
      password_mode:passwordMode, password:passwordMode === 'replace' ? byId('mqtt-password').value : ''};
    for (const key of ['topic','username','client_id','password']) {
      if (new TextEncoder().encode(values[key]).length > 64) { notify('主题、用户名及凭据各最多 64 字节。', 'error'); return; }
    }
    const body = new URLSearchParams(values);
    mqttSaving = true; byId('mqtt-fields').disabled = true; byId('mqtt-save').textContent = '正在保存…';
    notify('正在保存消息设置。');
    try {
      await request('/api/mqtt', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded','X-CSRF-Token':csrf}, body});
      // Clear secrets immediately; the next status read reloads canonical settings.
      byId('mqtt-client-id').value = ''; byId('mqtt-password').value = '';
      mqttLoaded = false;
      notify('已保存，消息连接将自动更新。连接状态以设备反馈为准。', 'success');
      try { showState(await request('/api/state')); }
      catch (_) { notify('已保存，但暂时无法刷新设备状态；稍后会自动重试。', 'success'); }
    } catch (error) {
      notify(error.name === 'AbortError' ? '未收到保存结果，请刷新页面核对消息配置后再操作。' : error.message, 'error');
    } finally {
      mqttSaving = false; byId('mqtt-fields').disabled = !mqttWritable || !mqttLoaded;
      byId('mqtt-save').textContent = '保存消息设置 ↗';
    }
  });
  byId('rotation-settings').addEventListener('submit', async event => {
    event.preventDefault();
    const rotationForm = byId('rotation-settings'), rotationResult = byId('rotation-result');
    const notify = (text, state = '') => { rotationResult.textContent = text; rotationResult.className = state; };
    if (rotationSaving || !rotationLoaded || !rotationForm.reportValidity()) return;
    let pages = 0;
    for (const [index, page] of contentPages.entries()) if (byId(`page-${page}`).checked) pages |= 1 << index;
    if (!pages) { notify('请至少选择一项显示内容。', 'error'); return; }
    const body = new URLSearchParams({pages:String(pages),hold:byId('rotation-hold').value});
    rotationSaving = true; byId('rotation-fields').disabled = true; byId('rotation-save').textContent = '正在保存…';
    notify('正在保存轮播设置。');
    try {
      await request('/api/rotation', {method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-CSRF-Token':csrf},body});
      rotationLoaded = false;
      notify('已保存。普通显示将从所选第一项开始轮播。', 'success');
      try { showState(await request('/api/state')); }
      catch (_) { notify('已保存，但暂时无法刷新设备状态；稍后会自动重试。', 'success'); }
    } catch (error) {
      notify(error.name === 'AbortError' ? '未收到保存结果，请刷新页面核对轮播设置后再操作。' : error.message, 'error');
    } finally {
      rotationSaving = false; byId('rotation-fields').disabled = !rotationWritable || !rotationLoaded;
      byId('rotation-save').textContent = '保存轮播设置 ↗';
    }
  });
  poll();
})();
