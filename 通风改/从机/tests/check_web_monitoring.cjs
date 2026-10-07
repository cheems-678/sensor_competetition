/* Execute the actual embedded script against a real DOM and virtual time. */
const assert=require('node:assert/strict');
const fs=require('node:fs');const path=require('node:path');
const {JSDOM}=require('../../上位机/frontend/node_modules/jsdom');
const {readPage}=require('./check_slave_web_page.cjs');
const vm=require('node:vm');
async function check(){
  const page=readPage(),dom=new JSDOM(page,{url:'http://192.168.4.1/',runScripts:'outside-only'}),w=dom.window;
  let clock=0,inflight=0,maxInflight=0,posts=0,canvasCalls=0;const timers=new Map(),downloads=[];let tid=0;
  w.performance.now=()=>clock;w.setTimeout=(fn,delay)=>{timers.set(++tid,{fn,delay});return tid};w.clearTimeout=id=>timers.delete(id);w.setInterval=()=>0;
  w.matchMedia=()=>({matches:false});w.HTMLCanvasElement.prototype.getContext=()=>{canvasCalls++;return new Proxy({createRadialGradient:()=>({addColorStop(){}})},{get:(t,k)=>t[k]||(()=>{})})};
  w.Blob=class{constructor(parts){this.parts=parts}};w.URL.createObjectURL=b=>{downloads.push(b.parts.join(''));return 'blob:test'};w.URL.revokeObjectURL=()=>{};w.HTMLAnchorElement.prototype.click=()=>{};
  const rect={top:0,bottom:500,width:700,height:360};w.HTMLElement.prototype.getBoundingClientRect=()=>rect;
  let data={uptime_ms:10000,sample_seq:1,age_ms:0,valid:true,temperature_x10:200,humidity_x10:500,pressure_pa:101325,
    master:{online:true,rx_age_ms:0,uptime_ms:10000,sample_seq:1,age_ms:0,valid:true,temperature_x10:220,humidity_x10:400,pressure_pa:101000,rain:0,fan_pwm:[0,80,90,100]},
    mq2:{valid:true,age_ms:0,sample_seq:1,raw:0,pa7_mv:0,ao_mv:0},
    ultrasonic:{valid:true,age_ms:0,sample_seq:1,distance_mm:100,raw_mm:100,pulse_us:583,filter_count:5},
    acoustic:{valid:true,age_ms:0,sample_seq:1,peak_to_peak:[0,100,101,4095,50]},windows:[]};
  w.fetch=async(url,opts)=>{inflight++;maxInflight=Math.max(maxInflight,inflight);try{if(opts?.method==='POST'){posts++;const c=JSON.parse(opts.body);return {ok:true,status:202,json:async()=>({request_id:c.request_id,state:'success',phase:'finished',reason:'none'})}}return {ok:true,status:200,json:async()=>data}}finally{inflight--}};
  const context=dom.getInternalVMContext();vm.runInContext(page.match(/<script>([\s\S]*?)<\/script>/)[1],context);await new Promise(r=>setImmediate(r));
  const get=id=>w.document.getElementById(id),evalJS=src=>vm.runInContext(src,context),value=id=>get(id).textContent;
  const click=id=>get(id).click();const advance=ms=>{clock+=ms;evalJS('monitorTick()')};
  const review=(values={})=>{data={...data,...values};w.testData=data;evalJS('lastSuccess=performance.now();networkInterrupted=false;reviewTelemetry(testData,0);monitorTick()')};
  assert.equal(get('masterPressure').textContent,'101.00');assert.equal(get('pressure').textContent,'101.33');
  assert.equal(w.document.querySelectorAll('nav button').length,10);assert.equal(value('acousticValue1'),'0');
  assert.equal(value('acousticState1'),'待设置阈值');
  ['0','100','100','','50'].forEach((v,i)=>get('acousticThreshold'+(i+1)).value=v);click('applyAcoustic');
  assert.equal(value('acousticState1'),'幅度未超限');assert.equal(value('acousticState2'),'幅度未超限');assert.equal(value('acousticState3'),'幅度超限');assert.equal(value('acousticState4'),'待设置阈值');
  get('acousticThreshold3').value='4096';click('applyAcoustic');assert.match(value('thresholdResult'),/0–4095/);assert.equal(evalJS('acousticThresholds[2]'),100);
  get('acousticThreshold3').value='1.5';click('applyAcoustic');assert.equal(evalJS('acousticThresholds[2]'),100);
  evalJS("switchPage('acoustic')");get('acousticModel').dispatchEvent(new w.KeyboardEvent('keydown',{key:'ArrowRight'}));assert(evalJS('acousticView.yaw')>.15);click('acousticIn');assert(evalJS('acousticView.zoom')>1);evalJS("switchPage('environment');switchPage('acoustic')");assert(evalJS('acousticView.zoom')>1);click('acousticReset');assert.equal(evalJS('acousticView.zoom'),1);
  const beforeHide=canvasCalls;Object.defineProperty(w.document,'hidden',{configurable:true,value:true});evalJS('renderAcoustic()');assert.equal(canvasCalls,beforeHide,'hidden page stops acoustic drawing');Object.defineProperty(w.document,'hidden',{configurable:true,value:false});
  const getRect=w.HTMLElement.prototype.getBoundingClientRect;w.HTMLElement.prototype.getBoundingClientRect=()=>({top:10000,bottom:10300});evalJS('renderAcoustic()');assert.equal(canvasCalls,beforeHide,'offscreen acoustic model does not draw');w.HTMLElement.prototype.getBoundingClientRect=getRect;
  const contextGetter=w.HTMLCanvasElement.prototype.getContext;w.HTMLCanvasElement.prototype.getContext=()=>null;evalJS('renderAcoustic()');assert.equal(value('acousticValue1'),'0','no canvas still shows data');w.HTMLCanvasElement.prototype.getContext=contextGetter;
  advance(1999);assert.equal(value('acousticValue1'),'0');advance(1);assert.equal(value('acousticValue1'),'--');assert.equal(value('acousticState3'),'数据不可用');
  review({acoustic:null});assert.equal(value('acousticValue3'),'--','old firmware is compatible');
  review({acoustic:{valid:true,age_ms:0,sample_seq:2,peak_to_peak:[0,1,2,3,null]}});assert.equal(value('acousticValue1'),'--','malformed channel invalidates whole window');
  review({master:{...data.master,rain:1},mq2:{...data.mq2,raw:850,pa7_mv:850},ultrasonic:{...data.ultrasonic,sample_seq:2,distance_mm:99}});
  assert.equal(evalJS('sensorChecks.rain.state'),'abnormal');assert.equal(evalJS('sensorChecks.smoke.state'),'abnormal');assert.equal(evalJS('sensorChecks.distance.state'),'abnormal');assert.equal(evalJS('warningEvents.length'),3);
  evalJS('monitorTick();monitorTick()');assert.equal(evalJS('warningEvents.length'),3,'persistent abnormal samples do not duplicate events');
  const freshDistance=(mm,seq)=>review({master:{...data.master,rain:0},mq2:{...data.mq2,raw:0,pa7_mv:0},ultrasonic:{...data.ultrasonic,sample_seq:seq,distance_mm:mm}});
  freshDistance(110,3);advance(1000);freshDistance(110,4);advance(1000);freshDistance(110,5);assert.equal(evalJS('sensorChecks.distance.state'),'abnormal');advance(1000);freshDistance(110,6);assert.equal(evalJS('sensorChecks.distance.state'),'normal');
  freshDistance(99,7);freshDistance(110,8);advance(1999);freshDistance(110,8);advance(1001);freshDistance(110,8);assert.equal(evalJS('sensorChecks.distance.state'),'abnormal','duplicate distance cannot confirm recovery');
  advance(2000);assert.equal(evalJS('sensorChecks.distance.state'),'unavailable');freshDistance(110,9);assert.equal(evalJS('sensorChecks.distance.state'),'abnormal','unavailable gap restarts recovery');
  freshDistance(100,10);assert.equal(evalJS('sensorChecks.distance.state'),'abnormal','hysteresis holds until 11cm');
  evalJS('resetMonitoring();warningEvents=[]');review();freshDistance(100,11);assert.equal(evalJS('sensorChecks.distance.state'),'normal','exactly 10cm does not trigger a new event');
  review({mq2:{...data.mq2,raw:550,pa7_mv:550}});assert.equal(evalJS('sensorChecks.smoke.current'),10);assert.equal(evalJS('sensorChecks.smoke.state'),'normal');
  evalJS('resetMonitoring();warningEvents=[]');review();
  const sample=(seconds,tempRate=.6)=>{clock=10000+seconds*1000;review({uptime_ms:clock+10000,master:{...data.master,uptime_ms:clock+10000},ultrasonic:{...data.ultrasonic,sample_seq:1000+seconds,distance_mm:300}});w.sample={temperature:20+seconds/60*tempRate,humidity:50,pressure:101325};evalJS('observeTrend("slave",sample);monitorTick()')};
  for(let s=0;s<=119;s++)sample(s);assert.equal(evalJS('buildTrendReport().checks.slave_temp.state'),'unavailable');
  sample(120);assert.equal(evalJS('buildTrendReport().checks.slave_temp.state'),'abnormal');assert.equal(evalJS('warningEvents.filter(e=>e.key==="slave_temp").length'),0,'automatic default is off');
  click('checkTrend');assert.match(value('trendReport'),/变化超限/);assert.equal(evalJS('warningEvents.find(e=>e.key==="slave_temp").trigger'),'manual');
  const frozen=JSON.parse(evalJS('JSON.stringify(lastReport)'));advance(1000);assert.equal(evalJS('lastReport.created_at'),frozen.created_at,'report stays frozen');
  click('exportReport');assert.equal(JSON.parse(downloads[0]).created_at,frozen.created_at);assert.equal(JSON.parse(downloads[0]).window_seconds,120);click('exportCsv');assert.match(downloads[1],/气压\(kPa\)/);
  evalJS('resetMonitoring();warningEvents=[];warningEnabled=true');for(let s=0;s<=149;s++)sample(s);assert.equal(evalJS('warningEvents.filter(e=>e.key==="slave_temp").length'),0);sample(150);assert.equal(evalJS('warningEvents.find(e=>e.key==="slave_temp").status'),'active');
  // Feed a flat window; recovery is 60 continuous seconds below 80%.
  for(let s=151;s<=330;s++){clock=10000+s*1000;review();w.sample={temperature:21.5,humidity:50,pressure:101325};evalJS('observeTrend("slave",sample);monitorTick()')}
  assert.equal(evalJS('warningEvents.find(e=>e.key==="slave_temp").status'),'resolved');
  advance(10001);assert.equal(evalJS('warningSeries.slave.length'),0);assert.equal(evalJS('buildTrendReport().checks.slave_temp.state'),'unavailable');
  get('tempRate').value='0';click('applyRates');assert.match(value('ruleResult'),/有限正数/);assert.equal(evalJS('warningRules.temp'),.4);
  // Controls remain serialized and only an explicit click can send.
  review({master:{...data.master,uptime_ms:999999,fan_pwm:[0,80,90,100]}});w.testMaster=data.master;evalJS('updateMaster(testMaster,0);refreshFanMotion();switchPage("fans")');
  assert.equal(posts,0);click('fanPreset1-100');click('fanPreset2-0');assert.equal(evalJS('commandDraft.channel'),1);await evalJS('poll()');assert.equal(posts,1);assert.match(value('fanLevel1'),/已确认PWM 100/);assert.equal(maxInflight,1);
  for(const id of ['overview','environment','acoustic','warnings','rain','smoke','fans','windows','logs','distance'])evalJS('switchPage("'+id+'")');assert.equal(posts,1);
  evalJS('monitorInterrupted();networkInterrupted=true;monitorTick()');assert.equal(evalJS('sensorChecks.smoke.state'),'unavailable');assert.equal(value('acousticValue1'),'--');
  const thresholds=evalJS('JSON.stringify(acousticThresholds)');evalJS('resetSession();monitorTick()');assert.equal(evalJS('JSON.stringify(acousticThresholds)'),thresholds,'device restart retains manual thresholds');assert.equal(evalJS('warningSeries.slave.length'),0);
  const fixture=JSON.parse(fs.readFileSync(path.join(__dirname,'build/acoustic_telemetry_fixture.json'),'utf8'));assert.deepEqual(fixture.acoustic.peak_to_peak,[4095,4095,4095,4095,4095]);assert.equal(fixture.acoustic.valid,true);
  dom.window.close();console.log('PASS: 10-page real DOM, acoustic thresholds/model/expiry/old firmware, sensor warning boundaries/hysteresis/duplicates, 120s/150s trend detection and recovery, frozen reports, presets/serial control, disconnect/reboot');
}
if(require.main===module)check().catch(e=>{console.error(e);process.exitCode=1});
