// Mono's String.GetHashCode (non-randomized, two-lane djb2/xor), used for the daily challenge seed.
#pragma once
#include <cstdint>
#include <string>

namespace dd {

// Hashes UTF-16 code units exactly like Mono. Stops at the first NUL, as Mono's loop does.
int32_t MonoStringHash(const std::u16string& s);

// Convenience for ASCII/Latin-1 strings (dates, platform names).
int32_t MonoStringHash(const std::string& ascii);

} // namespace dd
