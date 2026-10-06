const fs = require('node:fs');
const path = require('node:path');
const raw = fs.readFileSync(path.join(__dirname, '../App/slave_web_page.html'));
const source = require('node:zlib').gzipSync(raw, {level:9,mtime:0});
const chunks = [];
for (let start = 0; start < source.length; start += 96) {
  let line = '';
  for (const byte of source.subarray(start, start + 96)) {
    if (byte >= 32 && byte < 127 && byte !== 34 && byte !== 92) line += String.fromCharCode(byte);
    else if (byte === 34) line += '\\"';
    else if (byte === 92) line += '\\\\';
    else line += '\\' + byte.toString(8).padStart(3, '0');
  }
  chunks.push('"' + line + '"');
}
fs.writeFileSync(path.join(__dirname, '../App/slave_web_page.h'),
  '/* Generated from slave_web_page.html; edit the HTML source. */\n' +
  '#ifndef SLAVE_WEB_PAGE_H\n#define SLAVE_WEB_PAGE_H\nstatic const char SlaveWebPage[] =\n' +
  chunks.join('\n') + ';\n#endif\n', 'ascii');
console.log('Generated embedded page (' + source.length + ' gzip bytes; '+raw.length+' UTF-8 bytes)' );
