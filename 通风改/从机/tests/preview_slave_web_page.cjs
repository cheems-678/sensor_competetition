/* Local QA only. No real UART, probes, or sensor access. */
const http = require('node:http');
const {readPage} = require('./check_slave_web_page.cjs');
const page = readPage();
let seq = 0;
const started = Date.now();
http.createServer((req, res) => {
  if (req.url === '/api/telemetry') {
    seq++;
    const valid = seq % 20 !== 0;
    res.writeHead(200, {'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store'});
    res.end(JSON.stringify({device: 'slave', group: 1, uptime_ms: Date.now() - started,
      sample_seq: seq, age_ms: valid ? 20 : null, valid,
      temperature_x10: valid ? 253 + Math.round(Math.sin(seq / 5) * 8) : null,
      humidity_x10: valid ? 462 + Math.round(Math.cos(seq / 5) * 15) : null,
      pressure_pa: valid ? 101325 : null,
      mq2: {valid: true, sample_seq: seq * 10, age_ms: 10, raw: 1550,
        pa7_mv: 1249, ao_mv: 2498}}));
  } else if (req.url === '/') {
    res.writeHead(200, {'Content-Type': 'text/html; charset=utf-8'}); res.end(page);
  } else {res.writeHead(404); res.end();}
}).listen(8766, '127.0.0.1', () => console.log('QA preview: http://127.0.0.1:8766 (simulated data)'));
