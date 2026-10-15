#include <watx/text/utf8.h>
namespace watx {
bool valid_utf8(const std::string &text) {
    for (std::size_t i = 0; i < text.size();) {
        unsigned char c = static_cast<unsigned char>(text[i++]);
        if (c < 0x80)
            continue;
        unsigned int count = 0, value = 0, minimum = 0;
        if (c >= 0xc2 && c <= 0xdf) {
            count = 1;
            value = c & 31;
            minimum = 0x80;
        } else if (c >= 0xe0 && c <= 0xef) {
            count = 2;
            value = c & 15;
            minimum = 0x800;
        } else if (c >= 0xf0 && c <= 0xf4) {
            count = 3;
            value = c & 7;
            minimum = 0x10000;
        } else
            return false;
        if (i + count > text.size())
            return false;
        while (count--) {
            unsigned char next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80)
                return false;
            value = (value << 6) | (next & 63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            return false;
    }
    return true;
}
}
