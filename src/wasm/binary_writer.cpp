#include <watx/binary_writer.h>
#include <watx/wasm/opcodes.h>

#include <stdexcept>
#include <string.h>
#include <utility>

namespace watx {

void append_u32(std::vector<unsigned char> &out, unsigned long value) {
    do {
        unsigned char b = static_cast<unsigned char>(value & 0x7fUL);
        value >>= 7;
        if (value)
            b |= 0x80;
        out.push_back(b);
    } while (value);
}
void append_i32(std::vector<unsigned char> &out, long value) {
    bool more = true;
    while (more) {
        unsigned char b = static_cast<unsigned char>(value & 0x7fL);
        value >>= 7;
        bool sign = (b & 0x40) != 0;
        more = !((value == 0 && !sign) || (value == -1 && sign));
        if (more)
            b |= 0x80;
        out.push_back(b);
    }
}
void append_i64(std::vector<unsigned char> &out, int64_t value) {
    bool more = true;
    while (more) {
        unsigned char b = static_cast<unsigned char>(static_cast<uint64_t>(value) & 0x7f);
        value >>= 7;
        bool sign = (b & 0x40) != 0;
        more = !((value == 0 && !sign) || (value == -1 && sign));
        if (more)
            b |= 0x80;
        out.push_back(b);
    }
}
static unsigned char type_byte(ValueType t) {
    switch (t) {
    case TYPE_I32:
        return 0x7f;
    case TYPE_I64:
        return 0x7e;
    case TYPE_F32:
        return 0x7d;
    case TYPE_F64:
        return 0x7c;
    case TYPE_NONE:
        return 0x40;
    }
    throw std::runtime_error("invalid value type in module");
}
static void append_string(std::vector<unsigned char> &out, const std::string &s) {
    append_u32(out, static_cast<unsigned long>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}
static void section(std::vector<unsigned char> &out, unsigned char id,
                    const std::vector<unsigned char> &p) {
    if (p.empty())
        return;
    out.push_back(id);
    append_u32(out, static_cast<unsigned long>(p.size()));
    out.insert(out.end(), p.begin(), p.end());
}
static void append_limits(std::vector<unsigned char> &out, const Limits &l) {
    out.push_back(l.has_maximum ? 1 : 0);
    append_u32(out, l.minimum);
    if (l.has_maximum)
        append_u32(out, l.maximum);
}
static void append_memory_limits(std::vector<unsigned char> &out, const Memory &m) {
    unsigned char flags = m.shared ? 3 : (m.limits.has_maximum ? 1 : 0);
    if (m.shared && !m.limits.has_maximum)
        throw std::runtime_error("shared memory requires an explicit maximum");
    out.push_back(flags);
    append_u32(out, m.limits.minimum);
    if (m.limits.has_maximum)
        append_u32(out, m.limits.maximum);
}
static void append_type(std::vector<unsigned char> &out, const FunctionType &t) {
    if (t.results.size() > 1)
        throw std::runtime_error("MVP function type has more than one result");
    out.push_back(0x60);
    append_u32(out, static_cast<unsigned long>(t.parameters.size()));
    for (std::size_t i = 0; i < t.parameters.size(); ++i)
        out.push_back(type_byte(t.parameters[i]));
    append_u32(out, static_cast<unsigned long>(t.results.size()));
    for (std::size_t i = 0; i < t.results.size(); ++i)
        out.push_back(type_byte(t.results[i]));
}
static void append_instruction(std::vector<unsigned char> &out, const Instruction &i) {
    if (i.opcode != 0xfe && i.extended_opcode != 0)
        throw std::runtime_error("extended opcode metadata on a non-extended instruction");
    out.push_back(i.opcode);
    switch (i.opcode) {
    case 0x02:
    case 0x03:
    case 0x04:
        out.push_back(type_byte(i.block_type));
        break;
    case 0x0c:
    case 0x0d:
    case 0x10:
    case 0x20:
    case 0x21:
    case 0x22:
    case 0x23:
    case 0x24:
        append_u32(out, i.immediate);
        break;
    case 0x0e:
        if (i.labels.empty())
            throw std::runtime_error("br_table has no default label");
        append_u32(out, static_cast<unsigned long>(i.labels.size() - 1));
        for (std::size_t n = 0; n + 1 < i.labels.size(); ++n)
            append_u32(out, i.labels[n]);
        append_u32(out, i.labels.back());
        break;
    case 0x11:
        append_u32(out, i.immediate);
        append_u32(out, i.secondary);
        break;
    case 0x28:
    case 0x29:
    case 0x2a:
    case 0x2b:
    case 0x2c:
    case 0x2d:
    case 0x2e:
    case 0x2f:
    case 0x30:
    case 0x31:
    case 0x32:
    case 0x33:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x38:
    case 0x39:
    case 0x3a:
    case 0x3b:
    case 0x3c:
    case 0x3d:
    case 0x3e:
        append_u32(out, i.immediate);
        append_u32(out, i.secondary);
        break;
    case 0x3f:
    case 0x40:
        append_u32(out, i.immediate);
        break;
    case 0xfe:
        if (atomic_natural_alignment(i.extended_opcode) == 0xffffffffU)
            throw std::runtime_error("invalid atomic instruction subopcode");
        append_u32(out, i.extended_opcode);
        if (i.extended_opcode == 0x03) {
            if (i.immediate != 0 || i.secondary != 0 || !i.bytes.empty())
                throw std::runtime_error("atomic.fence has invalid immediates");
            out.push_back(0);
        } else {
            if (i.immediate != atomic_natural_alignment(i.extended_opcode) || !i.bytes.empty())
                throw std::runtime_error("atomic instruction has invalid immediates");
            append_u32(out, i.immediate);
            append_u32(out, i.secondary);
        }
        break;
    case 0x41: {
        if (i.bytes.size() != 4)
            throw std::runtime_error("i32 constant has invalid byte width");
        uint32_t bits =
            static_cast<uint32_t>(i.bytes[0]) | (static_cast<uint32_t>(i.bytes[1]) << 8) |
            (static_cast<uint32_t>(i.bytes[2]) << 16) | (static_cast<uint32_t>(i.bytes[3]) << 24);
        int32_t signed_bits;
        memcpy(&signed_bits, &bits, sizeof(signed_bits));
        append_i32(out, static_cast<long>(signed_bits));
        break;
    }
    case 0x42: {
        if (i.bytes.size() != 8)
            throw std::runtime_error("i64 constant has invalid byte width");
        uint64_t bits = 0;
        for (unsigned int n = 0; n < 8; ++n)
            bits |= static_cast<uint64_t>(i.bytes[n]) << (8 * n);
        int64_t signed_bits;
        memcpy(&signed_bits, &bits, sizeof(signed_bits));
        append_i64(out, signed_bits);
        break;
    }
    case 0x43:
        if (i.bytes.size() != 4)
            throw std::runtime_error("f32 constant has invalid byte width");
        out.insert(out.end(), i.bytes.begin(), i.bytes.end());
        break;
    case 0x44:
        if (i.bytes.size() != 8)
            throw std::runtime_error("f64 constant has invalid byte width");
        out.insert(out.end(), i.bytes.begin(), i.bytes.end());
        break;
    default:
        if (i.opcode > 0xbf)
            throw std::runtime_error("non-MVP opcode in MVP module");
        if (!i.bytes.empty())
            throw std::runtime_error("unexpected instruction immediate bytes");
    }
}
static std::vector<unsigned char> expression(const std::vector<Instruction> &code) {
    std::vector<unsigned char> out;
    for (std::size_t i = 0; i < code.size(); ++i)
        append_instruction(out, code[i]);
    return out;
}

std::vector<unsigned char> write_module(const Module &m) {
    std::vector<unsigned char> out;
    const unsigned char header[] = {0, 0x61, 0x73, 0x6d, 1, 0, 0, 0};
    out.insert(out.end(), header, header + 8);
    if (!m.types.empty()) {
        std::vector<unsigned char> p;
        append_u32(p, static_cast<unsigned long>(m.types.size()));
        for (std::size_t i = 0; i < m.types.size(); ++i)
            append_type(p, m.types[i]);
        section(out, 1, p);
    }
    unsigned long imports = 0;
    for (std::size_t i = 0; i < m.functions.size(); ++i)
        if (m.functions[i].imported)
            ++imports;
    for (std::size_t i = 0; i < m.tables.size(); ++i)
        if (m.tables[i].imported)
            ++imports;
    for (std::size_t i = 0; i < m.memories.size(); ++i)
        if (m.memories[i].imported)
            ++imports;
    for (std::size_t i = 0; i < m.globals.size(); ++i)
        if (m.globals[i].imported)
            ++imports;
    if (imports) {
        std::vector<unsigned char> p;
        append_u32(p, imports);
        for (std::size_t i = 0; i < m.functions.size(); ++i)
            if (m.functions[i].imported) {
                const Function &f = m.functions[i];
                append_string(p, f.import_module);
                append_string(p, f.import_name);
                p.push_back(0);
                append_u32(p, f.type_index);
            }
        for (std::size_t i = 0; i < m.tables.size(); ++i)
            if (m.tables[i].imported) {
                const Table &t = m.tables[i];
                append_string(p, t.import_module);
                append_string(p, t.import_name);
                p.push_back(1);
                p.push_back(0x70);
                append_limits(p, t.limits);
            }
        for (std::size_t i = 0; i < m.memories.size(); ++i)
            if (m.memories[i].imported) {
                const Memory &x = m.memories[i];
                append_string(p, x.import_module);
                append_string(p, x.import_name);
                p.push_back(2);
                append_memory_limits(p, x);
            }
        for (std::size_t i = 0; i < m.globals.size(); ++i)
            if (m.globals[i].imported) {
                const Global &g = m.globals[i];
                append_string(p, g.import_module);
                append_string(p, g.import_name);
                p.push_back(3);
                p.push_back(type_byte(g.type));
                p.push_back(g.mutable_ ? 1 : 0);
            }
        section(out, 2, p);
    }
    unsigned long funcs = 0;
    for (std::size_t i = 0; i < m.functions.size(); ++i)
        if (!m.functions[i].imported)
            ++funcs;
    if (funcs) {
        std::vector<unsigned char> p;
        append_u32(p, funcs);
        for (std::size_t i = 0; i < m.functions.size(); ++i)
            if (!m.functions[i].imported)
                append_u32(p, m.functions[i].type_index);
        section(out, 3, p);
    }
    unsigned long tables = 0;
    for (std::size_t i = 0; i < m.tables.size(); ++i)
        if (!m.tables[i].imported)
            ++tables;
    if (tables) {
        std::vector<unsigned char> p;
        append_u32(p, tables);
        for (std::size_t i = 0; i < m.tables.size(); ++i)
            if (!m.tables[i].imported) {
                p.push_back(0x70);
                append_limits(p, m.tables[i].limits);
            }
        section(out, 4, p);
    }
    unsigned long memories = 0;
    for (std::size_t i = 0; i < m.memories.size(); ++i)
        if (!m.memories[i].imported)
            ++memories;
    if (memories) {
        std::vector<unsigned char> p;
        append_u32(p, memories);
        for (std::size_t i = 0; i < m.memories.size(); ++i)
            if (!m.memories[i].imported)
                append_memory_limits(p, m.memories[i]);
        section(out, 5, p);
    }
    unsigned long globals = 0;
    for (std::size_t i = 0; i < m.globals.size(); ++i)
        if (!m.globals[i].imported)
            ++globals;
    if (globals) {
        std::vector<unsigned char> p;
        append_u32(p, globals);
        for (std::size_t i = 0; i < m.globals.size(); ++i)
            if (!m.globals[i].imported) {
                p.push_back(type_byte(m.globals[i].type));
                p.push_back(m.globals[i].mutable_ ? 1 : 0);
                std::vector<unsigned char> e = expression(m.globals[i].init);
                p.insert(p.end(), e.begin(), e.end());
            }
        section(out, 6, p);
    }
    if (!m.exports.empty()) {
        std::vector<unsigned char> p;
        append_u32(p, static_cast<unsigned long>(m.exports.size()));
        for (std::size_t i = 0; i < m.exports.size(); ++i) {
            append_string(p, m.exports[i].name);
            p.push_back(m.exports[i].kind);
            append_u32(p, m.exports[i].index);
        }
        section(out, 7, p);
    }
    if (m.has_start) {
        std::vector<unsigned char> p;
        append_u32(p, m.start);
        section(out, 8, p);
    }
    if (!m.elements.empty()) {
        std::vector<unsigned char> p;
        append_u32(p, static_cast<unsigned long>(m.elements.size()));
        for (std::size_t i = 0; i < m.elements.size(); ++i) {
            append_u32(p, m.elements[i].table_index);
            std::vector<unsigned char> e = expression(m.elements[i].offset);
            p.insert(p.end(), e.begin(), e.end());
            append_u32(p, static_cast<unsigned long>(m.elements[i].functions.size()));
            for (std::size_t j = 0; j < m.elements[i].functions.size(); ++j)
                append_u32(p, m.elements[i].functions[j]);
        }
        section(out, 9, p);
    }
    if (funcs) {
        std::vector<unsigned char> p;
        append_u32(p, funcs);
        for (std::size_t i = 0; i < m.functions.size(); ++i)
            if (!m.functions[i].imported) {
                std::vector<unsigned char> body;
                std::vector<std::pair<unsigned long, ValueType> > groups;
                for (std::size_t j = 0; j < m.functions[i].locals.size(); ++j) {
                    if (!groups.empty() && groups.back().second == m.functions[i].locals[j].type)
                        ++groups.back().first;
                    else
                        groups.push_back(std::make_pair(1UL, m.functions[i].locals[j].type));
                }
                append_u32(body, static_cast<unsigned long>(groups.size()));
                for (std::size_t j = 0; j < groups.size(); ++j) {
                    append_u32(body, groups[j].first);
                    body.push_back(type_byte(groups[j].second));
                }
                std::vector<unsigned char> e = expression(m.functions[i].code);
                body.insert(body.end(), e.begin(), e.end());
                append_u32(p, static_cast<unsigned long>(body.size()));
                p.insert(p.end(), body.begin(), body.end());
            }
        section(out, 10, p);
    }
    if (!m.data.empty()) {
        std::vector<unsigned char> p;
        append_u32(p, static_cast<unsigned long>(m.data.size()));
        for (std::size_t i = 0; i < m.data.size(); ++i) {
            append_u32(p, m.data[i].memory_index);
            std::vector<unsigned char> e = expression(m.data[i].offset);
            p.insert(p.end(), e.begin(), e.end());
            append_u32(p, static_cast<unsigned long>(m.data[i].bytes.size()));
            p.insert(p.end(), m.data[i].bytes.begin(), m.data[i].bytes.end());
        }
        section(out, 11, p);
    }
    return out;
}

}
