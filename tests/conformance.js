'use strict';


const fs = require('fs');
const path = require('path');
const {spawnSync} = require('child_process');
const assert = require('assert');
const {inspect,instantiateMVP} = require('./support/wasm');
const root = path.resolve(__dirname, '..');
const suite = path.resolve(process.env.WATX_SPEC_DIR || path.join(root, 'build/spec-mvp/test/core'));
const compiler = process.env.WATX_BINARY || path.join(root, 'build', process.platform === 'win32' ? 'watx.exe' : 'watx');
const work = path.resolve(process.env.WATX_TEST_WORK || path.join(root, 'build/conformance'));
fs.mkdirSync(work, {recursive:true});

function parse(text) {
  let i = 0;
  function skip() {
    for (;;) {
      while (/\s/.test(text[i] || '') && i < text.length) i++;
      if (text.slice(i,i+2) === ';;') { while (i < text.length && text[i] !== '\n') i++; }
      else if (text.slice(i,i+2) === '(;') {
        i += 2; let depth = 1;
        while (depth && i < text.length) {
          if (text.slice(i,i+2) === '(;') { depth++; i += 2; }
          else if (text.slice(i,i+2) === ';)') { depth--; i += 2; }
          else i++;
        }
        if (depth) throw Error('Unclosed comment');
      } else break;
    }
  }
  function node() {
    skip(); const start = i;
    if (text[i] === '(') {
      i++; const children = []; skip();
      while (i < text.length && text[i] !== ')') { children.push(node()); skip(); }
      if (text[i++] !== ')') throw Error('Unclosed list');
      return {children,start,end:i};
    }
    if (text[i] === '"') {
      i++;
      while (i < text.length && text[i] !== '"') { if (text[i] === '\\') i++; i++; }
      if (text[i++] !== '"') throw Error('Unclosed string');
    } else {
      while (i < text.length && !/[\s()]/.test(text[i]) && text.slice(i,i+2)!==';;') i++;
    }
    if (i === start) throw Error('Invalid script token');
    return {atom:text.slice(start,i),start,end:i};
  }
  const nodes = []; skip();
  while (i < text.length) { nodes.push(node()); skip(); }
  return nodes;
}
const atom = n => n && n.atom;
const head = n => n && n.children && atom(n.children[0]);
function bytes(token) {
  const text = token.atom; const parts = []; let plain = '';
  const flush = () => { if (plain) { parts.push(Buffer.from(plain)); plain = ''; } };
  for (let i=1;i<text.length-1;i++) {
    if (text[i] !== '\\') { plain += text[i]; continue; }
    flush(); const c = text[++i];
    const escapes = {n:10,r:13,t:9,'"':34,"'":39,'\\':92};
    if (Object.hasOwn(escapes,c)) parts.push(Buffer.from([escapes[c]]));
    else if (c === 'u') {
      if (text[++i] !== '{') throw Error('Invalid Unicode escape');
      let code = ''; while (text[++i] !== '}') code += text[i];
      parts.push(Buffer.from(String.fromCodePoint(parseInt(code.replace(/_/g,''),16))));
    } else parts.push(Buffer.from([parseInt(c + text[++i],16)]));
  }
  flush(); return Buffer.concat(parts);
}
const string = n => bytes(n).toString('utf8');
function floatBits(text,width) {
  let sign = 0n; text = text.replace(/_/g,'');
  if (text[0] === '-') { sign = 1n << BigInt(width-1); text = text.slice(1); }
  else if (text[0] === '+') text = text.slice(1);
  const frac = width === 32 ? 23 : 52;
  const emin = width === 32 ? -126 : -1022;
  const emax = width === 32 ? 127 : 1023;
  const bias = width === 32 ? 127 : 1023;
  const inf = BigInt(width === 32 ? 255 : 2047) << BigInt(frac);
  if (text === 'inf') return sign | inf;
  if (text === 'nan') return sign | inf | (1n << BigInt(frac-1));
  if (text.startsWith('nan:0x')) return sign | inf | BigInt(text.slice(4));
  let numerator, denominator = 1n;
  if (text.startsWith('0x')) {
    const parts = text.slice(2).split(/[pP]/);
    const fraction = (parts[0].split('.')[1] || '').length;
    numerator = BigInt('0x' + parts[0].replace('.',''));
    const exp = Number(parts[1] || 0) - fraction*4;
    if (exp >= 0) numerator <<= BigInt(exp); else denominator <<= BigInt(-exp);
  } else {
    const parts = text.split(/[eE]/);
    const fraction = (parts[0].split('.')[1] || '').length;
    numerator = BigInt(parts[0].replace('.',''));
    const exp = Number(parts[1] || 0) - fraction;
    if (exp >= 0) numerator *= 10n**BigInt(exp); else denominator *= 10n**BigInt(-exp);
  }
  if (!numerator) return sign;
  let exponent = numerator.toString(2).length - denominator.toString(2).length;
  if (exponent >= 0 ? numerator < (denominator << BigInt(exponent)) : (numerator << BigInt(-exponent)) < denominator) exponent--;
  exponent = Math.max(emin,exponent);
  const shift = frac-exponent;
  let n = numerator, d = denominator;
  if (shift >= 0) n <<= BigInt(shift); else d <<= BigInt(-shift);
  let q = n/d; const rem = n%d;
  if (rem*2n > d || (rem*2n === d && (q & 1n))) q++;
  if (q >= (1n << BigInt(frac+1))) { q >>= 1n; exponent++; }
  if (exponent > emax) return sign | inf;
  const hidden = 1n << BigInt(frac);
  if (q < hidden) return sign | q;
  return sign | (BigInt(exponent+bias) << BigInt(frac)) | (q-hidden);
}
function value(n) {
  const type = head(n).slice(0,3); let literal = atom(n.children[1]).replace(/_/g,'');
  if (type === 'i32' || type === 'i64') {
    let negative = literal[0] === '-'; if (/^[+-]/.test(literal)) literal=literal.slice(1);
    let number = BigInt(literal); if (negative) number=-number;
    return type === 'i32' ? Number(BigInt.asIntN(32,number)) : BigInt.asIntN(64,number);
  }
  const width = type === 'f32' ? 32 : 64;
  const bits = floatBits(literal,width); const b = Buffer.alloc(width/8);
  if (width === 32) { b.writeUInt32LE(Number(bits)); return b.readFloatLE(); }
  b.writeBigUInt64LE(bits); return b.readDoubleLE();
}
const stats = {passed:0,failed:0,skipped:0,modules:0,actions:0,rejections:0};
const failures = [];
const skippedReasons = {};
function skip(reason) { stats.skipped++; skippedReasons[reason]=(skippedReasons[reason] || 0)+1; }
const signatures = new WeakMap();
function spectest() {
  const print = () => {};
  return {print,print_i32:print,print_i64:print,print_f32:print,print_f64:print,print_i32_f32:print,print_f64_f64:print,
    global_i32:666,global_i64:666n,global_f32:Math.fround(666.6),global_f64:666.6,
    table:new WebAssembly.Table({initial:10,maximum:20,element:'anyfunc'}),
    memory:new WebAssembly.Memory({initial:1,maximum:2})};
}
function moduleSource(n,text) {
  let p=1; if (atom(n.children[p])?.startsWith('$')) p++;
  if (atom(n.children[p]) === 'binary') return null;
  if (atom(n.children[p]) === 'quote') return Buffer.concat(n.children.slice(p+1).map(bytes));
  return Buffer.from(text.slice(n.start,n.end));
}
function compile(n,text,label) {
  const input=moduleSource(n,text); if (input === null) return null;
  const wat=path.join(work,'case.wat'), wasm=path.join(work,'case.wasm');
  fs.writeFileSync(wat,input);
  const result=spawnSync(compiler,[wat,'-o',wasm],{encoding:'utf8',timeout:10000});
  if (result.error || result.signal) throw Error(`${label}: compiler ${result.error || result.signal}`);
  return {ok:result.status === 0,bytes:result.status === 0 ? fs.readFileSync(wasm) : null,error:result.stderr.trim()};
}
function runFile(file) {
  const text=fs.readFileSync(path.join(suite,file),'utf8');
  let commands=parse(text);

  if(commands.length && commands.every(n=>['type','import','func','table','memory','global','export','start','elem','data'].includes(head(n)))) {
    const wrapped='(module '+text+')';
    const result=compile(parse(wrapped)[0],wrapped,file);
    if(!result.ok) {stats.failed++;failures.push({file,index:1,kind:'implicit-module',error:result.error});return;}
    instantiateMVP(new WebAssembly.Module(result.bytes),result.bytes,{spectest:spectest()});
    stats.passed++;stats.modules++;return;
  }
  const registry={spectest:spectest()}, instances={}; let current=null;
  const action = (n,captureFloatBits=false) => {
    const c=n.children; let p=1, instance=current;
    if (atom(c[p])?.startsWith('$')) instance=instances[atom(c[p++])];
    if (!instance) return {skip:true};
    const name=string(c[p++]);
    if (head(n)==='get') { const global=instance.exports[name]; return {value:global instanceof WebAssembly.Global ? global.value : global}; }


    if(captureFloatBits || c.slice(p).some(arg=>atom(arg.children?.[1])?.includes('nan'))) {
      const type=signatures.get(instance)[name];
      const clause=`${type.params.length ? `(param ${type.params.join(' ')})` : ''} ${type.results.length ? `(result ${type.results.join(' ')})` : ''}`;
      const arguments_=c.slice(p).map(arg=>text.slice(arg.start,arg.end)).join(' ');
      const floatType=type.results[0];
      const raw=captureFloatBits && (floatType==='f32' || floatType==='f64');
      const resultType=raw ? (floatType==='f32' ? 'i32' : 'i64') : type.results[0];
      const call=`(call $target ${arguments_})`;
      const expression=raw ? `(${resultType}.reinterpret_${floatType} ${call})` : call;
      const source=`(module (import "host" "target" (func $target ${clause})) (func (export "run") ${resultType ? `(result ${resultType})` : ''} ${expression}))`;
      const result=compile(parse(source)[0],source,'NaN argument trampoline');
      if(!result.ok) throw Error(result.error);
      const output=new WebAssembly.Instance(new WebAssembly.Module(result.bytes),{host:{target:instance.exports[name]}}).exports.run();
      if(raw) {
        const width=floatType==='f32' ? 32 : 64;
        return {value:NaN,bits:BigInt.asUintN(width,BigInt(output)),width};
      }
      return {value:output};
    }
    return {value:instance.exports[name](...c.slice(p).map(value))};
  };
  for (let index=0;index<commands.length;index++) {
    const n=commands[index], kind=head(n), label=`${file}:${index+1}`;
    try {
      if (kind==='module') {
        current=null; const result=compile(n,text,label); if (!result) { skip("binary-input"); continue; }
        if (!result.ok) throw Error(result.error);
        const module=new WebAssembly.Module(result.bytes);
        current=instantiateMVP(module,result.bytes,registry); stats.modules++;
        signatures.set(current,inspect(result.bytes).exports);
        const id=atom(n.children[1]); if (id?.startsWith('$')) instances[id]=current;
      } else if (kind==='register') {
        const id=atom(n.children[2]); const instance=id ? instances[id] : current;
        if (!instance) { skip("depends-on-binary-module"); continue; }
        registry[string(n.children[1])]=instance.exports;
      } else if (kind==='assert_invalid' || kind==='assert_malformed') {
        const result=compile(n.children[1],text,label); if (!result) { skip("binary-input"); continue; }
        if (result.ok) throw Error('watx accepted an invalid/malformed module'); stats.rejections++;
      } else if (kind==='assert_unlinkable' || kind==='assert_uninstantiable' || (kind==='assert_trap' && head(n.children[1])==='module')) {
        const result=compile(n.children[1],text,label); if (!result) { skip("binary-input"); continue; }
        if (!result.ok) throw Error(`expected valid module: ${result.error}`);
        let trapped=false;
        try { instantiateMVP(new WebAssembly.Module(result.bytes),result.bytes,registry); }
        catch(e) { if (kind==='assert_unlinkable' ? (e instanceof WebAssembly.LinkError || e instanceof WebAssembly.RuntimeError || e instanceof TypeError) : e instanceof WebAssembly.RuntimeError) trapped=true; else throw e; }
        if (!trapped) throw Error('expected module linking/initialization failure');
      } else if (kind==='assert_return' || kind==='assert_return_canonical_nan' || kind==='assert_return_arithmetic_nan') {
        const expected=n.children[2];
        const nanToken=atom(expected?.children?.[1]);
        const expectNaN=kind!=='assert_return' || nanToken?.includes('nan');
        const result=action(n.children[1],expectNaN); if (result.skip) { skip("depends-on-binary-module"); continue; }
        if (kind!=='assert_return' || atom(expected?.children?.[1])?.startsWith('nan:canonical') || atom(expected?.children?.[1])?.startsWith('nan:arithmetic')) {
          if (!Number.isNaN(result.value)) throw Error('expected NaN');
          if(result.bits!==undefined) {
            const fraction=result.width===32 ? 23n : 52n;
            const exponent=BigInt(result.width===32 ? 255 : 2047)<<fraction;
            const quiet=1n<<(fraction-1n);
            const magnitude=result.bits&((1n<<BigInt(result.width-1))-1n);
            if(kind==='assert_return_canonical_nan' || nanToken==='nan:canonical') assert.strictEqual(magnitude,exponent|quiet);
            else assert.strictEqual(magnitude&(exponent|quiet),exponent|quiet);
          }
        } else if (!expected) assert.strictEqual(result.value,undefined);
        else {
          const wanted=value(expected);
          if (Number.isNaN(wanted)) {
            assert(Number.isNaN(result.value));
            if(result.bits!==undefined) assert.strictEqual(result.bits,floatBits(nanToken,result.width));
          }
          else assert(Object.is(result.value,wanted),`got ${String(result.value)}, expected ${String(wanted)}`);
        }
        stats.actions++;
      } else if (kind==='assert_trap' || kind==='assert_exhaustion') {
        let trapped=false,skipped=false;
        try { skipped=action(n.children[1]).skip; } catch(e) { if (e instanceof WebAssembly.RuntimeError || (kind==='assert_exhaustion' && e instanceof RangeError)) trapped=true; else throw e; }
        if (skipped) { skip("depends-on-binary-module"); continue; }
        if (!trapped) throw Error('expected execution trap'); stats.actions++;
      } else if (kind==='invoke' || kind==='get') { if (action(n).skip) { skip("depends-on-binary-module"); continue; } stats.actions++; }
      else { skip("unsupported-script-command:"+kind); continue; }
      stats.passed++;
    } catch(e) {
      stats.failed++; const failure={file,index:index+1,kind,error:e.message}; failures.push(failure);
      if (fs.existsSync(path.join(work,'case.wat'))) fs.copyFileSync(path.join(work,'case.wat'),path.join(work,`${file}.${index+1}.wat`));
      if (failures.length<=60) console.error(`${label} ${kind}: ${e.message}`);
      if (kind==='module') current=null;
    }
  }
}
const selected=process.argv.slice(2);
const files=selected.length ? selected : fs.readdirSync(suite).filter(f=>f.endsWith('.wast')).sort();
for (const file of files) { runFile(file); }
fs.writeFileSync(path.join(work,'results.json'),JSON.stringify({stats,skippedReasons,failures},null,2));
console.log(JSON.stringify({stats,skippedReasons}));
if (stats.failed) process.exitCode=1;
