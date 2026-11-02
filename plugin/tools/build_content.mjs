// Builds the content pack data from the web source (single source of truth):
//   engine.json  – parameters, amp models, tonestack component values, cab curves (read by the C++ engine)
//   presets.json – the factory presets (for tests / tooling; the UI carries its own copy)
// usage: node build_content.mjs <apparition.src.html> <outDir>
import fs from 'node:fs';
import vm from 'node:vm';

const [src, out] = process.argv.slice(2);
const html = fs.readFileSync(src, 'utf8');
const cut = (a, b) => { const i = html.indexOf(a), j = html.indexOf(b, i); if (i < 0 || j < 0) throw new Error('marker not found: ' + a); return html.slice(i, j); };

// constants + the helpers they need at definition time
const code = [
  cut('const REF=', '\n'),
  cut('const f1=', 'const state={}'),
  cut('const st=(g,cf,k,cc,ml', 'const MICS='),
  cut('const lg2=Math.log2', 'const rippleCache'),
  cut('const CATS=', 'let curPreset'),
  "CABS.wraith.pts=derive('wraith'); CABS.hollow.pts=derive('hollow'); if(CABS.tomb) CABS.tomb.pts=derive('tomb');",
  'const EQF=[80,240,750,2200,6600];',
  'globalThis.__out={PARAMS,AMPS,CABS,EQF,PRESETS,CATS,REF};'
].join('\n');
const ctx = {Math, console}; vm.createContext(ctx); vm.runInContext(code.replace(/^const /gm, 'var '), ctx);
const {PARAMS, AMPS, CABS, EQF, PRESETS, CATS, REF} = ctx.__out;

// tonestack table lives inside the worklet source string
const tsm = html.match(/const TS=(\{[^;]*\});/);
const TS = vm.runInNewContext('(' + tsm[1] + ')');

const params = {};
for (const [id, p] of Object.entries(PARAMS)) {
  const o = {def: p.def};
  if (p.e) o.e = p.e; else if (p.b) { o.min = 0; o.max = 1; o.b = 1; } else { o.min = p.min; o.max = p.max; if (p.log) o.log = 1; }
  params[id] = o;
}
const amps = {};
for (const [k, a] of Object.entries(AMPS)) {
  const {name, sub, tape, chip, ...dsp} = a; amps[k] = {name, ...dsp};
}
const cabs = {};
for (const [k, c] of Object.entries(CABS)) cabs[k] = {name: c.name, seed: c.seed, tail: c.tail, pts: c.pts};

fs.mkdirSync(out, {recursive: true});
fs.writeFileSync(out + '/engine.json', JSON.stringify({abi: 1, params, amps, ts: TS, cabs, eqf: EQF, odLatency: 192 / 48000, revK: 1}));
fs.writeFileSync(out + '/presets.json', JSON.stringify({cats: CATS, presets: PRESETS, ref: REF}));
console.log(`engine.json: ${Object.keys(amps).length} amps, ${Object.keys(cabs).length} cabs, ${Object.keys(params).length} params · presets.json: ${PRESETS.length} presets`);
