// Bit-exact port of System.Random from the game's Mono mscorlib (Knuth subtractive generator).
// Sessions, enemy AI and daily challenges all depend on this producing identical sequences.
#pragma once
#include <cstdint>

namespace dd {

class MonoRandom {
public:
    explicit MonoRandom(int32_t seed);

    int32_t Next();
    int32_t Next(int32_t maxValue);
    int32_t Next(int32_t minValue, int32_t maxValue);
    double NextDouble();

private:
    double Sample();
    int32_t InternalSample();
    double GetSampleForLargeRange();

    int32_t seedArray_[56];
    int inext_;
    int inextp_;
};

// SessionSeed.SeededRandom
int32_t SeededRandom(MonoRandom& rng, int32_t min, int32_t max, bool inclusiveMax = false);

} // namespace dd
