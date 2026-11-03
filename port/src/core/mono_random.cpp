#include "mono_random.h"

#include <climits>
#include <cstdio>
#include <cstdlib>

namespace dd {

namespace {
constexpr int32_t MBIG = INT32_MAX;
constexpr int32_t MSEED = 161803398;
} // namespace

MonoRandom::MonoRandom(int32_t seed) {
    int32_t subtraction = (seed == INT32_MIN) ? INT32_MAX : std::abs(seed);
    int32_t mj = MSEED - subtraction;
    seedArray_[55] = mj;
    int32_t mk = 1;
    for (int i = 1; i < 55; i++) {
        int ii = (21 * i) % 55;
        seedArray_[ii] = mk;
        mk = mj - mk;
        if (mk < 0) mk += MBIG;
        mj = seedArray_[ii];
    }
    for (int k = 1; k < 5; k++) {
        for (int i = 1; i < 56; i++) {
            seedArray_[i] -= seedArray_[1 + (i + 30) % 55];
            if (seedArray_[i] < 0) seedArray_[i] += MBIG;
        }
    }
    inext_ = 0;
    inextp_ = 21;
}

double MonoRandom::Sample() {
    // C# literal 4.656612875245797E-10 == 1.0 / int.MaxValue (correctly rounded double).
    return static_cast<double>(InternalSample()) * (1.0 / static_cast<double>(MBIG));
}

int32_t MonoRandom::InternalSample() {
    int locINext = inext_;
    int locINextp = inextp_;
    if (++locINext >= 56) locINext = 1;
    if (++locINextp >= 56) locINextp = 1;
    int32_t retVal = seedArray_[locINext] - seedArray_[locINextp];
    if (retVal == MBIG) retVal--;
    if (retVal < 0) retVal += MBIG;
    seedArray_[locINext] = retVal;
    inext_ = locINext;
    inextp_ = locINextp;
    return retVal;
}

double MonoRandom::GetSampleForLargeRange() {
    int32_t result = InternalSample();
    if (InternalSample() % 2 == 0) result = -result;
    return (static_cast<double>(result) + 2147483646.0) / 4294967293.0;
}

int32_t MonoRandom::Next() { return InternalSample(); }

int32_t MonoRandom::Next(int32_t maxValue) {
    return static_cast<int32_t>(Sample() * static_cast<double>(maxValue));
}

int32_t MonoRandom::Next(int32_t minValue, int32_t maxValue) {
    int64_t range = static_cast<int64_t>(maxValue) - static_cast<int64_t>(minValue);
    if (range <= INT32_MAX) {
        return static_cast<int32_t>(Sample() * static_cast<double>(range)) + minValue;
    }
    return static_cast<int32_t>(static_cast<int64_t>(GetSampleForLargeRange() * static_cast<double>(range)) + minValue);
}

double MonoRandom::NextDouble() { return Sample(); }

int32_t SeededRandom(MonoRandom& rng, int32_t min, int32_t max, bool inclusiveMax) {
    if (min > max) {
        std::fprintf(stderr, "SeededRandom: Min is greater than Max.\n");
        return rng.Next(min, min);
    }
    if (inclusiveMax) return rng.Next(min, max + 1);
    return rng.Next(min, max);
}

} // namespace dd
