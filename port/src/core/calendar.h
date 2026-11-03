// Date helpers, seasonal event detection (SeasonalEvents) and daily challenge seeds (SessionSeed).
#pragma once
#include <cstdint>
#include <string>

#include "gamedata.h"

namespace dd {

struct Date {
    int year = 1970, month = 1, day = 1;
    static bool IsLeapYear(int year);
    int DayOfYear() const;
    static Date TodayUtc();
};

// Helper.GetDateStandardised: "Y/M/D" without zero padding.
std::string DateStandardised(const Date& d);

// BuildConfig.Platform enum name, as used by Platform.ToString().
const char* PlatformName(int platform);

// SessionSeed.GetDailyChallengeSeed
int32_t DailyChallengeSeed(int platform, const Date& date);

// SeasonalEvents.GetSeasonalEvent (keeps the original's quirks: the Christmas range can never match).
SeasonalEvent DetectSeasonalEvent(const Date& date);

} // namespace dd
