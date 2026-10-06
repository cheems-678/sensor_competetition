/* Local QA only. No real UART, probes, or sensor access. */
const http = require('node:http');
const {readPage} = require('./check_slave_web_page.cjs');
const page = require('node:zlib').gzipSync(Buffer.from(readPage()),{level:9});
const fanDuties=[0,25,50,100],requests=new Map();
let active=null;
function settleControls(){if(active&&Date.now()-active.started>=500){active.state='success';if(active.type==='fan')fanDuties[active.channel-1]=active.value;active=null;}}
let seq = 0;
const started = Date.now();
http.createServer((req, res) => {
  settleControls();
  if(req.url==='/api/control'&&req.method==='POST'){let body='';req.on('data',b=>{body+=b;if(body.length>96)req.destroy();});req.on('end',()=>{try{const c=JSON.parse(body);if(!/^[a-f0-9]{16}$/.test(c.request_id)||!['fan','window'].includes(c.type)||!Number.isInteger(c.channel)||c.channel<1||c.channel>4||!Number.isInteger(c.value)||c.value<0||c.value>(c.type==='fan'?100:1))throw Error();let old=requests.get(c.request_id);if(!old){old={...c,state:active?'busy':'queued',started:Date.now()};requests.set(c.request_id,old);if(!active)active=old;}res.writeHead(old.state==='busy'?409:202,{'Content-Type':'application/json'});res.end(JSON.stringify({request_id:c.request_id,state:old.state}));}catch(error){res.writeHead(400);res.end('{}');}});return;}
  if(req.url.startsWith('/api/control?id=')){const id=req.url.slice(16),c=requests.get(id);res.writeHead(c?200:404,{'Content-Type':'application/json'});res.end(JSON.stringify({request_id:id,state:c?c.state:'unknown'}));return;}
  if (req.url === '/api/telemetry') {
    seq++;
    const valid = true;
    res.writeHead(200, {'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store'});
    res.end(JSON.stringify({device: 'slave', group: 1, uptime_ms: Date.now() - started,
      sample_seq: seq, age_ms: valid ? 20 : null, valid,
      temperature_x10: valid ? 253 + Math.round(Math.sin(seq / 5) * 8) : null,
      humidity_x10: valid ? 462 + Math.round(Math.cos(seq / 5) * 15) : null,
      pressure_pa: valid ? 101325 : null,
      mq2: {valid: true, sample_seq: seq * 10, age_ms: 10, raw: 1550,
        pa7_mv: 1249, ao_mv: 2498},
      ultrasonic: {valid: true, distance_mm: 300, raw_mm: 302, pulse_us: 1761,
        filter_count: 5, sample_seq: seq * 10, age_ms: 10, error: 0},
      master: {online: true, rx_age_ms: 50, uptime_ms: Date.now() - started,
        sample_seq: seq, valid: true, age_ms: 100,
        temperature_x10: 238, humidity_x10: 530, pressure_pa: 101250,
        rain: 0, dark: 1, light_on: 1, fan_pwm: fanDuties},
      windows: [{state: 5,pulse_us: 1500,error: 0}, {state: 6,pulse_us: 1700,error: 0},
        {state: 5,pulse_us: 1500,error: 0}, {state: 4,pulse_us: 0,error: 2}]}));
  } else if (req.url === '/') {
    res.writeHead(200, {'Content-Type': 'text/html; charset=utf-8','Content-Encoding':'gzip'}); res.end(page);
  } else {res.writeHead(404); res.end();}
}).listen(8766, '127.0.0.1', () => console.log('QA preview: http://127.0.0.1:8766 (simulated data)'));
