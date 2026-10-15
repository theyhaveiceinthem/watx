#include <watx/binary_reader.h>
#include <watx/wasm/opcodes.h>

#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace watx {
namespace {

class Reader {
  public:
    Reader(const std::vector<unsigned char> &bytes, std::size_t begin, std::size_t end)
        : bytes_(bytes), pos_(begin), end_(end) {}

    std::size_t position() const {
        return pos_;
    }
    std::size_t remaining() const {
        return end_ - pos_;
    }
    bool empty() const {
        return pos_ == end_;
    }
    const std::vector<unsigned char> &bytes() const {
        return bytes_;
    }

    void require(std::size_t n) const {
        if (n > remaining())
            throw std::runtime_error("truncated WebAssembly binary");
    }

    unsigned char byte() {
        require(1);
        return bytes_[pos_++];
    }

    uint32_t u32() {
        uint32_t value = 0;
        unsigned int shift = 0;
        for (unsigned int i = 0; i < 5; ++i) {
            unsigned char b = byte();
            unsigned int payload = b & 0x7f;
            if (i == 4 && payload > 0x0f)
                throw std::runtime_error("u32 LEB128 overflow");
            value |= static_cast<uint32_t>(payload) << shift;
            if ((b & 0x80) == 0)
                return value;
            if (i == 4)
                throw std::runtime_error("u32 LEB128 is too long");
            shift += 7;
        }
        throw std::runtime_error("u32 LEB128 is too long");
    }

    uint64_t signed_bits(unsigned int width) {
        const unsigned int max_bytes = (width + 6) / 7;
        uint64_t value = 0;
        unsigned int shift = 0;
        unsigned char last = 0;
        unsigned int count = 0;
        for (; count < max_bytes; ++count) {
            last = byte();
            const unsigned int payload = last & 0x7f;
            if (count + 1 == max_bytes) {
                const unsigned int used = width - count * 7;
                const unsigned int low_mask = (1U << used) - 1U;
                const unsigned int high = payload & ~low_mask;
                const bool negative = (payload & (1U << (used - 1))) != 0;
                if ((!negative && high != 0) || (negative && high != (0x7fU & ~low_mask)))
                    throw std::runtime_error("signed LEB128 overflow");
                value |= static_cast<uint64_t>(payload & low_mask) << shift;
            } else {
                value |= static_cast<uint64_t>(payload) << shift;
            }
            if ((last & 0x80) == 0) {
                const unsigned int bits = (count + 1) * 7;
                if ((last & 0x40) && bits < width)
                    value |= (~static_cast<uint64_t>(0)) << bits;
                if (width == 32)
                    value &= static_cast<uint64_t>(0xffffffffUL);
                return value;
            }
            shift += 7;
        }
        throw std::runtime_error("signed LEB128 is too long");
    }

    std::string string() {
        uint32_t n = u32();
        require(n);
        std::string result;
        result.reserve(n);
        for (uint32_t i = 0; i < n; ++i)
            result.push_back(static_cast<char>(bytes_[pos_++]));
        if (!valid_utf8(result))
            throw std::runtime_error("invalid UTF-8 in WebAssembly name");
        return result;
    }

    void copy_bytes(std::vector<unsigned char> &out, std::size_t n) {
        require(n);
        out.insert(out.end(), bytes_.begin() + pos_, bytes_.begin() + pos_ + n);
        pos_ += n;
    }

    void skip(std::size_t n) {
        require(n);
        pos_ += n;
    }

  private:
    static bool valid_utf8(const std::string &s) {
        std::size_t i = 0;
        while (i < s.size()) {
            const unsigned char a = static_cast<unsigned char>(s[i++]);
            if (a <= 0x7f)
                continue;
            if (a >= 0xc2 && a <= 0xdf) {
                if (i >= s.size() || (static_cast<unsigned char>(s[i++]) & 0xc0) != 0x80)
                    return false;
                continue;
            }
            if (a >= 0xe0 && a <= 0xef) {
                if (i + 1 >= s.size())
                    return false;
                const unsigned char b = static_cast<unsigned char>(s[i++]);
                const unsigned char c = static_cast<unsigned char>(s[i++]);
                if ((b & 0xc0) != 0x80 || (c & 0xc0) != 0x80 || (a == 0xe0 && b < 0xa0) ||
                    (a == 0xed && b >= 0xa0))
                    return false;
                continue;
            }
            if (a >= 0xf0 && a <= 0xf4) {
                if (i + 2 >= s.size())
                    return false;
                const unsigned char b = static_cast<unsigned char>(s[i++]);
                const unsigned char c = static_cast<unsigned char>(s[i++]);
                const unsigned char d = static_cast<unsigned char>(s[i++]);
                if ((b & 0xc0) != 0x80 || (c & 0xc0) != 0x80 || (d & 0xc0) != 0x80 ||
                    (a == 0xf0 && b < 0x90) || (a == 0xf4 && b >= 0x90))
                    return false;
                continue;
            }
            return false;
        }
        return true;
    }

    const std::vector<unsigned char> &bytes_;
    std::size_t pos_, end_;
};

ValueType read_value_type(Reader &r) {
    switch (r.byte()) {
    case 0x7f:
        return TYPE_I32;
    case 0x7e:
        return TYPE_I64;
    case 0x7d:
        return TYPE_F32;
    case 0x7c:
        return TYPE_F64;
    default:
        throw std::runtime_error("invalid MVP value type");
    }
}

ValueType read_block_type(Reader &r) {
    switch (r.byte()) {
    case 0x40:
        return TYPE_NONE;
    case 0x7f:
        return TYPE_I32;
    case 0x7e:
        return TYPE_I64;
    case 0x7d:
        return TYPE_F32;
    case 0x7c:
        return TYPE_F64;
    default:
        throw std::runtime_error("invalid MVP block type");
    }
}

uint32_t read_count(Reader &r, std::size_t minimum_bytes) {
    uint32_t count = r.u32();
    if (minimum_bytes != 0 && count > r.remaining() / minimum_bytes)
        throw std::runtime_error("vector count exceeds section bounds");
    return count;
}

void read_limits(Reader &r, Limits &limits) {
    unsigned char flags = r.byte();
    if (flags > 1)
        throw std::runtime_error("invalid MVP limits flags");
    limits.minimum = r.u32();
    limits.has_maximum = flags != 0;
    if (limits.has_maximum)
        limits.maximum = r.u32();
}

void read_memory_limits(Reader &r, Memory &memory) {
    unsigned char flags = r.byte();
    if (flags != 0 && flags != 1 && flags != 3)
        throw std::runtime_error("invalid memory limits flags");
    memory.shared = flags == 3;
    memory.limits.minimum = r.u32();
    memory.limits.has_maximum = (flags & 1) != 0;
    if (memory.limits.has_maximum)
        memory.limits.maximum = r.u32();
    if (memory.shared && !memory.limits.has_maximum)
        throw std::runtime_error("shared memory requires an explicit maximum");
}

struct ControlFrame {
    unsigned char opcode;
    bool has_else;
    explicit ControlFrame(unsigned char op) : opcode(op), has_else(false) {}
};

bool plain_opcode(unsigned char op) {
    return op == 0x00 || op == 0x01 || op == 0x05 || op == 0x0f || op == 0x1a || op == 0x1b ||
           (op >= 0x45 && op <= 0xbf);
}

Instruction read_instruction(Reader &r, unsigned char op) {
    Instruction i(op);
    if (op == 0xfe) {
        i.extended_opcode = r.u32();
        uint32_t natural = atomic_natural_alignment(i.extended_opcode);
        if (natural == 0xffffffffU)
            throw std::runtime_error("invalid atomic instruction subopcode");
        if (i.extended_opcode == 0x03) {
            if (r.byte() != 0)
                throw std::runtime_error("atomic.fence reserved byte is not zero");
        } else {
            i.immediate = r.u32();
            i.secondary = r.u32();
        }
    } else if (op >= 0x02 && op <= 0x04) {
        i.block_type = read_block_type(r);
    } else if (op == 0x0c || op == 0x0d || op == 0x10 || (op >= 0x20 && op <= 0x24)) {
        i.immediate = r.u32();
    } else if (op == 0x0e) {
        uint32_t n = read_count(r, 1);
        if (n == std::numeric_limits<uint32_t>::max())
            throw std::runtime_error("br_table count overflow");
        i.labels.reserve(static_cast<std::size_t>(n) + 1);
        for (uint32_t j = 0; j < n; ++j)
            i.labels.push_back(r.u32());
        i.labels.push_back(r.u32());
    } else if (op == 0x11) {
        i.immediate = r.u32();
        i.secondary = r.byte();
        if (i.secondary != 0)
            throw std::runtime_error("call_indirect reserved byte is not zero");
    } else if (op >= 0x28 && op <= 0x3e) {
        i.immediate = r.u32();
        i.secondary = r.u32();
    } else if (op == 0x3f || op == 0x40) {
        i.immediate = r.byte();
        if (i.immediate != 0)
            throw std::runtime_error("memory instruction reserved byte is not zero");
    } else if (op == 0x41 || op == 0x42) {
        const unsigned int width = op == 0x41 ? 32 : 64;
        uint64_t bits = r.signed_bits(width);
        const unsigned int n = width / 8;
        for (unsigned int j = 0; j < n; ++j)
            i.bytes.push_back(static_cast<unsigned char>((bits >> (8 * j)) & 0xff));
    } else if (op == 0x43 || op == 0x44) {
        r.copy_bytes(i.bytes, op == 0x43 ? 4 : 8);
    } else if (!plain_opcode(op)) {
        throw std::runtime_error("non-MVP or invalid WebAssembly opcode");
    }
    return i;
}

std::vector<Instruction> read_expression(Reader &r) {
    std::vector<Instruction> code;
    std::vector<ControlFrame> stack;
    for (;;) {
        unsigned char op = r.byte();
        if (op == 0x0b) {
            code.push_back(Instruction(op));
            if (stack.empty())
                return code;
            stack.pop_back();
            continue;
        }
        if (op == 0x05) {
            if (stack.empty() || stack.back().opcode != 0x04 || stack.back().has_else)
                throw std::runtime_error("else without matching if");
            stack.back().has_else = true;
            code.push_back(Instruction(op));
            continue;
        }
        Instruction ins = read_instruction(r, op);
        code.push_back(ins);
        if (op >= 0x02 && op <= 0x04)
            stack.push_back(ControlFrame(op));
    }
}

void require_empty(const Reader &r, const char *what) {
    if (!r.empty())
        throw std::runtime_error(std::string("trailing bytes in ") + what + " section");
}

void read_type_section(Reader &r, Module &m) {
    uint32_t n = read_count(r, 3);
    m.types.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        if (r.byte() != 0x60)
            throw std::runtime_error("invalid function type form");
        FunctionType t;
        uint32_t params = read_count(r, 1);
        t.parameters.reserve(params);
        for (uint32_t j = 0; j < params; ++j)
            t.parameters.push_back(read_value_type(r));
        uint32_t results = read_count(r, 1);
        if (results > 1)
            throw std::runtime_error("MVP function type has multiple results");
        t.results.reserve(results);
        for (uint32_t j = 0; j < results; ++j)
            t.results.push_back(read_value_type(r));
        m.types.push_back(t);
    }
    require_empty(r, "type");
}

void read_import_section(Reader &r, Module &m) {
    uint32_t n = read_count(r, 3);
    for (uint32_t i = 0; i < n; ++i) {
        std::string mod = r.string();
        std::string name = r.string();
        unsigned char kind = r.byte();
        if (kind == 0) {
            Function f;
            f.imported = true;
            f.import_module = mod;
            f.import_name = name;
            f.type_index = r.u32();
            m.functions.push_back(f);
        } else if (kind == 1) {
            if (r.byte() != 0x70)
                throw std::runtime_error("invalid table element type");
            Table t;
            t.imported = true;
            t.import_module = mod;
            t.import_name = name;
            read_limits(r, t.limits);
            m.tables.push_back(t);
        } else if (kind == 2) {
            Memory x;
            x.imported = true;
            x.import_module = mod;
            x.import_name = name;
            read_memory_limits(r, x);
            m.memories.push_back(x);
        } else if (kind == 3) {
            Global g;
            g.imported = true;
            g.import_module = mod;
            g.import_name = name;
            g.type = read_value_type(r);
            unsigned char mut = r.byte();
            if (mut > 1)
                throw std::runtime_error("invalid global mutability");
            g.mutable_ = mut != 0;
            m.globals.push_back(g);
        } else {
            throw std::runtime_error("invalid import kind");
        }
    }
    require_empty(r, "import");
}

void read_function_section(Reader &r, Module &m, std::vector<uint32_t> &types) {
    uint32_t n = read_count(r, 1);
    types.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        Function f;
        f.type_index = r.u32();
        types.push_back(f.type_index);
        m.functions.push_back(f);
    }
    require_empty(r, "function");
}

void read_table_section(Reader &r, Module &m) {
    uint32_t n = read_count(r, 2);
    for (uint32_t i = 0; i < n; ++i) {
        if (r.byte() != 0x70)
            throw std::runtime_error("invalid table element type");
        Table t;
        read_limits(r, t.limits);
        m.tables.push_back(t);
    }
    require_empty(r, "table");
}

void read_memory_section(Reader &r, Module &m) {
    uint32_t n = read_count(r, 2);
    for (uint32_t i = 0; i < n; ++i) {
        Memory x;
        read_memory_limits(r, x);
        m.memories.push_back(x);
    }
    require_empty(r, "memory");
}

void read_global_section(Reader &r, Module &m) {
    uint32_t n = read_count(r, 3);
    for (uint32_t i = 0; i < n; ++i) {
        Global g;
        g.type = read_value_type(r);
        unsigned char mut = r.byte();
        if (mut > 1)
            throw std::runtime_error("invalid global mutability");
        g.mutable_ = mut != 0;
        g.init = read_expression(r);
        m.globals.push_back(g);
    }
    require_empty(r, "global");
}

void read_export_section(Reader &r, Module &m) {
    uint32_t n = read_count(r, 3);
    m.exports.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        Export e;
        e.name = r.string();
        e.kind = r.byte();
        if (e.kind > 3)
            throw std::runtime_error("invalid export kind");
        e.index = r.u32();
        m.exports.push_back(e);
    }
    require_empty(r, "export");
}

void read_start_section(Reader &r, Module &m) {
    m.start = r.u32();
    m.has_start = true;
    require_empty(r, "start");
}

void read_element_section(Reader &r, Module &m) {
    uint32_t n = read_count(r, 3);
    m.elements.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        ElementSegment e;
        e.table_index = r.u32();
        e.offset = read_expression(r);
        uint32_t count = read_count(r, 1);
        e.functions.reserve(count);
        for (uint32_t j = 0; j < count; ++j)
            e.functions.push_back(r.u32());
        m.elements.push_back(e);
    }
    require_empty(r, "element");
}

const std::vector<unsigned char> &r_bytes(const Reader &r);

void read_code_section(Reader &r, Module &m, std::size_t defined_functions) {
    uint32_t n = read_count(r, 1);
    if (n != defined_functions)
        throw std::runtime_error("function and code section counts differ");
    std::size_t function_index = 0;
    while (function_index < m.functions.size() && m.functions[function_index].imported)
        ++function_index;
    for (uint32_t i = 0; i < n; ++i, ++function_index) {
        uint32_t body_size = r.u32();
        r.require(body_size);
        const std::size_t body_end = r.position() + body_size;
        Reader body(r_bytes(r), r.position(), body_end);
        uint32_t groups = read_count(body, 2);
        Function &f = m.functions[function_index];
        for (uint32_t j = 0; j < groups; ++j) {
            uint32_t count = body.u32();
            ValueType type = read_value_type(body);
            if (count > 1000000U || f.locals.size() > 1000000U - count)
                throw std::runtime_error("local count exceeds reader safety limit");
            if (static_cast<std::size_t>(count) > f.locals.max_size() - f.locals.size())
                throw std::runtime_error("local vector size overflow");
            f.locals.insert(f.locals.end(), count, Local(std::string(), type));
        }
        f.code = read_expression(body);
        require_empty(body, "function body");
        r.skip(body_size);
    }
    require_empty(r, "code");
}

void read_data_section(Reader &r, Module &m) {
    uint32_t n = read_count(r, 3);
    m.data.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        DataSegment d;
        d.memory_index = r.u32();
        d.offset = read_expression(r);
        uint32_t size = r.u32();
        r.require(size);
        d.bytes.reserve(size);
        for (uint32_t j = 0; j < size; ++j)
            d.bytes.push_back(static_cast<char>(r.byte()));
        m.data.push_back(d);
    }
    require_empty(r, "data");
}

const std::vector<unsigned char> &r_bytes(const Reader &r) {
    return r.bytes();
}

}

Module read_module(const std::vector<unsigned char> &bytes) {
    if (bytes.size() < 8)
        throw std::runtime_error("truncated WebAssembly header");
    if (bytes[0] != 0 || bytes[1] != 0x61 || bytes[2] != 0x73 || bytes[3] != 0x6d)
        throw std::runtime_error("invalid WebAssembly magic");
    if (bytes[4] != 1 || bytes[5] != 0 || bytes[6] != 0 || bytes[7] != 0)
        throw std::runtime_error("unsupported WebAssembly version");

    Reader input(bytes, 8, bytes.size());
    Module module;
    std::vector<uint32_t> defined_types;
    bool seen[12] = {false};
    unsigned char last_section = 0;
    while (!input.empty()) {
        unsigned char id = input.byte();
        uint32_t size = input.u32();
        input.require(size);
        const std::size_t end = input.position() + size;
        Reader section(bytes, input.position(), end);
        if (id == 0) {
            section.string();
            input.skip(size);
            continue;
        }
        if (id > 11)
            throw std::runtime_error("unsupported WebAssembly section");
        if (seen[id])
            throw std::runtime_error("duplicate WebAssembly section");
        if (id < last_section)
            throw std::runtime_error("WebAssembly sections are out of order");
        seen[id] = true;
        last_section = id;
        switch (id) {
        case 1:
            read_type_section(section, module);
            break;
        case 2:
            read_import_section(section, module);
            break;
        case 3:
            read_function_section(section, module, defined_types);
            break;
        case 4:
            read_table_section(section, module);
            break;
        case 5:
            read_memory_section(section, module);
            break;
        case 6:
            read_global_section(section, module);
            break;
        case 7:
            read_export_section(section, module);
            break;
        case 8:
            read_start_section(section, module);
            break;
        case 9:
            read_element_section(section, module);
            break;
        case 10:
            read_code_section(section, module, defined_types.size());
            break;
        case 11:
            read_data_section(section, module);
            break;
        }
        input.skip(size);
    }
    if (defined_types.size() != 0 && !seen[10])
        throw std::runtime_error("missing code section");
    return module;
}

}
