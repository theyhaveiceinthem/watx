'use strict';








const PAGE_SIZE = 65536n;
const VALUE_TYPES = {0x7f: 'i32', 0x7e: 'i64', 0x7d: 'f32', 0x7c: 'f64'};

class Reader {
  constructor(buffer, start, end) {
    this.buffer = buffer;
    this.pos = start || 0;
    this.end = end === undefined ? buffer.length : end;
  }

  require(size) {
    if (!Number.isSafeInteger(size) || size < 0 || this.pos + size > this.end) {
      throw new RangeError('truncated WebAssembly binary');
    }
  }

  byte() {
    this.require(1);
    return this.buffer[this.pos++];
  }

  u32() {
    let value = 0;
    let factor = 1;
    for (let i = 0; i < 5; i++) {
      const byte = this.byte();
      value += (byte & 0x7f) * factor;
      if ((byte & 0x80) === 0) {
        if (value > 0xffffffff) throw new RangeError('u32 LEB128 overflow');
        return value;
      }
      if (i === 4) throw new RangeError('invalid u32 LEB128');
      factor *= 128;
    }
    throw new RangeError('invalid u32 LEB128');
  }

  s32() {
    let value = 0n;
    let shift = 0n;
    for (let i = 0; i < 5; i++) {
      const byte = this.byte();
      value |= BigInt(byte & 0x7f) << shift;
      shift += 7n;
      if ((byte & 0x80) === 0) {
        if ((byte & 0x40) !== 0) value -= 1n << shift;
        return BigInt.asIntN(32, value);
      }
    }
    throw new RangeError('invalid s32 LEB128');
  }

  string() {
    const length = this.u32();
    this.require(length);
    const value = this.buffer.toString('utf8', this.pos, this.pos + length);
    this.pos += length;
    return value;
  }

  skip(length) {
    this.require(length);
    this.pos += length;
  }
}

function readLimits(reader) {
  const flags = reader.byte();
  if (flags !== 0 && flags !== 1) throw new RangeError('invalid MVP limits flags');
  const minimum = reader.u32();
  if (flags) reader.u32();
  return minimum;
}

function readValueType(reader) {
  const type = VALUE_TYPES[reader.byte()];
  if (!type) throw new RangeError('invalid MVP value type');
  return type;
}

function readModule(buffer) {
  if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
  if (buffer.length < 8 || !buffer.subarray(0, 8).equals(Buffer.from([0, 0x61, 0x73, 0x6d, 1, 0, 0, 0]))) {
    throw new RangeError('not a WebAssembly 1.0 binary');
  }

  const parsed = {
    types: [], funcTypes: [], exports: [], tables: [], memories: [], globals: [],
    elements: [], data: []
  };
  const outer = new Reader(buffer, 8);
  while (outer.pos < outer.end) {
    const id = outer.byte();
    const size = outer.u32();
    outer.require(size);
    const sectionEnd = outer.pos + size;
    const section = new Reader(buffer, outer.pos, sectionEnd);
    outer.pos = sectionEnd;

    if (id === 1) {
      const count = section.u32();
      for (let i = 0; i < count; i++) {
        if (section.byte() !== 0x60) throw new RangeError('invalid MVP function type');
        const params = [];
        let length = section.u32();
        while (length--) params.push(readValueType(section));
        const results = [];
        length = section.u32();
        while (length--) results.push(readValueType(section));
        parsed.types.push({params, results});
      }
    } else if (id === 2) {
      const count = section.u32();
      for (let i = 0; i < count; i++) {
        const module = section.string();
        const name = section.string();
        const kind = section.byte();
        if (kind === 0) {
          parsed.funcTypes.push(section.u32());
        } else if (kind === 1) {
          section.byte();
          parsed.tables.push({import: {module, name}, minimum: readLimits(section)});
        } else if (kind === 2) {
          parsed.memories.push({import: {module, name}, minimum: readLimits(section)});
        } else if (kind === 3) {
          const type = readValueType(section);
          const mutable = section.byte() !== 0;
          parsed.globals.push({import: {module, name}, type, mutable});
        } else {
          throw new RangeError('unsupported MVP import kind');
        }
      }
    } else if (id === 3) {
      const count = section.u32();
      for (let i = 0; i < count; i++) parsed.funcTypes.push(section.u32());
    } else if (id === 4) {
      const count = section.u32();
      for (let i = 0; i < count; i++) {
        section.byte();
        parsed.tables.push({minimum: readLimits(section)});
      }
    } else if (id === 5) {
      const count = section.u32();
      for (let i = 0; i < count; i++) parsed.memories.push({minimum: readLimits(section)});
    } else if (id === 7) {
      const count = section.u32();
      for (let i = 0; i < count; i++) {
        parsed.exports.push({name: section.string(), kind: section.byte(), index: section.u32()});
      }
    } else if (id === 9) {
      const count = section.u32();
      for (let i = 0; i < count; i++) {
        const tableIndex = section.u32();
        const offset = readOffset(section);
        const length = section.u32();
        for (let j = 0; j < length; j++) section.u32();
        parsed.elements.push({tableIndex, offset, length});
      }
    } else if (id === 11) {
      const count = section.u32();
      for (let i = 0; i < count; i++) {
        const memoryIndex = section.u32();
        const offset = readOffset(section);
        const length = section.u32();
        section.skip(length);
        parsed.data.push({memoryIndex, offset, length});
      }
    }
  }

  const exportSignatures = Object.create(null);
  for (const ex of parsed.exports) {
    if (ex.kind !== 0) continue;
    const typeIndex = parsed.funcTypes[ex.index];
    const type = parsed.types[typeIndex];
    if (!type) throw new RangeError('function export has an invalid type index');
    exportSignatures[ex.name] = {params: type.params.slice(), results: type.results.slice()};
  }
  parsed.exportSignatures = exportSignatures;
  return parsed;
}

function readOffset(reader) {
  const opcode = reader.byte();
  let value;
  if (opcode === 0x41) {
    value = BigInt.asUintN(32, reader.s32());
  } else if (opcode === 0x23) {
    value = {globalIndex: reader.u32()};
  } else {
    throw new RangeError('unsupported active segment offset expression');
  }
  if (reader.byte() !== 0x0b) throw new RangeError('unterminated active segment offset expression');
  return value;
}

function importValue(imports, descriptor) {
  if (!descriptor || !imports) return undefined;
  try {
    const module = imports[descriptor.module];
    if (module === null || module === undefined) return undefined;
    return module[descriptor.name];
  } catch (_) {

    return undefined;
  }
}

function resolveOffset(offset, globals, imports) {
  if (typeof offset === 'bigint') return offset;
  const global = globals[offset.globalIndex];
  if (!global || !global.import || global.mutable) return undefined;
  const imported = importValue(imports, global.import);
  if (imported === undefined || imported === null) return undefined;
  let value;
  try { value = imported instanceof WebAssembly.Global ? imported.value : imported; }
  catch (_) { return undefined; }
  if (global.type !== 'i32' || (typeof value !== 'number' && typeof value !== 'bigint')) return undefined;
  try { return BigInt.asUintN(32, BigInt(value)); }
  catch (_) { return undefined; }
}

function resourceSize(resource, imports, byteSize) {
  if (resource.import) {
    const value = importValue(imports, resource.import);
    if (byteSize) {
      if (!(value instanceof WebAssembly.Memory)) return undefined;
      return BigInt(value.buffer.byteLength);
    }
    if (!(value instanceof WebAssembly.Table)) return undefined;
    return BigInt(value.length);
  }
  return BigInt(resource.minimum) * (byteSize ? PAGE_SIZE : 1n);
}

function preflight(parsed, imports) {
  for (const segment of parsed.elements) {
    const table = parsed.tables[segment.tableIndex];
    if (!table) continue;
    const size = resourceSize(table, imports, false);
    const offset = resolveOffset(segment.offset, parsed.globals, imports);
    if (size === undefined || offset === undefined) continue;
    if (offset + BigInt(segment.length) > size) {
      throw new WebAssembly.LinkError('elements segment does not fit table');
    }
  }
  for (const segment of parsed.data) {
    const memory = parsed.memories[segment.memoryIndex];
    if (!memory) continue;
    const size = resourceSize(memory, imports, true);
    const offset = resolveOffset(segment.offset, parsed.globals, imports);
    if (size === undefined || offset === undefined) continue;
    if (offset + BigInt(segment.length) > size) {
      throw new WebAssembly.LinkError('data segment does not fit memory');
    }
  }
}

function inspect(buffer) {
  const parsed = readModule(buffer);
  return {exports: parsed.exportSignatures};
}

function instantiateMVP(module, buffer, imports) {
  const parsed = readModule(buffer);
  preflight(parsed, imports || {});
  return new WebAssembly.Instance(module, imports || {});
}

module.exports = {inspect, instantiateMVP};
