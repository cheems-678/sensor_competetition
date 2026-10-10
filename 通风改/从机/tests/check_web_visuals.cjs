/* Actual source and embedded browser scripts; virtual time, no hardware. */
const assert=require('node:assert/strict'),vm=require('node:vm');
const {JSDOM}=require('../../上位机/frontend/node_modules/jsdom');
const {readPage}=require('./check_slave_web_page.cjs');
const {readSource}=require('./web_page_source.cjs');
async function check(page,label){
  const dom=new JSDOM(page,{url:'http://192.168.4.1/',runScripts:'outside-only'}),w=dom.window;
  let clock=0,seq=0,tid=0,inflight=0,maxInflight=0,posts=0,fail=false,reduced=false,offscreen=false,canvasMissing=false;
  const intervals=[],timers=new Map(),frames=new Map(),calls=[];
  w.performance.now=()=>clock;w.Date.now=()=>1700000000000+clock;
  Object.defineProperty(w.document,'hidden',{configurable:true,value:false});
  w.setTimeout=(fn,delay)=>{timers.set(++tid,{fn,delay});return tid};w.clearTimeout=id=>timers.delete(id);w.setInterval=fn=>intervals.push(fn);
  w.requestAnimationFrame=fn=>{frames.set(++tid,fn);return tid};w.cancelAnimationFrame=id=>frames.delete(id);
  w.matchMedia=()=>({matches:reduced});w.Element.prototype.getBoundingClientRect=()=>({top:offscreen?10000:0,bottom:offscreen?10500:500});
  w.HTMLCanvasElement.prototype.getContext=function(){if(canvasMissing)return null;const id=this.id;return new Proxy({createRadialGradient:()=>({addColorStop(){}})},{get:(t,k)=>t[k]||((...args)=>{if(id==='chart')calls.push([k,...args])})})};
  const fresh=(mm=250,age=0)=>({uptime_ms:100000+clock,sample_seq:++seq,age_ms:age,valid:true,temperature_x10:250,humidity_x10:500,pressure_pa:101325,
    master:{online:true,rx_age_ms:0,uptime_ms:100000+clock,sample_seq:seq,age_ms:age,valid:true,temperature_x10:230+seq%3,humidity_x10:550,pressure_pa:101000,rain:0,fan_pwm:[0,80,90,100]},
    ultrasonic:{valid:true,age_ms:0,sample_seq:seq,distance_mm:mm,raw_mm:mm,pulse_us:Math.round(mm*5.83),filter_count:5,error:0},
    mq2:{valid:true,age_ms:0,sample_seq:seq,raw:0,pa7_mv:0,ao_mv:0},acoustic:{valid:true,age_ms:0,sample_seq:seq,peak_to_peak:[0,100,200,300,400]},windows:[]});
  let data=fresh();w.fetch=async(url,opts)=>{inflight++;maxInflight=Math.max(maxInflight,inflight);try{if(fail)throw Error('test network loss');if(opts?.method==='POST'){posts++;const c=JSON.parse(opts.body);return {ok:true,status:202,json:async()=>({request_id:c.request_id,state:'success'})}}return {ok:true,status:200,json:async()=>data}}finally{inflight--}};
  const ctx=dom.getInternalVMContext(),run=s=>vm.runInContext(s,ctx),get=id=>w.document.getElementById(id);
  const tick=ms=>{clock+=ms;intervals.forEach(fn=>fn())};
  const poll=async()=>{await run('poll()')};
  const frame=()=>{clock+=16;const pending=[...frames.values()];frames.clear();pending.forEach(fn=>fn(clock))};
  try{
    run(page.match(/<script>([\s\S]*?)<\/script>/)[1]);await new Promise(r=>setImmediate(r));run('switchPage("distance")');
    for(const mm of [49,50,100,110,500,501]){
      clock+=10;reduced=true;data=fresh(mm);await poll();const valid=mm>=50&&mm<=500;
      assert.equal(run('sensorChecks.distance.current'),valid?mm/10:null);
      assert.equal(get('distanceGrain').getAttribute('visibility'),valid?'visible':'hidden');
      assert.equal(get('distance').textContent,valid?(mm/10).toFixed(1):'--');
      if(valid){const y=run('distanceVisual.shown');assert(y>=110&&y<=215);assert.equal(Number(get('distanceScene').dataset.surfaceY),Number(y.toFixed(2)));assert.equal(get('distanceGrain').getAttribute('d'),`M140 ${y}H380V235H140Z`);assert.equal(get('distanceLine').getAttribute('d'),`M260 83V${y}`);assert.equal(get('distanceGrain').style.transform,'')}
    }
    data=fresh(250);data.ultrasonic.raw_mm=501;await poll();assert.equal(get('distance').textContent,'--');assert.equal(run('sensorChecks.distance.state'),'unavailable','raw out-of-range invalidates both display and warnings');
    for(const error of [1,2,3,4,5,6]){data=fresh();data.ultrasonic={valid:false,error};await poll();assert.equal(get('distanceSignal').getAttribute('visibility'),'hidden');assert.equal(get('distanceSurfaceLabel').textContent,'粮面未知')}
    clock+=10;data=fresh(100);await poll();tick(1999);assert.equal(get('distanceGrain').getAttribute('visibility'),'visible');tick(1);assert.equal(get('distanceGrain').getAttribute('visibility'),'hidden');assert.equal(get('distance').textContent,'--');
    reduced=false;data=fresh(100);await poll();const accepted=run('distanceVisual.target');clock+=10;data=fresh(101);await poll();assert.equal(run('distanceVisual.target'),accepted,'1mm does not move visual');assert.equal(get('distance').textContent,'10.1','deadband does not change data');
    clock+=10;data=fresh(103);await poll();assert(frames.size>0);assert.equal(run('distanceVisual.target'),110+53/450*105);frame();assert(run('distanceVisual.shown')>accepted);assert.equal(get('distanceLine').getAttribute('d'),'M260 83V'+run('distanceVisual.shown'));
    for(let i=0;i<100&&frames.size;i++)frame();assert.equal(frames.size,0,'animation settles');
    data=fresh(400);await poll();assert(frames.size>0);Object.defineProperty(w.document,'hidden',{configurable:true,value:true});w.document.dispatchEvent(new w.Event('visibilitychange'));assert.equal(frames.size,0);Object.defineProperty(w.document,'hidden',{configurable:true,value:false});
    offscreen=true;run('refreshDistanceVisual()');assert.equal(frames.size,0);offscreen=false;reduced=true;data=fresh(450);await poll();assert.equal(frames.size,0);assert.equal(run('distanceVisual.shown'),run('distanceVisual.target'));
    reduced=false;data=fresh(100);await poll();assert(frames.size>0);fail=true;await poll();assert.equal(frames.size,0);assert.equal(get('distance').textContent,'--');assert.equal(get('distanceGrain').getAttribute('visibility'),'hidden');fail=false;
    // Reproduce the reported six isolated points with 1900ms source age.
    run('resetSession();switchPage("environment")');clock+=10000;
    for(let i=0;i<6;i++){data=fresh(250,1900);await poll();tick(250);if(i<5)tick(750)}
    calls.length=0;run('draw()');assert.equal(run('trend.filter(p=>p.side==="master").length'),6);assert.equal(calls.filter(c=>c[0]==='lineTo').length-4,10,'five actual links per source');assert.equal(calls.filter(c=>c[0]==='arc').length,2,'dense sample dots suppressed');
    assert.equal(run('trend.filter(p=>p.side==="master")[0].at'),clock-5250-1900);assert.equal(get('trendWindow').textContent,'最近1分钟');
    const before=run('trend.length');await poll();assert.equal(run('trend.length'),before,'duplicate source sequence is ignored');
    const firstEpoch=run('trend[0].epoch');w.Date.now=()=>1800000000000+clock;assert(run('trendCsv()').includes(new Date(firstEpoch).toISOString()),'export preserves point timestamp after system clock change');
    assert(calls.some(c=>c[0]==='fillText'&&c[1]==='30秒前'),'one-minute midpoint is 30 seconds');
    run('trend=[];trendIds={master:null,slave:null}');w.sample={temperature:20,humidity:50,pressure:100000};run('record("master",99999,sample)');
    for(const [delta,minutes] of [[60001,2],[60000,5],[180000,10]]){clock+=delta;run('draw()');assert.equal(get('trendWindow').textContent,'最近'+minutes+'分钟')}
    run('trend=[];trendIds={master:null,slave:null}');w.sample={temperature:20,humidity:50,pressure:100000};
    run('record("master",100000,sample)');tick(3000);run('record("master",100001,sample)');tick(3001);run('record("master",100002,sample)');calls.length=0;run('draw()');assert.equal(calls.filter(c=>c[0]==='lineTo').length-4,1,'exact 3sec connects; 3001ms breaks');
    const held=run('trend.length');fail=true;await poll();fail=false;assert.equal(run('trend.length'),held,'request failures do not add invented observations');
    run('trend=[];trendIds={master:null,slave:null}');for(let i=0;i<1300;i++){clock++;run(`record("master",${200000+i},sample)`)}assert.equal(run('trend.length'),1200);for(let i=0;i<100;i++)run('recordGap("master")');assert.equal(run('trend.length'),1200);tick(600001);run('pruneTrend()');assert.equal(run('trend.length'),0);
    canvasMissing=true;run('draw()');assert.match(get('empty').textContent,/绘制不可用/);data=fresh();await poll();assert.equal(get('temperature').textContent,'25.0');assert.equal(run('networkInterrupted'),false,'canvas failure is not network failure');canvasMissing=false;
    // Five simulated minutes: expiry between polls, one failed poll per 23s,
    // all 4 fan and 4 window commands; rendering never overlaps HTTP.
    run('resetSession()');const postStart=posts;
    for(let s=0;s<300;s++){tick(1000);data=fresh(250,1900);fail=s%23===22;await poll();fail=false;if(s%30===0&&s<240){clock+=1;data=fresh();await poll();const n=s/30;run(`submitCommand("${n<4?'fan':'window'}",${n%4+1},${n<4?80:1})`);await poll()}}
    assert.equal(posts-postStart,8);assert.equal(maxInflight,1);assert.equal(run('trend.some(p=>p.value===null)'),false);assert(run('trend.length')>500);assert.equal(frames.size,0,'distance page hidden during monitoring');
    console.log(`PASS ${label}: distance geometry/faults/expiry/motion, source-time 3sec lines/duplicates/history/CSV, canvas fallback, 5min serial monitoring and 8 controls`);
  }finally{dom.window.close()}
}
(async()=>{await check(readSource(),'source');await check(readPage(),'embedded minified')})().catch(e=>{console.error(e);process.exitCode=1});
