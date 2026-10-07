const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

function readPage() {
  const source = fs.readFileSync(path.join(__dirname, '../App/slave_web_page.h'), 'ascii');
  const literals = source.match(/"(?:[^"\\]|\\.)*"/g);
  assert(literals && literals.length > 0);
  const bytes = literals.map(s => JSON.parse(s.replace(/\\([0-7]{3})/g,
    (_, digits) => '\\u00' + parseInt(digits, 8).toString(16).padStart(2, '0')))).join('');
  return require('node:zlib').gunzipSync(Buffer.from(bytes, 'latin1')).toString('utf8');
}

async function check() {
  const page = readPage();
  assert.equal(page, fs.readFileSync(path.join(__dirname, '../App/slave_web_page.html'), 'utf8'));
  assert(page.includes('从机环境监测') && !page.includes('\ufffd'));
  assert(!/<(?:script|link)[^>]+(?:src|href)=/i.test(page));
  assert(page.includes('@media(max-width:600px)'));
  const script = page.match(/<script>([\s\S]*?)<\/script>/)[1];
  const nodes = {};
  const canvas = new Proxy({createRadialGradient:()=>({addColorStop(){}})}, {get: (target, key) => target[key] || (() => {})});
  const writes = {};
  const get = id => {
    if (!nodes[id]) {
      let value = '--'; writes[id] = 0;
      let cls = ''; writes[id + 'Class'] = 0;
      nodes[id] = {value: id.startsWith('fan')?'0':'temperature', style:{},dataset:{},hidden:false,setAttribute(){},removeAttribute(){},setPointerCapture(){},clientWidth:700, handlers:{},addEventListener(event,fn){this.handlers[event]=fn;}, getContext:()=>canvas};
      Object.defineProperty(nodes[id], 'textContent', {get: () => value, set: next => {value = next; writes[id]++;}});
      Object.defineProperty(nodes[id], 'className', {get: () => cls, set: next => {cls = next; writes[id + 'Class']++;}});
    }
    return nodes[id];
  };
  let current = {uptime_ms: 1400, sample_seq: 1, age_ms: 100, valid: true,
    temperature_x10: -125, humidity_x10: 0, pressure_pa: 100123,
    mq2: {valid: true, age_ms: 100, sample_seq: 123, raw: 2048, pa7_mv: 1650, ao_mv: 3301}};
  const master = {online: true, rx_age_ms: 50, uptime_ms: 1200, sample_seq: 1,
    valid: true, age_ms: 100, temperature_x10: 231, humidity_x10: 550, pressure_pa: 101325,
    rain: 0, dark: 1, light_on: 0, fan_pwm: [0, 25, null, 100]};
  current.master = {...master};
  current.windows = [{state: 5, pulse_us: 1500, error: 0}, {state: 6, pulse_us: 1700, error: 0},
    {state: 4, pulse_us: 0, error: 2}, {state: 0, pulse_us: 0, error: 0}];
  let clock = 0, inflight = 0, maxInflight = 0, mode = 'normal', httpDelay = 0;
  let nextId = 0, controlMode='success', controlId=null; const controlCalls=[];
  const timers = new Map(), intervals = [];
  const context = vm.createContext({document: {getElementById: get,querySelectorAll:()=>[],hidden:false},
    window: {devicePixelRatio: 1, addEventListener() {}}, performance: {now: () => clock},
    crypto:require('node:crypto').webcrypto,AbortController, Date, console,
    setTimeout: (fn, delay) => { const id = ++nextId; timers.set(id, {fn, delay}); return id; },
    clearTimeout: id => timers.delete(id), setInterval: fn => intervals.push(fn),
    fetch: async (url, options) => {const {signal}=options;
      inflight++; maxInflight = Math.max(maxInflight, inflight);
      try {
        if (mode === 'error') throw new Error('offline');
        if (mode === 'slow') await new Promise((resolve, reject) => {
          signal.addEventListener('abort', () => reject(new Error('abort')), {once: true});
        });
        if(url.startsWith('/api/control')){
          controlCalls.push({url,options});
          if(options.method==='POST'){
            const body=JSON.parse(options.body);assert(options.body.length<=96);assert.match(body.request_id,/^[a-f0-9]{16}$/);controlId=body.request_id;
            if(controlMode==='http_error')return {ok:false,status:400,json:async()=>({request_id:null,state:'failed',phase:'rejected',reason:'invalid_request'})};
            if(controlMode==='plain_error')return {ok:false,status:400,json:async()=>{throw new Error('plain error body')}};
            if(controlMode==='bad_success')return {ok:false,status:400,json:async()=>({request_id:controlId,state:'success',reason:'none'})};
            if(controlMode==='id_error')return {ok:true,status:202,json:async()=>({request_id:'0000000000000000',state:'queued'})};
            return {ok:true,status:202,json:async()=>({request_id:controlId,state:'queued',phase:'radio_queued',reason:'none'})};
          }
          return {ok:true,status:200,json:async()=>({request_id:controlId,state:controlMode,phase:controlMode==='waiting'?'awaiting_result':'finished',reason:controlMode==='failed'?'device_rejected':'none'})};
        }
        clock += httpDelay;
        return {ok: true, json: async () => current};
      } finally { inflight--; }
    }});
  vm.runInContext(script, context, {timeout: 1000});
  const settle = () => new Promise(resolve => setImmediate(resolve));
  async function poll() {
    const pending = [...timers.entries()].filter(([, t]) => t.delay !== 1800);
    assert.equal(pending.length, 1, 'exactly one next poll');
    const [id, t] = pending[0]; timers.delete(id); await t.fn(); await settle();
  }
  await settle();
  assert.equal(get('masterTemperature').textContent, '23.1');
  assert.equal(get('masterHumidity').textContent, '55.0');
  assert.equal(get('masterPressure').textContent, '101.33');
  assert.equal(get('temperature').textContent, '-12.5');
  assert.equal(get('humidity').textContent, '0.0');
  assert.equal(get('pressure').textContent, '100.12');
  assert.equal(get('mq2').textContent, '3.301');
  assert.equal(get('fan1').textContent, '0'); assert.equal(get('fan3').textContent, '--');
  assert.equal(get('window1').textContent, '停止'); assert.equal(get('window2').textContent, '动作中');
  assert.equal(get('window3').textContent, '故障 · PWM更新失败'); assert.equal(get('window4').textContent, '未启动');
  const sameWrites = {...writes}; await poll();
  for (const id of ['masterTemperature','masterStatus','masterStatusClass','slaveStatus','slaveStatusClass','temperature','humidity','mq2','status','statusClass','window2'])
    assert.equal(writes[id], sameWrites[id], id + ' is not rewritten for identical data');
  assert.equal(vm.runInContext('trend.filter(p=>p.side==="slave").map(p=>p.value).length', context), 1, 'duplicates do not add chart samples');
  const step = ms => {clock += ms; intervals.forEach(fn => fn());};
  const fresh = (age = 100) => {
    mode = 'normal'; current = {...current, uptime_ms: clock + 10000, sample_seq: current.sample_seq + 1,
      age_ms: age, valid: true, temperature_x10: 260, humidity_x10: 450, pressure_pa: 101325,
      mq2: {valid: true, age_ms: age, sample_seq: 124, raw: 0, pa7_mv: 0, ao_mv: 0},
      master: {...master, age_ms: age, rx_age_ms: 0}};
  };
  // Thirty phase-shift cycles cover both numbers and status badges, including single HTTP failures.
  for (let cycle = 0; cycle < 30; cycle++) {
    fresh(1900); await poll();
    const masterBadge = get('masterStatus').textContent, slaveBadge = get('slaveStatus').textContent,
      headerBadge = get('status').textContent, chartLength = vm.runInContext('trend.filter(p=>p.side==="slave").map(p=>p.value).length', context);
    step(150);
    assert.equal(get('masterTemperature').textContent, '23.1');
    assert.equal(get('temperature').textContent, '26.0'); assert.equal(get('mq2').textContent, '0.000');
    assert.equal(get('masterStatus').textContent, masterBadge, 'master badge does not flap at age 2000');
    assert.equal(get('slaveStatus').textContent, slaveBadge, 'slave badge does not flap at age 2000');
    current = {...current, valid: false, age_ms: null,
      master: {...master, online: false, rx_age_ms: null, valid: false, age_ms: null},
      mq2: {valid: false, age_ms: null}};
    await poll(); step(400); mode = 'error'; await poll();
    assert.equal(get('temperature').textContent, '26.0', 'invalid response/HTTP failure retains BME');
    assert.equal(get('mq2').textContent, '0.000'); assert.equal(get('window2').textContent, '动作中');
    assert.equal(get('masterStatus').textContent, masterBadge);
    assert.equal(get('slaveStatus').textContent, slaveBadge);
    assert.equal(get('status').textContent, headerBadge, 'one HTTP failure cannot flash the header');
    assert.equal(vm.runInContext('trend.filter(p=>p.side==="slave").map(p=>p.value).filter(v=>v===null).length', context) > 0, true, 'chart retains a real gap');
    assert(vm.runInContext('trend.filter(p=>p.side==="slave").map(p=>p.value).length', context) <= chartLength + 1);
    step(450); fresh(); await poll(); step(1000);
    assert.match(get('masterStatus').textContent, /LoRa 同步/);
    assert.match(get('slaveStatus').textContent, /数据正常/);
  }
  // Invalid responses cannot renew BME/MQ; fresh independent master working states still survive.
  fresh(); await poll();
  for (let i = 0; i < 4; i++) {
    step(1000); current = {...current, valid: false, age_ms: null,
      mq2: {valid: false, age_ms: null}, master: {...master, valid: false, age_ms: null, rx_age_ms: 0}};
    await poll(); assert.equal(get('temperature').textContent, '26.0');
    assert.equal(get('mq2').textContent, '0.000'); assert.equal(get('masterTemperature').textContent, '23.1');
  }
  assert.match(get('slaveStatus').textContent, /待更新/, 'persistent degradation changes badge after settling');
  assert.match(get('masterStatus').textContent, /待更新/);
  step(899); assert.equal(get('temperature').textContent, '26.0');
  step(1); assert.equal(get('temperature').textContent, '--'); assert.equal(get('mq2').textContent, '--');
  assert.equal(get('masterTemperature').textContent, '--'); assert.equal(get('fan4').textContent, '100');
  assert.match(get('slaveStatus').textContent, /过期/);
  assert.match(get('status').textContent, /已连接/, 'sensor failure does not imply Wi-Fi failure');
  fresh(); await poll(); assert.equal(get('temperature').textContent, '26.0');
  step(1000); fresh(); await poll(); step(1000); fresh(); await poll();
  assert.match(get('slaveStatus').textContent, /数据正常/, 'sustained recovery restores the badge');
  // Sensor lifetimes remain independent, with saturation and zero retained as real readings.
  current.mq2 = {valid: true, age_ms: 0, sample_seq: 125, raw: 4095, pa7_mv: 3300, ao_mv: 6600};
  current.valid = false; current.age_ms = null; await poll();
  assert.equal(get('mq2').textContent, '6.600'); assert.equal(get('temperature').textContent, '26.0');
  step(2000); current.mq2.age_ms = 0; await poll(); assert.match(get('mq2state').textContent, /满量程/);
  step(3000); current.mq2.age_ms = 0; await poll();
  assert.equal(get('temperature').textContent, '--'); assert.equal(get('mq2').textContent, '6.600');
  // A full connection loss must clear all values, windows and connection badges at five seconds.
  fresh(0); await poll(); mode = 'error'; await poll();
  step(2100); assert.equal(get('temperature').textContent, '26.0'); assert.equal(get('window2').textContent, '动作中');
  step(1000); assert.match(get('status').textContent, /连接延迟/);
  step(1899); assert.equal(get('temperature').textContent, '26.0'); assert.equal(get('mq2').textContent, '0.000');
  step(1);
  for (const id of ['temperature','mq2','masterTemperature','fan4','window2','pulse2']) assert.equal(get(id).textContent, '--', id + ' expires');
  assert.match(get('status').textContent, /数据中断/); assert.match(get('masterStatus').textContent, /离线/);
  // Repeated cache reads preserve original age rather than extending a sample's lifetime.
  fresh(1800); await poll(); step(100); current.age_ms = current.mq2.age_ms = current.master.age_ms = 1900;
  current.master.rx_age_ms = 100; await poll(); step(3099);
  assert.equal(get('temperature').textContent, '26.0'); step(1);
  assert.equal(get('temperature').textContent, '--'); assert.equal(get('mq2').textContent, '--');
  assert.equal(get('masterTemperature').textContent, '--'); assert.equal(get('fan4').textContent, '100');
  // HTTP round trip counts toward each sensor's deadline.
  fresh(); await poll();
  context.testMaster = {...master, age_ms: 1500, rx_age_ms: 0};
  vm.runInContext('updateMaster(testMaster,400)', context);
  step(3100); assert.equal(get('masterTemperature').textContent, '--');
  assert.equal(get('temperature').textContent, '26.0');
  fresh(1500); httpDelay = 400; await poll(); httpDelay = 0;
  assert.equal([...timers.values()].find(t=>t.delay!==1800).delay, 600, 'response duration does not add another full second');
  step(3099); assert.equal(get('temperature').textContent, '26.0');
  step(1); assert.equal(get('temperature').textContent, '--'); assert.equal(get('mq2').textContent, '--');
  assert.equal(get('masterTemperature').textContent, '--', 'HTTP duration counts toward all sensor ages');
  // Reboot must discard pre-reboot retained readings even when the first new sample is invalid.
  current = {...current, uptime_ms: 1, sample_seq: 0, valid: false, age_ms: null,
    mq2: {valid: false}, master: {online: false}, windows: []}; await poll();
  for (const id of ['temperature','mq2','masterTemperature','fan4','window2']) assert.equal(get(id).textContent, '--');
  assert.equal(vm.runInContext('trend.filter(p=>p.side==="slave").map(p=>p.value).filter(v=>v!==null).length', context), 0);
  fresh(); await poll(); assert.equal(vm.runInContext('trend.filter(p=>p.side==="slave").map(p=>p.value).filter(v=>v!==null).length', context), 1);
  // Interrupted samples create a gap but re-reading the same sample cannot append a new point.
  mode = 'error'; await poll(); mode = 'normal'; await poll();
  assert.equal(vm.runInContext('trend.filter(p=>p.side==="slave").map(p=>p.value).filter(v=>v!==null).length', context), 1);
  // Distance faults clear immediately, independently of the five-second BME hold.
  context.testUltrasonic={valid:true,distance_mm:299,raw_mm:300,pulse_us:1749,filter_count:5,sample_seq:7,age_ms:100};
  vm.runInContext('updateDistance(testUltrasonic,0)',context);
  assert.equal(get('distance').textContent,'29.9');assert.equal(get('distanceRaw').textContent,'30.0');
  assert.equal(get('distancePulse').textContent,'1749');
  vm.runInContext('updateDistance({...testUltrasonic,distance_mm:50,raw_mm:50,pulse_us:292},0)',context);
  assert.equal(get('distance').textContent,'5.0','5 cm is accepted');
  vm.runInContext('updateDistance({...testUltrasonic,distance_mm:49,raw_mm:49,pulse_us:285},0)',context);
  assert.equal(get('distance').textContent,'--','below 5 cm remains unavailable');
  vm.runInContext('updateDistance(testUltrasonic,0)',context);
  step(1899);assert.equal(get('distance').textContent,'29.9');
  step(1);assert.equal(get('distance').textContent,'--');
  vm.runInContext('updateDistance(testUltrasonic,0)',context);
  vm.runInContext('updateDistance({valid:false,error:4},0)',context);
  assert.equal(get('distance').textContent,'--');assert.equal(get('distanceState').textContent,'未收到有效回波');
  assert.equal(get('temperature').textContent,'26.0');
  vm.runInContext('updateDistance(testUltrasonic,1900)',context);
  assert.equal(get('distance').textContent,'--','HTTP age reaches distance deadline');
  vm.runInContext('updateDistance(testUltrasonic,0);resetSession()',context);
  assert.equal(get('distance').textContent,'--','reboot clears distance');
  fresh();await poll();
  mode = 'slow';
  const pending = [...timers.entries()].find(([, t]) => t.delay !== 1800);
  timers.delete(pending[0]); const slow = pending[1].fn(); await settle();
  assert.equal(inflight, 1); assert.equal([...timers.values()].filter(t=>t.delay===1000).length, 0);
  const abort = [...timers.values()].find(t=>t.delay===1800); step(1800); abort.fn(); await slow;
  assert.equal(inflight, 0); assert.equal(maxInflight, 1);
  assert.equal([...timers.values()].find(t=>t.delay!==1800).delay, 100, 'slow requests have a bounded pause and never overlap');
  assert.equal(get('temperature').textContent, '26.0', 'one timeout retains values');
  fresh(); await poll();
  // Explicit UI interaction only; a queued operation cannot be submitted twice.
  fresh();await poll();vm.runInContext("switchPage('fans')",context);
  get('fanRange4').value='75';get('fanRange4').handlers.input();assert.equal(get('fanInput4').value,'75');assert.equal(controlCalls.length,0);
  get('fanRange4').handlers.change();assert.equal(controlCalls.length,0);await poll();
  assert.equal(controlCalls.length,1);assert.equal(JSON.parse(controlCalls[0].options.body).channel,4);
  vm.runInContext("submitCommand('window',1,1)",context);assert.equal(vm.runInContext('commandDraft',context),null);
  step(1000);fresh();await poll();assert.equal(controlCalls.length,2);assert.match(get('fanResult4').textContent,/已确认/);
  controlMode='busy';vm.runInContext("submitCommand('window',2,1)",context);await poll();step(1000);fresh();await poll();assert.match(get('windowResult2').textContent,/忙/);
  controlMode='waiting';vm.runInContext("submitCommand('window',4,0)",context);await poll();const calls=controlCalls.length;step(8000);fresh();await poll();
  assert.match(get('windowResult4').textContent,/等待设备结果超时.*结果未知/);assert.equal(controlCalls.length,calls,'timeout is not retried');
  for(const page of ['overview','environment','distance','rain','smoke','fans','windows','logs'])vm.runInContext('switchPage("'+page+'")',context);
  assert.equal(controlCalls.length,calls,'navigation does not control devices');
  fresh();await poll();controlMode='success';const beforeKeys=controlCalls.length;
  get('fanRange1').handlers.keydown({key:'ArrowRight'});get('fanRange1').value='45';get('fanRange1').handlers.change();assert.equal(vm.runInContext('commandDraft',context),null,'range keydown cannot submit');
  get('fanRange1').handlers.keyup({key:'ArrowRight'});assert.equal(controlCalls.length,beforeKeys);await poll();step(1000);fresh();await poll();assert.match(get('fanResult1').textContent,/已确认/);
  get('fanInput1').value='';get('fanSend1').handlers.click();assert.equal(vm.runInContext('commandDraft',context),null,'blank numeric input does not stop a fan');
  vm.runInContext("submitCommand('fan',1,10)",context);step(1001);fresh();await poll();assert.match(get('fanResult1').textContent,/未发送/);assert.equal(controlCalls.length,beforeKeys+2,'expired browser draft is never sent');
  for(const error of ['http_error','plain_error','bad_success','id_error']){
    fresh();await poll();controlMode=error;const count=controlCalls.length;
    vm.runInContext("submitCommand('fan',2,75)",context);await poll();
    assert.match(get('fanResult2').textContent,error==='id_error'?/编号不匹配/:/HTTP 400/);
    assert(!get('fanResult2').textContent.includes('已确认'));step(1000);fresh();await poll();
    assert.equal(controlCalls.length,count+1,'failed HTTP submission is never retried or polled');
  }
  fresh();await poll();controlMode='failed';vm.runInContext("submitCommand('window',3,1)",context);await poll();
  assert.match(get('windowResult3').textContent,/等待无线发送/);step(1000);fresh();await poll();
  assert.match(get('windowResult3').textContent,/设备拒绝/);assert(get('logs').textContent.includes('device_rejected'));
  vm.runInContext("for(let i=0;i<1300;i++)record('master',10000+i,{temperature:i,humidity:50,pressure:100000})",context);
  assert.equal(vm.runInContext('trend.length',context),1200);step(600001);vm.runInContext('pruneTrend()',context);assert.equal(vm.runInContext('trend.filter(p=>p.value).length',context),0);
  vm.runInContext("switchPage('overview')",context);const zoom=vm.runInContext('modelView.zoom',context);get('zoomIn').handlers.click();assert(vm.runInContext('modelView.zoom',context)>zoom);get('resetModel').handlers.click();assert.equal(vm.runInContext('modelView.zoom',context),1);
  assert.equal(require('node:zlib').gunzipSync(fs.readFileSync(path.join(__dirname,'build/http_page.gz'))).toString('utf8'),page,'real HTTP gzip body equals the canonical HTML');
  const fixture = JSON.parse(fs.readFileSync(path.join(__dirname, 'build/combined_telemetry_fixture.json'), 'utf8'));
  assert.equal(fixture.ultrasonic.distance_mm,299);assert.equal(fixture.ultrasonic.raw_mm,300);
  assert.equal(fixture.master.temperature_x10, -234);
  assert.deepEqual(fixture.master.fan_pwm, [0, 25, 50, 100]);
  assert.equal(fixture.windows.length, 4); assert.equal(fixture.windows[1].state, 6);
  fs.writeFileSync(path.join(__dirname, 'build/slave_web_preview.html'), page);
  console.log('PASS: real embedded page, 30 all-panel no-flicker cycles, badge debounce/recovery, minimal DOM writes, independent source-age expiry, reboot, chart gaps and serial HTTP');
}
module.exports = {readPage};
if (require.main === module) check().catch(error => {console.error(error); process.exitCode = 1;});
