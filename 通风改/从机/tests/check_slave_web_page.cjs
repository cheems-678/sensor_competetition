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
  return Buffer.from(bytes, 'latin1').toString('utf8');
}

async function check() {
  const page = readPage();
  assert(page.includes('从机环境监测') && !page.includes('\ufffd'));
  assert(!/<(?:script|link)[^>]+(?:src|href)=/i.test(page));
  assert(page.includes('@media(max-width:600px)'));
  const script = page.match(/<script>([\s\S]*?)<\/script>/)[1];
  const nodes = {};
  const canvas = new Proxy({}, {get: (target, key) => target[key] || (() => {})});
  const get = id => nodes[id] ||= {textContent: '--', className: '', value: 'temperature',
    clientWidth: 700, addEventListener() {}, getContext: () => canvas};
  let current = {uptime_ms: 1400, sample_seq: 1, age_ms: 100, valid: true,
    temperature_x10: -125, humidity_x10: 0, pressure_pa: 100123,
    mq2: {valid: true, age_ms: 100, sample_seq: 123, raw: 2048, pa7_mv: 1650, ao_mv: 3301}};
  let clock = 0, inflight = 0, maxInflight = 0, mode = 'normal';
  let nextId = 0;
  const timers = new Map(), intervals = [];
  const context = vm.createContext({document: {getElementById: get},
    window: {devicePixelRatio: 1, addEventListener() {}}, performance: {now: () => clock},
    AbortController, Date, console,
    setTimeout: (fn, delay) => { const id = ++nextId; timers.set(id, {fn, delay}); return id; },
    clearTimeout: id => timers.delete(id), setInterval: fn => intervals.push(fn),
    fetch: async (_, {signal}) => {
      inflight++; maxInflight = Math.max(maxInflight, inflight);
      try {
        if (mode === 'error') throw new Error('offline');
        if (mode === 'slow') await new Promise((resolve, reject) => {
          signal.addEventListener('abort', () => reject(new Error('abort')), {once: true});
        });
        return {ok: true, json: async () => current};
      } finally { inflight--; }
    }});
  vm.runInContext(script, context, {timeout: 1000});
  const settle = () => new Promise(resolve => setImmediate(resolve));
  async function poll() {
    const pending = [...timers.entries()].filter(([, t]) => t.delay === 1000);
    assert.equal(pending.length, 1, 'exactly one next poll');
    const [id, t] = pending[0]; timers.delete(id); await t.fn(); await settle();
  }
  await settle();
  assert.equal(get('temperature').textContent, '-12.5');
  assert.equal(get('humidity').textContent, '0.0');
  assert.equal(get('pressure').textContent, '1001.2');
  assert.equal(get('mq2').textContent, '3.301');
  assert.equal(get('mq2pin').textContent, '1.650');
  assert.equal(get('mq2raw').textContent, '2048');
  assert.equal(vm.runInContext('history.length', context), 1);
  await poll(); assert.equal(vm.runInContext('history.length', context), 1, 'duplicates do not add samples');
  current = {...current, valid: false, age_ms: null, temperature_x10: null, humidity_x10: null, pressure_pa: null};
  await poll(); assert.equal(get('temperature').textContent, '--');
  assert.equal(get('mq2').textContent, '3.301', 'MQ2 survives a BME failure');
  assert.equal(vm.runInContext('history[1]', context), null, 'invalid sample creates a gap');
  assert.match(get('status').textContent, /传感器数据无效/);
  current = {uptime_ms: 1500, sample_seq: 2, age_ms: 100, valid: true,
    temperature_x10: 260, humidity_x10: 450, pressure_pa: 101325,
    mq2: {valid: false, age_ms: null, sample_seq: 123, raw: null, pa7_mv: null, ao_mv: null}};
  await poll(); assert.equal(get('temperature').textContent, '26.0');
  assert.equal(get('mq2').textContent, '--', 'BME survives an MQ2 failure');
  clock += 2100; intervals.forEach(fn => fn());
  assert.equal(get('temperature').textContent, '--');
  assert.match(get('status').textContent, /数据中断/);
  current = {...current, uptime_ms: 1600, age_ms: 2000};
  await poll(); assert.equal(get('temperature').textContent, '--', 'aged sample is rejected');
  current = {...current, uptime_ms: 100, sample_seq: 1, age_ms: 10};
  await poll(); assert.equal(vm.runInContext('history.length', context), 1, 'reboot starts a new chart');
  mode = 'error'; await poll(); assert.equal(get('temperature').textContent, '--');
  mode = 'slow';
  const pending = [...timers.entries()].find(([, t]) => t.delay === 1000);
  timers.delete(pending[0]); const slow = pending[1].fn(); await settle();
  assert.equal(inflight, 1);
  assert.equal([...timers.values()].filter(t => t.delay === 1000).length, 0, 'no parallel poll while pending');
  const abort = [...timers.values()].find(t => t.delay === 1800); abort.fn(); await slow;
  assert.equal(inflight, 0); assert.equal(maxInflight, 1);
  mode = 'normal'; await poll(); assert.equal(get('temperature').textContent, '26.0');
  current = {...current, mq2: {valid: true, age_ms: 0, sample_seq: 124, raw: 0, pa7_mv: 0, ao_mv: 0}};
  await poll(); assert.equal(get('mq2').textContent, '0.000', 'zero is a valid ADC reading');
  current = {...current, mq2: {...current.mq2, age_ms: 1999}};
  await poll(); clock += 500; intervals.forEach(fn => fn());
  assert.equal(get('mq2').textContent, '--', 'source age expires between network polls');
  current = {...current, mq2: {...current.mq2, age_ms: 2000}};
  await poll(); assert.equal(get('mq2').textContent, '--');
  current = {...current, mq2: {...current.mq2, age_ms: 0, raw: 4095, pa7_mv: 3300, ao_mv: 6600}};
  await poll(); assert.equal(get('mq2').textContent, '6.600');
  assert.match(get('mq2state').textContent, /满量程/);
  mode = 'error'; await poll(); assert.equal(get('mq2').textContent, '--');
  mode = 'normal'; await poll(); assert.equal(get('mq2').textContent, '6.600', 'MQ2 recovers after network loss');
  fs.writeFileSync(path.join(__dirname, 'build/slave_web_preview.html'), page);
  console.log('PASS: real embedded UTF-8 page, units, nulls, expiry, gaps, reboot, abort and serial polling');
}
module.exports = {readPage};
if (require.main === module) check().catch(error => {console.error(error); process.exitCode = 1;});
