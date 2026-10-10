// Reuse the existing frontend build dependency; never install packages here.
const fs = require('node:fs');
const path = require('node:path');
const esbuild = require('../../上位机/frontend/node_modules/esbuild');
function buildPage(source) {
  return source.replace(/\r\n/g, '\n')
    .replace(/<script>([\s\S]*?)<\/script>/g, (_, script) =>
      '<script>' + esbuild.transformSync(script, {minify: true, target: 'es2020'}).code + '</script>')
    .replace(/<style>([\s\S]*?)<\/style>/g, (_, style) =>
      '<style>' + esbuild.transformSync(style, {loader: 'css', minify: true, target: 'es2020'}).code + '</style>');
}
function readSource() { return fs.readFileSync(path.join(__dirname, '../App/slave_web_page.html'), 'utf8'); }
module.exports = {buildPage, readSource};
