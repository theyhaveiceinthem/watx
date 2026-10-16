const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const assert = require('node:assert/strict');

const cli = path.resolve(process.argv[2] || 'build/watx');
const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'watx-jit-'));
const source = path.join(temporary, 'numeric.wat');
const binary = path.join(temporary, 'numeric.wasm');
const fields = [];
const cases = [];
const integerOperations = ['add', 'sub', 'mul', 'and', 'or', 'xor', 'shl', 'shr_s',
  'shr_u', 'rotl', 'rotr', 'eq', 'ne', 'lt_s', 'lt_u', 'gt_s', 'gt_u', 'le_s',
  'le_u', 'ge_s', 'ge_u'];
let id = 0;

function addOperation(op, input, output, argumentsList) {
  const name = 'case_' + id++;
  const parameters = argumentsList[0].length;
  const prefix = `local.get 0 local.set ${parameters} local.get ${parameters} local.tee ${parameters} drop local.get ${parameters}`;
  const body = prefix + (parameters === 2 ? ' local.get 1 ' : ' ') + op;
  fields.push(`(func (export "${name}") (param ${Array(parameters).fill(input).join(' ')}) (result ${output}) (local ${input}) ${body})`);
  for (const args of argumentsList) cases.push({ name, input, output, args });
}

for (const type of ['i32', 'i64']) {
  const pairs = type === 'i32'
    ? [[0, 0], [1, -1], [-2147483648, 31], [-12345, 255], [2147483647, 32]]
    : [[0n, 0n], [1n, -1n], [-9223372036854775808n, 63n], [-123456789012345n, 255n], [9223372036854775807n, 64n]];
  for (const operation of integerOperations) {
    const comparison = ['eq', 'ne', 'lt_s', 'lt_u', 'gt_s', 'gt_u', 'le_s', 'le_u', 'ge_s', 'ge_u'].includes(operation);
    addOperation(type + '.' + operation, type, comparison ? 'i32' : type, pairs);
  }
  addOperation(type + '.eqz', type, 'i32', pairs.map(pair => [pair[0]]));
}
const floatingPairs = [[1.5, 2.25], [-123.5, 0.125], [-0, 0], [Infinity, 2], [1, -Infinity]];
for (const operation of ['add', 'sub', 'mul', 'div']) {
  addOperation('f64.' + operation, 'f64', 'f64', floatingPairs);
}
addOperation('f64.sqrt', 'f64', 'f64', [[0], [-0], [2], [Infinity], [-1]]);
addOperation('f64.convert_i32_s', 'i32', 'f64', [[0], [-1], [-2147483648], [2147483647]]);
addOperation('f64.convert_i32_u', 'i32', 'f64', [[0], [-1], [-2147483648], [2147483647]]);
addOperation('i32.wrap_i64', 'i64', 'i32', [[0n], [-1n], [4294967301n], [-9223372036854775808n]]);
addOperation('i64.extend_i32_s', 'i32', 'i64', [[0], [-1], [-2147483648], [2147483647]]);
addOperation('i64.extend_i32_u', 'i32', 'i64', [[0], [-1], [-2147483648], [2147483647]]);

fields.push(`(func (export "deep_stack") (result i32) ${Array.from({length: 128}, (_, i) => 'i32.const ' + i).join(' ')} ${Array(127).fill('i32.add').join(' ')})`);
cases.push({name: 'deep_stack', input: 'i32', output: 'i32', args: []});
fields.push(`(func (export "deep_control") (result i32) ${Array(16).fill('block (result i32)').join(' ')} i32.const 77 br 15 ${Array(16).fill('end').join(' ')})`);
cases.push({name: 'deep_control', input: 'i32', output: 'i32', args: []});
fields.push(`(func (export "many_locals") (result i32) (local ${Array(80).fill('i32').join(' ')}) i32.const 1234 local.set 79 local.get 79)`);
cases.push({name: 'many_locals', input: 'i32', output: 'i32', args: []});
fields.push('(func (export "constant64") (result i64) i64.const 0x1122334455667788)');
cases.push({name: 'constant64', input: 'i32', output: 'i64', args: []});
fields.push('(func (export "constant32") (result f32) f32.const -0)');
cases.push({name: 'constant32', input: 'i32', output: 'f32', args: []});

function run(args) {
  const child = spawnSync(cli, args, {encoding: 'utf8', timeout: 15000});
  if (child.error || child.status !== 0) {
    throw new Error(`${args.join(' ')} failed (${child.status}): ${child.error || child.stderr}`);
  }
  return child.stdout.trim();
}
function literal(value) {
  if (Object.is(value, -0)) return '-0';
  if (value === Infinity) return 'inf';
  if (value === -Infinity) return '-inf';
  if (Number.isNaN(value)) return 'nan';
  return String(value);
}
function numeric(text) {
  if (text === 'inf') return Infinity;
  if (text === '-inf') return -Infinity;
  if (/^-?nan$/.test(text)) return NaN;
  return Number(text);
}

try {
  fs.writeFileSync(source, '(module\n' + fields.join('\n') + '\n)\n');
  run([source, '-o', binary]);
  const instance = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(binary)));
  let checks = 0;
  for (const test of cases) {
    const expected = instance.exports[test.name](...test.args);
    for (const flags of [[], ['--jit']]) {
      const text = run([binary, '--run', '--function', test.name, ...flags, '--',
        ...test.args.map(value => test.input + ':' + literal(value))]);
      assert(text.startsWith(test.output + ':'), text);
      const value = text.slice(test.output.length + 1);
      const actual = test.output === 'i64' ? BigInt(value) : numeric(value);
      assert(Object.is(actual, expected) || (Number.isNaN(actual) && Number.isNaN(expected)),
        `${test.name} ${flags.join(' ')}: ${test.args.map(literal)} expected ${literal(expected)}, got ${text}`);
      checks++;
    }
  }
  console.log(`${checks} interpreter/JIT results matched Node/V8, including deep stacks and controls`);
} finally {
  fs.rmSync(temporary, {recursive: true, force: true});
}
