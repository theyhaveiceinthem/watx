#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <watx/text/literals.h>

namespace watx {
namespace {
int digit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
std::string digits(const std::string &text, std::size_t &pos, unsigned base, bool required) {
    std::string out;
    bool previous = false;
    while (pos < text.size()) {
        int d = digit(text[pos]);
        if (d >= 0 && static_cast<unsigned>(d) < base) {
            out += text[pos++];
            previous = true;
        } else if (text[pos] == '_') {
            if (!previous || pos + 1 == text.size() || digit(text[pos + 1]) < 0 ||
                static_cast<unsigned>(digit(text[pos + 1])) >= base)
                throw std::runtime_error("misplaced underscore in numeric literal '" + text + "'");
            ++pos;
            previous = false;
        } else
            break;
    }
    if (required && out.empty())
        throw std::runtime_error("missing digits in numeric literal '" + text + "'");
    return out;
}
uint64_t magnitude(const std::string &text, std::size_t pos, uint64_t maximum) {
    unsigned base = 10;
    if (pos + 1 < text.size() && text[pos] == '0' && text[pos + 1] == 'x') {
        base = 16;
        pos += 2;
    }
    std::string clean = digits(text, pos, base, true);
    if (pos != text.size())
        throw std::runtime_error("invalid integer literal '" + text + "'");
    uint64_t result = 0;
    for (std::size_t i = 0; i < clean.size(); ++i) {
        unsigned d = static_cast<unsigned>(digit(clean[i]));
        if (result > maximum / base || (result == maximum / base && d > maximum % base))
            throw std::runtime_error("integer literal out of range: '" + text + "'");
        result = result * base + d;
    }
    return result;
}
std::vector<unsigned char> little_endian(uint64_t bits, unsigned width) {
    std::vector<unsigned char> bytes;
    for (unsigned i = 0; i < width / 8; ++i)
        bytes.push_back(static_cast<unsigned char>((bits >> (i * 8)) & 255));
    return bytes;
}
}
uint32_t parse_natural(const std::string &text) {
    if (text.empty() || text[0] == '+' || text[0] == '-')
        throw std::runtime_error("expected unsigned index: '" + text + "'");
    return static_cast<uint32_t>(magnitude(text, 0, static_cast<uint64_t>(0xffffffffUL)));
}
uint64_t parse_integer_bits(const std::string &text, unsigned width) {
    if (width != 32 && width != 64)
        throw std::runtime_error("invalid integer width");
    if (text.empty())
        throw std::runtime_error("missing integer literal");
    bool minus = text[0] == '-';
    bool plus = text[0] == '+';
    uint64_t high = static_cast<uint64_t>(1) << (width - 1);
    uint64_t maximum =
        width == 64 ? ~static_cast<uint64_t>(0) : static_cast<uint64_t>(0xffffffffUL);
    uint64_t limit = minus ? high : (plus ? high - 1 : maximum);
    uint64_t value = magnitude(text, minus || plus ? 1 : 0, limit);
    return (minus ? ~value + 1 : value) & maximum;
}
std::vector<unsigned char> parse_float_bits(const std::string &text, unsigned width) {
    if (width != 32 && width != 64)
        throw std::runtime_error("invalid float width");
    if (text.empty())
        throw std::runtime_error("missing floating-point literal");
    std::size_t pos = (text[0] == '+' || text[0] == '-') ? 1 : 0;
    bool negative = text[0] == '-';
    std::string body = text.substr(pos);
    uint64_t exponent =
        width == 32 ? static_cast<uint64_t>(0x7f800000UL) : static_cast<uint64_t>(0x7ff) << 52;
    unsigned fraction = width == 32 ? 23 : 52;
    uint64_t sign = negative ? static_cast<uint64_t>(1) << (width - 1) : 0;
    if (body == "inf")
        return little_endian(sign | exponent, width);
    if (body == "nan")
        return little_endian(sign | exponent | (static_cast<uint64_t>(1) << (fraction - 1)), width);
    if (body.substr(0, 6) == "nan:0x") {
        uint64_t payload = magnitude(body.substr(4), 0, (static_cast<uint64_t>(1) << fraction) - 1);
        if (!payload)
            throw std::runtime_error("NaN payload cannot be zero");
        return little_endian(sign | exponent | payload, width);
    }
    std::string clean = negative ? "-" : "";
    unsigned base = 10;
    if (pos + 1 < text.size() && text[pos] == '0' && text[pos + 1] == 'x') {
        base = 16;
        pos += 2;
        clean += "0x";
    }
    clean += digits(text, pos, base, true);
    if (pos < text.size() && text[pos] == '.') {
        clean += text[pos++];
        clean += digits(text, pos, base, false);
    }
    if (pos < text.size() && ((base == 10 && (text[pos] == 'e' || text[pos] == 'E')) ||
                              (base == 16 && (text[pos] == 'p' || text[pos] == 'P')))) {
        clean += text[pos++];
        if (pos < text.size() && (text[pos] == '+' || text[pos] == '-'))
            clean += text[pos++];
        clean += digits(text, pos, 10, true);
    }
    if (pos != text.size())
        throw std::runtime_error("invalid floating-point literal '" + text + "'");
    char *end = 0;
    if (width == 32) {
        if (sizeof(float) != 4 || !std::numeric_limits<float>::is_iec559)
            throw std::runtime_error("IEEE-754 float32 host required");
        float value = ::strtof(clean.c_str(), &end);
        if (!end || *end || value > std::numeric_limits<float>::max() ||
            value < -std::numeric_limits<float>::max())
            throw std::runtime_error("floating-point literal out of range: '" + text + "'");
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        return little_endian(bits, width);
    }
    if (sizeof(double) != 8 || !std::numeric_limits<double>::is_iec559)
        throw std::runtime_error("IEEE-754 float64 host required");
    double value = std::strtod(clean.c_str(), &end);
    if (!end || *end || value > std::numeric_limits<double>::max() ||
        value < -std::numeric_limits<double>::max())
        throw std::runtime_error("floating-point literal out of range: '" + text + "'");
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return little_endian(bits, width);
}
}
