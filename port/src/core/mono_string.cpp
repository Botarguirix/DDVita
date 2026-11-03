#include "mono_string.h"

namespace dd {

int32_t MonoStringHash(const std::u16string& s) {
    // Unsigned arithmetic gives the same two's-complement wraparound as C# without UB.
    uint32_t h1 = 5381;
    uint32_t h2 = h1;
    const char16_t* p = s.c_str();
    uint32_t c;
    while ((c = p[0]) != 0) {
        h1 = ((h1 << 5) + h1) ^ c;
        c = p[1];
        if (c == 0) break;
        h2 = ((h2 << 5) + h2) ^ c;
        p += 2;
    }
    return static_cast<int32_t>(h1 + h2 * 1566083941u);
}

int32_t MonoStringHash(const std::string& ascii) {
    std::u16string u;
    u.reserve(ascii.size());
    for (unsigned char ch : ascii) u.push_back(static_cast<char16_t>(ch));
    return MonoStringHash(u);
}

} // namespace dd
