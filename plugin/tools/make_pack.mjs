// Builds a content pack directory: pack.json, engine.json, presets.json and ui/ (index.html + three.js + fonts, all local).
// The engine dylib is added by CMake (factory pack) or by release.sh (update zip).
// usage: node make_pack.mjs <srcDir with apparition.src.html + amp3d.js> <outDir> <version>
import fs from 'node:fs';
import path from 'node:path';
import {execFileSync} from 'node:child_process';

const [srcDir, out, version] = process.argv.slice(2);
const here = path.dirname(new URL(import.meta.url).pathname);
const cache = path.join(here, '../third_party/web');
fs.mkdirSync(cache, {recursive: true});
fs.rmSync(out, {recursive: true, force: true});
fs.mkdirSync(path.join(out, 'ui/fonts'), {recursive: true});

const UA = 'Mozilla/5.0 (Macintosh; Intel Mac OS X 14_0) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Safari/605.1.15';
async function cached(url, name) {
  const f = path.join(cache, name);
  if (!fs.existsSync(f)) {
    const r = await fetch(url, {headers: {'User-Agent': UA}});
    if (!r.ok) throw new Error(`download failed ${r.status}: ${url}`);
    fs.writeFileSync(f, Buffer.from(await r.arrayBuffer()));
  }
  return fs.readFileSync(f);
}

let html = fs.readFileSync(path.join(srcDir, 'apparition.src.html'), 'utf8');
html = html.replace('/*__AMP3D__*/', () => fs.readFileSync(path.join(srcDir, 'amp3d.js'), 'utf8')).replace('__DI_B64__', '');

// three.js → local file
const THREE = 'https://cdnjs.cloudflare.com/ajax/libs/three.js/0.158.0/three.min.js';
if (!html.includes(THREE)) throw new Error('three.js URL not found in template');
fs.writeFileSync(path.join(out, 'ui/three.min.js'), await cached(THREE, 'three-0.158.0.min.js'));
html = html.replace(THREE, 'three.min.js');

// Google Fonts → local woff2 + css
const m = html.match(/<link rel="stylesheet" href="(https:\/\/fonts\.googleapis\.com\/css2\?[^"]+)">/);
if (!m) throw new Error('font link not found');
const cssUrl = m[1].replace(/&amp;/g, '&');
let css = (await cached(cssUrl, 'fonts-' + Buffer.from(cssUrl).toString('base64url').slice(-24) + '.css')).toString();
let i = 0;
for (const u of [...new Set(css.match(/https:\/\/fonts\.gstatic\.com\/[^)]+/g))]) {
  const name = `f${i++}.woff2`;
  fs.writeFileSync(path.join(out, 'ui/fonts', name), await cached(u, 'font-' + path.basename(u)));
  css = css.split(u).join(name);
}
fs.writeFileSync(path.join(out, 'ui/fonts/fonts.css'), css);
html = html.replace(m[0], '<link rel="stylesheet" href="fonts/fonts.css">').replace(/<link rel="preconnect"[^>]*>\n?/g, '');

fs.writeFileSync(path.join(out, 'ui/index.html'), html);
execFileSync('node', [path.join(here, 'build_content.mjs'), path.join(srcDir, 'apparition.src.html'), out], {stdio: 'inherit'});
fs.writeFileSync(path.join(out, 'pack.json'), JSON.stringify({version, abi: 1, minShell: '1.0.0', built: new Date().toISOString()}, null, 1));
console.log(`pack ${version}: ui ${(fs.statSync(path.join(out, 'ui/index.html')).size / 1024).toFixed(0)} KB, ${i} font files`);
