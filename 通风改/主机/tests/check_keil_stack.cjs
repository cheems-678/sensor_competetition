const fs = require('node:fs');
const path = require('node:path');

const graphPath = process.argv[2] || path.join(__dirname, '../MDK-ARM/LoraSlaveV1.0/LoraSlaveV1.htm');
const startupPath = process.argv[3] || path.join(__dirname, '../MDK-ARM/startup_stm32f103xb.s');
const graph = fs.readFileSync(graphPath, 'utf8');
const startup = fs.readFileSync(startupPath, 'utf8');
const size = startup.match(/^Stack_Size\s+EQU\s+(0x[0-9a-f]+|\d+)/im);
const depth = graph.match(/Maximum Stack Usage\s*=\s*(\d+) bytes/);
if (!size || !depth) throw new Error('Missing startup stack allocation or Keil call-graph depth');
const reserved = Number(size[1]), known = Number(depth[1]), interruptReserve = 256;
if (reserved < interruptReserve || known > reserved - interruptReserve) {
  throw new Error(`Keil stack budget exceeded: known=${known}, allocated=${reserved}, interrupt reserve=${interruptReserve}`);
}
console.log(`PASS Keil stack: known=${known}, allocated=${reserved}, unused=${reserved-known}, interrupt reserve=${interruptReserve}; unknown indirect calls still need hardware validation`);
