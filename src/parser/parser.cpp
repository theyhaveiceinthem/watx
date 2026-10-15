#include <watx/parser.h>
#include <watx/text/literals.h>

#include <stdexcept>

namespace watx {

static std::string atom(const Node &n, const char *what) {
    if (n.list)
        throw std::runtime_error(std::string("expected ") + what);
    return n.text;
}
static bool named(const Node &n) {
    return !n.list && n.text.size() > 0 && n.text[0] == '$';
}
static bool is_head(const Node &n, const char *s) {
    return is_form(n, s);
}
static std::string string_value(const Node &n, const char *what) {
    return decode_string(atom(n, what));
}
static uint32_t natural(const Node &n, const char *what) {
    try {
        return parse_natural(atom(n, what));
    } catch (const std::exception &) {
        throw std::runtime_error(std::string("invalid ") + what + " '" + n.text + "'");
    }
}
static ValueType valtype(const Node &n) {
    try {
        return value_type(atom(n, "value type"));
    } catch (const std::exception &) {
        throw std::runtime_error("unsupported value type '" + n.text + "'");
    }
}
static void require_count(const Node &n, std::size_t min, std::size_t max, const char *what) {
    if (n.children.size() < min || n.children.size() > max)
        throw std::runtime_error(std::string("invalid ") + what + " form");
}
static uint32_t resolve_index(const Module &m, const Node &n, const char *space) {
    std::string name = atom(n, "index or identifier");
    if (std::string(space) == "function")
        return function_index(m, name);
    if (std::string(space) == "type")
        return type_index(m, name);
    if (std::string(space) == "global")
        return global_index(m, name);
    if (std::string(space) == "table")
        return table_index(m, name);
    if (std::string(space) == "memory")
        return memory_index(m, name);
    throw std::runtime_error("unknown index space");
}

struct Signature {
    std::vector<ValueType> params, results;
    std::vector<std::string> names;
};
static void parse_signature_fields(const Node &form, Signature &sig, bool allow_locals,
                                   std::vector<Local> *locals, bool &has_param, bool &has_result) {
    std::size_t first = form.children.size() > 1 && named(form.children[1]) ? 2 : 1;
    bool saw_result = false, saw_local = false;
    for (std::size_t i = first; i < form.children.size(); ++i) {
        const Node &field = form.children[i];
        if (!field.list)
            continue;
        if (is_head(field, "param")) {
            if (saw_result || saw_local)
                throw std::runtime_error("parameter declarations must precede results and locals");
            has_param = true;
            std::size_t j = 1;
            if (j < field.children.size() && named(field.children[j])) {
                std::string name = field.children[j++].text;
                if (j >= field.children.size() || j + 1 != field.children.size())
                    throw std::runtime_error("a named parameter must have exactly one value type");
                sig.params.push_back(valtype(field.children[j]));
                sig.names.push_back(name);
            } else {
                for (; j < field.children.size(); ++j) {
                    sig.params.push_back(valtype(field.children[j]));
                    sig.names.push_back(std::string());
                }
            }
        } else if (is_head(field, "result")) {
            if (saw_local)
                throw std::runtime_error("result declarations must precede locals");
            saw_result = true;
            has_result = true;
            for (std::size_t j = 1; j < field.children.size(); ++j)
                sig.results.push_back(valtype(field.children[j]));
        } else if (is_head(field, "local") && allow_locals) {
            saw_local = true;
            std::size_t j = 1;
            if (j < field.children.size() && named(field.children[j])) {
                std::string name = field.children[j++].text;
                if (j >= field.children.size() || j + 1 != field.children.size())
                    throw std::runtime_error("a named local must have exactly one value type");
                locals->push_back(Local(name, valtype(field.children[j])));
            } else {
                for (; j < field.children.size(); ++j)
                    locals->push_back(Local(std::string(), valtype(field.children[j])));
            }
        }
    }
}
static FunctionType make_type(const Signature &s, const std::string &name) {
    FunctionType t;
    t.name = name;
    t.parameters = s.params;
    t.results = s.results;
    return t;
}
static void check_signature(const Signature &a, const Signature &b) {
    if (a.params != b.params || a.results != b.results)
        throw std::runtime_error("type use does not match inline function signature");
}
static void add_inline_exports(Module &m, const Node &form, unsigned char kind, uint32_t index) {
    for (std::size_t i = 1; i < form.children.size(); ++i) {
        const Node &f = form.children[i];
        if (is_head(f, "export")) {
            require_count(f, 2, 2, "inline export");
            Export e;
            e.name = string_value(f.children[1], "export name");
            e.kind = kind;
            e.index = index;
            m.exports.push_back(e);
        }
    }
}
static bool is_func_header_field(const Node &n) {
    return is_head(n, "type") || is_head(n, "param") || is_head(n, "result") ||
           is_head(n, "local") || is_head(n, "import") || is_head(n, "export");
}
static std::size_t function_header_end(const Node &n, std::size_t i) {
    std::size_t end = i;
    while (end < n.children.size() && is_func_header_field(n.children[end]))
        ++end;
    return end;
}
static bool has_defined_function(const Module &m) {
    for (std::size_t i = 0; i < m.functions.size(); ++i)
        if (!m.functions[i].imported)
            return true;
    return false;
}
static bool has_defined_table(const Module &m) {
    for (std::size_t i = 0; i < m.tables.size(); ++i)
        if (!m.tables[i].imported)
            return true;
    return false;
}
static bool has_defined_memory(const Module &m) {
    for (std::size_t i = 0; i < m.memories.size(); ++i)
        if (!m.memories[i].imported)
            return true;
    return false;
}
static bool has_defined_global(const Module &m) {
    for (std::size_t i = 0; i < m.globals.size(); ++i)
        if (!m.globals[i].imported)
            return true;
    return false;
}
static bool has_any_definition(const Module &m) {
    return has_defined_function(m) || has_defined_table(m) || has_defined_memory(m) ||
           has_defined_global(m);
}
static void limits_from(const Node &desc, std::size_t start, Limits &limits, bool &shared) {
    if (start >= desc.children.size())
        throw std::runtime_error("limits require a minimum");
    limits.minimum = natural(desc.children[start++], "minimum limit");
    if (start < desc.children.size() && atom(desc.children[start], "memory limit") != "shared") {
        limits.maximum = natural(desc.children[start++], "maximum limit");
        limits.has_maximum = true;
    }
    if (start < desc.children.size() &&
        atom(desc.children[start], "memory shared marker") == "shared") {
        shared = true;
        ++start;
    }
    if (start != desc.children.size())
        throw std::runtime_error("invalid limits");
    if (shared && !limits.has_maximum)
        throw std::runtime_error("shared memory requires an explicit maximum");
    if (limits.has_maximum && limits.maximum < limits.minimum)
        throw std::runtime_error("maximum limit is less than minimum");
}
static void table_limits_from(const Node &desc, std::size_t start, Limits &limits) {
    if (start >= desc.children.size())
        throw std::runtime_error("table requires limits and funcref element type");
    limits.minimum = natural(desc.children[start++], "table minimum");
    if (start < desc.children.size() && !desc.children[start].list &&
        desc.children[start].text != "funcref" && desc.children[start].text != "anyfunc") {
        limits.maximum = natural(desc.children[start++], "table maximum");
        limits.has_maximum = true;
    }
    if (start >= desc.children.size())
        throw std::runtime_error("MVP tables require a function reference element type");
    std::string elem = atom(desc.children[start], "table element type");
    if (elem != "funcref" && elem != "anyfunc")
        throw std::runtime_error("MVP tables require funcref elements");
    ++start;
    if (start != desc.children.size())
        throw std::runtime_error("invalid table type");
    if (limits.has_maximum && limits.maximum < limits.minimum)
        throw std::runtime_error("table maximum is less than minimum");
}
static void raw_expr(const Node &n, std::vector<Node> &out, const char *what) {
    if (!n.list)
        throw std::runtime_error(std::string(what) + " must be an instruction expression");
    out.push_back(n);
}
static void raw_offset_expr(const Node &n, std::vector<Node> &out, const char *what) {
    if (is_head(n, "offset")) {
        if (n.children.size() < 2)
            throw std::runtime_error(std::string(what) + " is empty");
        for (std::size_t i = 1; i < n.children.size(); ++i)
            out.push_back(n.children[i]);
    } else
        raw_expr(n, out, what);
}
static void raw_offset_field(const std::vector<Node> &fields, std::size_t &i,
                             std::vector<Node> &out, const char *what) {
    if (i >= fields.size())
        throw std::runtime_error(std::string(what) + " is missing");
    const Node &n = fields[i];
    if (is_head(n, "offset")) {
        raw_offset_expr(n, out, what);
        ++i;
        return;
    }
    if (n.list) {
        raw_expr(n, out, what);
        ++i;
        return;
    }
    if (n.text == "i32.const") {
        if (i + 1 >= fields.size() || fields[i + 1].list)
            throw std::runtime_error(std::string(what) + " i32.const is missing its immediate");
        out.push_back(fields[i++]);
        out.push_back(fields[i++]);
        return;
    }
    throw std::runtime_error(std::string(what) + " must be a single instruction");
}
static void parse_type_decl(const Node &n, Module &m) {
    require_count(n, 2, 1000000, "type declaration");
    std::string name;
    std::size_t i = 1;
    if (named(n.children[i]))
        name = n.children[i++].text;
    if (i >= n.children.size() || !is_head(n.children[i], "func"))
        throw std::runtime_error("type declaration must contain a func type");
    if (i + 1 != n.children.size())
        throw std::runtime_error("type declaration must contain exactly one func type");
    Signature sig;
    bool hp = false, hr = false;
    Node func = n.children[i];
    for (std::size_t j = 1; j < func.children.size(); ++j) {
        if (!func.children[j].list ||
            (!is_head(func.children[j], "param") && !is_head(func.children[j], "result")))
            throw std::runtime_error("invalid field in function type");
    }
    parse_signature_fields(func, sig, false, 0, hp, hr);
    FunctionType t = make_type(sig, name);
    if (!name.empty())
        for (std::size_t j = 0; j < m.types.size(); ++j)
            if (m.types[j].name == name)
                throw std::runtime_error("duplicate type identifier '" + name + "'");
    m.types.push_back(t);
}
static void parse_import(const Node &n, Module &m) {
    require_count(n, 4, 4, "import");
    std::string mod = string_value(n.children[1], "import module name");
    std::string field = string_value(n.children[2], "import field name");
    const Node &d = n.children[3];
    if (!d.list || d.children.size() < 1)
        throw std::runtime_error("invalid import descriptor");
    if (is_head(d, "func")) {
        if (has_any_definition(m))
            throw std::runtime_error("imports must precede module definitions");
        Function f;
        f.imported = true;
        f.import_module = mod;
        f.import_name = field;
        std::size_t first = 1;
        if (d.children.size() > 1 && named(d.children[1])) {
            f.name = d.children[1].text;
            first = 2;
        }
        for (std::size_t i = first; i < d.children.size(); ++i) {
            const Node &field_node = d.children[i];
            if (!field_node.list || !(is_head(field_node, "type") || is_head(field_node, "param") ||
                                      is_head(field_node, "result")))
                throw std::runtime_error("invalid field in imported function descriptor");
        }
        Signature sig;
        bool hp = false, hr = false;
        parse_signature_fields(d, sig, true, &f.locals, hp, hr);
        if (!f.locals.empty())
            throw std::runtime_error("imported function cannot declare locals");
        bool has_type = false;
        uint32_t ti = 0;
        Signature inline_sig;
        bool inline_seen = false;
        for (std::size_t i = 1; i < d.children.size(); ++i) {
            const Node &x = d.children[i];
            if (is_head(x, "type")) {
                require_count(x, 2, 2, "type use");
                if (has_type)
                    throw std::runtime_error("duplicate type use");
                if (inline_seen)
                    throw std::runtime_error(
                        "type use must precede inline parameter, result, and local declarations");
                ti = resolve_index(m, x.children[1], "type");
                has_type = true;
            } else if (is_head(x, "param") || is_head(x, "result"))
                inline_seen = true;
            else if (is_head(x, "local"))
                throw std::runtime_error("imported function cannot declare locals");
            else if (x.list)
                throw std::runtime_error("unknown imported function type field '" + form_head(x) +
                                         "'");
        }
        if (has_type) {
            inline_sig.params = sig.params;
            inline_sig.results = sig.results;
            const FunctionType &t = m.types[ti];
            Signature explicit_sig;
            explicit_sig.params = t.parameters;
            explicit_sig.results = t.results;
            if (!inline_sig.params.empty() || !inline_sig.results.empty())
                check_signature(explicit_sig, inline_sig);
            f.type_index = ti;
            if (!hp) {
                sig.params = t.parameters;
                sig.names.resize(sig.params.size());
            }
            if (!hr)
                sig.results = t.results;
        } else
            f.type_index = intern_type(m, make_type(sig, ""));
        f.parameter_names = sig.names;
        m.functions.push_back(f);
        return;
    }
    if (is_head(d, "table")) {
        if (has_any_definition(m))
            throw std::runtime_error("imports must precede module definitions");
        Table t;
        t.imported = true;
        t.import_module = mod;
        t.import_name = field;
        std::size_t i = 1;
        if (i < d.children.size() && named(d.children[i])) {
            t.name = d.children[i++].text;
        }
        table_limits_from(d, i, t.limits);
        m.tables.push_back(t);
        return;
    }
    if (is_head(d, "memory")) {
        if (has_any_definition(m))
            throw std::runtime_error("imports must precede module definitions");
        Memory x;
        x.imported = true;
        x.import_module = mod;
        x.import_name = field;
        std::size_t i = 1;
        if (i < d.children.size() && named(d.children[i]))
            x.name = d.children[i++].text;
        limits_from(d, i, x.limits, x.shared);
        m.memories.push_back(x);
        return;
    }
    if (is_head(d, "global")) {
        if (has_any_definition(m))
            throw std::runtime_error("imports must precede module definitions");
        Global g;
        g.imported = true;
        g.import_module = mod;
        g.import_name = field;
        std::size_t i = 1;
        if (i < d.children.size() && named(d.children[i]))
            g.name = d.children[i++].text;
        if (i >= d.children.size())
            throw std::runtime_error("global import missing type");
        if (is_head(d.children[i], "mut")) {
            require_count(d.children[i], 2, 2, "mutable global type");
            g.mutable_ = true;
            g.type = valtype(d.children[i].children[1]);
            ++i;
        } else
            g.type = valtype(d.children[i++]);
        if (i != d.children.size())
            throw std::runtime_error("invalid global import");
        m.globals.push_back(g);
        return;
    }
    throw std::runtime_error("unsupported import descriptor '" + form_head(d) + "'");
}

static void parse_func(const Node &n, Module &m) {
    Function f;
    std::size_t i = 1;
    if (i < n.children.size() && named(n.children[i]))
        f.name = n.children[i++].text;

    std::size_t header_end = function_header_end(n, i);
    Node header;
    header.list = true;
    for (std::size_t j = 0; j < header_end; ++j)
        header.children.push_back(n.children[j]);
    Signature sig;
    bool hp = false, hr = false;
    parse_signature_fields(header, sig, true, &f.locals, hp, hr);
    bool has_type = false;
    uint32_t ti = 0;
    bool inline_seen = false;
    for (std::size_t j = i; j < header_end; ++j) {
        if (is_head(n.children[j], "type")) {
            require_count(n.children[j], 2, 2, "type use");
            if (has_type)
                throw std::runtime_error("duplicate type use");
            if (inline_seen)
                throw std::runtime_error(
                    "type use must precede inline parameter, result, and local declarations");
            ti = resolve_index(m, n.children[j].children[1], "type");
            has_type = true;
        } else if (is_head(n.children[j], "param") || is_head(n.children[j], "result") ||
                   is_head(n.children[j], "local"))
            inline_seen = true;
    }
    bool imported = false;
    for (std::size_t j = i; j < header_end; ++j)
        if (is_head(n.children[j], "import")) {
            if (imported)
                throw std::runtime_error("duplicate inline import");
            require_count(n.children[j], 3, 3, "inline import");
            imported = true;
            f.imported = true;
            f.import_module = string_value(n.children[j].children[1], "import module name");
            f.import_name = string_value(n.children[j].children[2], "import field name");
        }
    if (imported && has_any_definition(m))
        throw std::runtime_error("imports must precede module definitions");
    if (imported)
        for (std::size_t j = i; j < header_end; ++j)
            if (is_head(n.children[j], "local"))
                throw std::runtime_error("imported function cannot declare locals");
    if (has_type) {
        Signature existing;
        existing.params = m.types[ti].parameters;
        existing.results = m.types[ti].results;
        if (!sig.params.empty() || !sig.results.empty())
            check_signature(existing, sig);
        if (!hp) {
            sig.params = existing.params;
            sig.names.resize(sig.params.size());
        }
        if (!hr)
            sig.results = existing.results;
        f.type_index = ti;
    } else
        f.type_index = intern_type(m, make_type(sig, ""));
    f.parameter_names = sig.names;
    if (imported && header_end != n.children.size())
        throw std::runtime_error("imported function cannot have a body");
    for (std::size_t j = header_end; j < n.children.size(); ++j)
        f.raw_body.push_back(n.children[j]);
    uint32_t index = static_cast<uint32_t>(m.functions.size());
    m.functions.push_back(f);
    add_inline_exports(m, header, 0, index);
}
static void prepass_function_type(Module &m, const Node &form, std::size_t first, std::size_t end) {
    bool has_explicit_type = false;
    for (std::size_t i = first; i < end; ++i)
        if (is_head(form.children[i], "type"))
            has_explicit_type = true;
    if (has_explicit_type)
        return;
    Node header;
    header.list = true;
    for (std::size_t i = 0; i < end; ++i)
        header.children.push_back(form.children[i]);
    Signature sig;
    bool has_param = false, has_result = false;
    std::vector<Local> locals;
    parse_signature_fields(header, sig, true, &locals, has_param, has_result);
    intern_type(m, make_type(sig, std::string()));
}
static bool is_call_indirect_clause(const Node &n) {
    return is_head(n, "type") || is_head(n, "param") || is_head(n, "result") || is_head(n, "table");
}
static std::size_t prepass_call_indirect(Module &m, const std::vector<Node> &nodes, std::size_t p) {
    FunctionType signature;
    bool has_explicit_type = false;
    bool saw_param = false, saw_result = false;
    while (p < nodes.size() && nodes[p].list && is_call_indirect_clause(nodes[p])) {
        const Node &clause = nodes[p];
        if (is_head(clause, "type")) {
            if (has_explicit_type || saw_param || saw_result)
                throw std::runtime_error("type clause must precede inline type-use clauses");
            require_count(clause, 2, 2, "type use");
            has_explicit_type = true;
        } else if (is_head(clause, "param")) {
            if (saw_result)
                throw std::runtime_error("parameter clause cannot follow a result clause");
            saw_param = true;
            for (std::size_t i = 1; i < clause.children.size(); ++i) {
                if (clause.children[i].list || named(clause.children[i]))
                    throw std::runtime_error(
                        "call_indirect parameters must be unnamed value types");
                signature.parameters.push_back(valtype(clause.children[i]));
            }
        } else if (is_head(clause, "result")) {
            saw_result = true;
            if (clause.children.size() > 2 ||
                (clause.children.size() == 2 && !signature.results.empty()))
                throw std::runtime_error("MVP indirect call type has at most one result");
            if (clause.children.size() == 2)
                signature.results.push_back(valtype(clause.children[1]));
        }
        ++p;
    }
    if (!has_explicit_type)
        intern_type(m, signature);
    return p;
}
static void prepass_instruction_node(Module &m, const Node &node);
static void prepass_instruction_sequence(Module &m, const std::vector<Node> &nodes) {
    for (std::size_t i = 0; i < nodes.size();) {
        if (!nodes[i].list && nodes[i].text == "call_indirect") {
            i = prepass_call_indirect(m, nodes, i + 1);
        } else {
            if (nodes[i].list)
                prepass_instruction_node(m, nodes[i]);
            ++i;
        }
    }
}
static void prepass_instruction_node(Module &m, const Node &node) {
    if (node.children.empty())
        return;
    if (is_head(node, "call_indirect")) {
        std::size_t p = prepass_call_indirect(m, node.children, 1);
        for (; p < node.children.size(); ++p)
            if (node.children[p].list)
                prepass_instruction_node(m, node.children[p]);
        return;
    }
    prepass_instruction_sequence(m, node.children);
}
static void prepass_implicit_types(Module &m, const Node &root, std::size_t start) {
    for (std::size_t i = start; i < root.children.size(); ++i) {
        const Node &field = root.children[i];
        if (is_head(field, "func")) {
            std::size_t first = 1;
            if (first < field.children.size() && named(field.children[first]))
                ++first;
            std::size_t end = function_header_end(field, first);
            prepass_function_type(m, field, 0, end);
            prepass_instruction_sequence(
                m, std::vector<Node>(field.children.begin() + end, field.children.end()));
        } else if (is_head(field, "import") && field.children.size() == 4 &&
                   is_head(field.children[3], "func")) {
            const Node &descriptor = field.children[3];
            prepass_function_type(m, descriptor, 0, descriptor.children.size());
        }
    }
}
struct InlineSegment {
    unsigned char kind;
    uint32_t index;
    std::size_t owner;
    Node form;
};
static void parse_table(const Node &n, Module &m, std::vector<InlineSegment> &segments,
                        std::size_t owner) {
    Table t;
    std::size_t i = 1;
    if (i < n.children.size() && named(n.children[i]))
        t.name = n.children[i++].text;
    for (std::size_t j = i; j < n.children.size(); ++j)
        if (is_head(n.children[j], "import")) {
            if (t.imported)
                throw std::runtime_error("duplicate inline import");
            require_count(n.children[j], 3, 3, "inline import");
            if (has_any_definition(m))
                throw std::runtime_error("imports must precede module definitions");
            t.imported = true;
            t.import_module = string_value(n.children[j].children[1], "import module name");
            t.import_name = string_value(n.children[j].children[2], "import field name");
        }
    if (i >= n.children.size())
        throw std::runtime_error("table missing limits");
    Node type;
    type.list = true;
    type.children.push_back(Node("table"));
    if (!t.name.empty())
        type.children.push_back(Node(t.name));
    for (std::size_t j = i; j < n.children.size(); ++j)
        if (!is_head(n.children[j], "elem") && !is_head(n.children[j], "export") &&
            !is_head(n.children[j], "import"))
            type.children.push_back(n.children[j]);
    std::size_t elem_type_index = t.name.empty() ? 1 : 2;
    if (elem_type_index < type.children.size() && !type.children[elem_type_index].list &&
        (type.children[elem_type_index].text == "funcref" ||
         type.children[elem_type_index].text == "anyfunc")) {
        std::size_t inferred = 0;
        bool has_elem = false;
        for (std::size_t j = 1; j < n.children.size(); ++j)
            if (is_head(n.children[j], "elem")) {
                has_elem = true;
                const Node &el = n.children[j];
                std::size_t k = 1;
                if (k < el.children.size() && !el.children[k].list && el.children[k].text == "func")
                    ++k;
                inferred += el.children.size() - k;
            }
        if (!has_elem)
            throw std::runtime_error("table shorthand requires an inline elem segment");
        if (inferred > 0xffffffffUL)
            throw std::runtime_error("inline table is too large");
        t.limits.minimum = static_cast<uint32_t>(inferred);
    } else
        table_limits_from(type, t.name.empty() ? 1 : 2, t.limits);
    uint32_t idx = static_cast<uint32_t>(m.tables.size());
    m.tables.push_back(t);
    add_inline_exports(m, n, 1, idx);
    for (std::size_t j = 1; j < n.children.size(); ++j)
        if (is_head(n.children[j], "elem")) {
            InlineSegment s;
            s.kind = 1;
            s.index = idx;
            s.owner = owner;
            s.form = n.children[j];
            segments.push_back(s);
        }
}
static void parse_memory(const Node &n, Module &m, std::vector<InlineSegment> &segments,
                         std::size_t owner) {
    Memory x;
    std::size_t i = 1;
    if (i < n.children.size() && named(n.children[i]))
        x.name = n.children[i++].text;
    for (std::size_t j = i; j < n.children.size(); ++j)
        if (is_head(n.children[j], "import")) {
            if (x.imported)
                throw std::runtime_error("duplicate inline import");
            require_count(n.children[j], 3, 3, "inline import");
            if (has_any_definition(m))
                throw std::runtime_error("imports must precede module definitions");
            x.imported = true;
            x.import_module = string_value(n.children[j].children[1], "import module name");
            x.import_name = string_value(n.children[j].children[2], "import field name");
        }
    Node type;
    type.list = true;
    type.children.push_back(Node("memory"));
    if (!x.name.empty())
        type.children.push_back(Node(x.name));
    std::size_t data_bytes = 0;
    bool has_inline_data = false;
    for (std::size_t j = i; j < n.children.size(); ++j)
        if (is_head(n.children[j], "data")) {
            has_inline_data = true;
            std::size_t segment_bytes = 0;
            for (std::size_t k = 1; k < n.children[j].children.size(); ++k)
                segment_bytes += string_value(n.children[j].children[k], "data string").size();
            if (segment_bytes > data_bytes)
                data_bytes = segment_bytes;
        } else if (!is_head(n.children[j], "export") && !is_head(n.children[j], "import"))
            type.children.push_back(n.children[j]);
    std::size_t min_pos = x.name.empty() ? 1 : 2;
    if (type.children.size() == min_pos && has_inline_data)
        x.limits.minimum = static_cast<uint32_t>((data_bytes + 65535) / 65536);
    else
        limits_from(type, min_pos, x.limits, x.shared);
    uint32_t idx = static_cast<uint32_t>(m.memories.size());
    m.memories.push_back(x);
    add_inline_exports(m, n, 2, idx);
    for (std::size_t j = 1; j < n.children.size(); ++j)
        if (is_head(n.children[j], "data")) {
            InlineSegment s;
            s.kind = 2;
            s.index = idx;
            s.owner = owner;
            s.form = n.children[j];
            segments.push_back(s);
        }
}
static void parse_global(const Node &n, Module &m) {
    Global g;
    std::size_t i = 1;
    if (i < n.children.size() && named(n.children[i]))
        g.name = n.children[i++].text;
    for (std::size_t j = i; j < n.children.size(); ++j)
        if (is_head(n.children[j], "import")) {
            if (g.imported)
                throw std::runtime_error("duplicate inline import");
            require_count(n.children[j], 3, 3, "inline import");
            if (has_any_definition(m))
                throw std::runtime_error("imports must precede module definitions");
            g.imported = true;
            g.import_module = string_value(n.children[j].children[1], "import module name");
            g.import_name = string_value(n.children[j].children[2], "import field name");
        }
    while (i < n.children.size() &&
           (is_head(n.children[i], "import") || is_head(n.children[i], "export")))
        ++i;
    if (i >= n.children.size())
        throw std::runtime_error("global missing type");
    if (is_head(n.children[i], "mut")) {
        require_count(n.children[i], 2, 2, "mutable global type");
        g.mutable_ = true;
        g.type = valtype(n.children[i].children[1]);
        ++i;
    } else
        g.type = valtype(n.children[i++]);
    if (!g.imported) {
        if (i >= n.children.size())
            throw std::runtime_error("global missing initializer");
        for (; i < n.children.size(); ++i)
            if (!is_head(n.children[i], "export"))
                g.raw_init.push_back(n.children[i]);
    }
    while (i < n.children.size() &&
           (is_head(n.children[i], "import") || is_head(n.children[i], "export")))
        ++i;
    if (i != n.children.size())
        throw std::runtime_error("invalid global declaration");
    uint32_t idx = static_cast<uint32_t>(m.globals.size());
    m.globals.push_back(g);
    add_inline_exports(m, n, 3, idx);
}
static void parse_export(const Node &n, Module &m) {
    require_count(n, 3, 3, "export");
    Export e;
    e.name = string_value(n.children[1], "export name");
    const Node &d = n.children[2];
    if (!d.list || d.children.size() != 2)
        throw std::runtime_error("invalid export descriptor");
    if (is_head(d, "func")) {
        e.kind = 0;
        e.index = resolve_index(m, d.children[1], "function");
    } else if (is_head(d, "table")) {
        e.kind = 1;
        e.index = resolve_index(m, d.children[1], "table");
    } else if (is_head(d, "memory")) {
        e.kind = 2;
        e.index = resolve_index(m, d.children[1], "memory");
    } else if (is_head(d, "global")) {
        e.kind = 3;
        e.index = resolve_index(m, d.children[1], "global");
    } else
        throw std::runtime_error("unsupported export descriptor");
    m.exports.push_back(e);
}
static void parse_elem(const Node &n, Module &m) {
    ElementSegment e;
    std::size_t i = 1;
    if (i < n.children.size() && !n.children[i].list &&
        (named(n.children[i]) || (!n.children[i].text.empty() &&
                                  ((n.children[i].text[0] >= '0' && n.children[i].text[0] <= '9') ||
                                   n.children[i].text[0] == '+'))))
        e.table_index = resolve_index(m, n.children[i++], "table");
    raw_offset_field(n.children, i, e.raw_offset, "element offset");
    if (i < n.children.size() && !n.children[i].list && n.children[i].text == "func")
        ++i;
    for (; i < n.children.size(); ++i)
        e.functions.push_back(resolve_index(m, n.children[i], "function"));
    m.elements.push_back(e);
}
static void parse_data(const Node &n, Module &m) {
    DataSegment d;
    std::size_t i = 1;
    if (i < n.children.size() && !n.children[i].list &&
        (named(n.children[i]) || (!n.children[i].text.empty() &&
                                  ((n.children[i].text[0] >= '0' && n.children[i].text[0] <= '9') ||
                                   n.children[i].text[0] == '+'))))
        d.memory_index = resolve_index(m, n.children[i++], "memory");
    raw_offset_field(n.children, i, d.raw_offset, "data offset");
    for (; i < n.children.size(); ++i)
        d.bytes += string_value(n.children[i], "data string");
    m.data.push_back(d);
}
static void parse_inline_segment(const InlineSegment &s, Module &m) {
    if (s.kind == 1) {
        ElementSegment e;
        e.table_index = s.index;
        Node off;
        off.list = true;
        off.children.push_back(Node("i32.const"));
        off.children.push_back(Node("0"));
        e.raw_offset.push_back(off);
        std::size_t j = 1;
        if (j < s.form.children.size() && !s.form.children[j].list &&
            s.form.children[j].text == "func")
            ++j;
        for (; j < s.form.children.size(); ++j)
            e.functions.push_back(resolve_index(m, s.form.children[j], "function"));
        m.elements.push_back(e);
    } else {
        DataSegment d;
        d.memory_index = s.index;
        Node off;
        off.list = true;
        off.children.push_back(Node("i32.const"));
        off.children.push_back(Node("0"));
        d.raw_offset.push_back(off);
        for (std::size_t j = 1; j < s.form.children.size(); ++j)
            d.bytes += string_value(s.form.children[j], "data string");
        m.data.push_back(d);
    }
}
Module parse_module(const std::vector<Token> &tokens) {
    std::vector<Node> roots = parse_syntax(tokens);
    if (roots.size() != 1 || !is_head(roots[0], "module"))
        throw std::runtime_error("expected one (module ...) form");
    const Node &root = roots[0];
    std::size_t start = 1;
    if (start < root.children.size() && named(root.children[start]))
        ++start;
    Module m;
    std::vector<InlineSegment> inline_segments;
    std::vector<std::vector<Export> > inline_exports(root.children.size());
    for (std::size_t i = start; i < root.children.size(); ++i)
        if (is_head(root.children[i], "type"))
            parse_type_decl(root.children[i], m);
    prepass_implicit_types(m, root, start);

    for (std::size_t i = start; i < root.children.size(); ++i) {
        const Node &n = root.children[i];
        if (!n.list)
            throw std::runtime_error("unexpected atom in module");
        if (is_head(n, "type") || is_head(n, "export") || is_head(n, "start") ||
            is_head(n, "elem") || is_head(n, "data"))
            continue;
        std::size_t export_begin = m.exports.size();
        if (is_head(n, "import"))
            parse_import(n, m);
        else if (is_head(n, "func"))
            parse_func(n, m);
        else if (is_head(n, "table"))
            parse_table(n, m, inline_segments, i);
        else if (is_head(n, "memory"))
            parse_memory(n, m, inline_segments, i);
        else if (is_head(n, "global"))
            parse_global(n, m);
        else
            throw std::runtime_error("unsupported module field '" + form_head(n) + "'");
        for (std::size_t j = export_begin; j < m.exports.size(); ++j)
            inline_exports[i].push_back(m.exports[j]);
    }
    m.exports.clear();
    for (std::size_t i = start; i < root.children.size(); ++i) {
        const Node &n = root.children[i];
        for (std::size_t j = 0; j < inline_exports[i].size(); ++j)
            m.exports.push_back(inline_exports[i][j]);
        for (std::size_t j = 0; j < inline_segments.size(); ++j)
            if (inline_segments[j].owner == i)
                parse_inline_segment(inline_segments[j], m);
        if (is_head(n, "export"))
            parse_export(n, m);
        else if (is_head(n, "start")) {
            require_count(n, 2, 2, "start");
            if (m.has_start)
                throw std::runtime_error("module has more than one start function");
            m.has_start = true;
            m.start = resolve_index(m, n.children[1], "function");
        } else if (is_head(n, "elem"))
            parse_elem(n, m);
        else if (is_head(n, "data"))
            parse_data(n, m);
    }
    return m;
}

}
