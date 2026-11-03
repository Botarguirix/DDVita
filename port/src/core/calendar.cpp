#include "calendar.h"

#include <cstdio>
#include <ctime>

#include "mono_string.h"

namespace dd {

bool Date::IsLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int Date::DayOfYear() const {
    static const int cumulative[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int doy = cumulative[month - 1] + day;
    if (month > 2 && IsLeapYear(year)) doy++;
    return doy;
}

Date Date::TodayUtc() {
    std::time_t t = std::time(nullptr);
    std::tm* g = std::gmtime(&t);
    return Date{g->tm_year + 1900, g->tm_mon + 1, g->tm_mday};
}

std::string DateStandardised(const Date& d) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d/%d/%d", d.year, d.month, d.day);
    return buf;
}

const char* PlatformName(int platform) {
    static const char* names[] = {"PCStandalone", "PCSteam", "PCKartridge", "mobileStandalone", "mobileStore",
                                  "webPlayer", "arcade", "nintendoSwitch", "PS4", "xboxOne"};
    if (platform < 0 || platform >= 10) return "PCSteam";
    return names[platform];
}

int32_t DailyChallengeSeed(int platform, const Date& date) {
    return MonoStringHash(DateStandardised(date) + PlatformName(platform));
}

namespace {

constexpr int kHalloweenStart = 298, kHalloweenEnd = 305;
constexpr int kChristmasStart = 356, kChristmasEnd = 5;
constexpr int kEasterDuration = 7;
constexpr int kMaximumEasterYear = 2100;

bool IsWithinDateRange(int dayOfYear, bool leapYear, int periodStart, int periodEnd) {
    int leap = leapYear ? 1 : 0;
    if (dayOfYear < periodStart + leap) return false;
    if (dayOfYear > periodEnd + leap) return false;
    return true;
}

// SeasonalEvents.GetEasterPeriodStartDate table (2022..2099), {month, day}.
const unsigned char kEaster[78][2] = {
    {4,15},{4,7},{3,29},{4,18},{4,3},{3,26},{4,14},{3,30},{4,19},{4,11},{3,26},{4,15},{4,7},{3,23},{4,11},{4,3},
    {4,23},{4,8},{3,30},{4,19},{4,4},{3,27},{4,15},{4,7},{3,23},{4,12},{4,3},{4,16},{4,8},{3,31},{4,19},{4,4},
    {3,27},{4,16},{3,31},{4,20},{4,12},{3,28},{4,16},{4,8},{3,24},{4,13},{4,4},{3,27},{4,9},{4,1},{4,20},{4,12},
    {3,28},{4,17},{4,8},{3,24},{4,13},{4,5},{4,17},{4,9},{4,1},{4,21},{4,5},{3,28},{4,17},{4,2},{3,24},{4,13},
    {3,29},{4,18},{4,9},{4,1},{4,14},{4,6},{3,28},{4,10},{4,2},{4,22},{4,13},{3,29},{4,18},{4,10}};

} // namespace

SeasonalEvent DetectSeasonalEvent(const Date& date) {
    int dayOfYear = date.DayOfYear();
    bool leap = Date::IsLeapYear(date.year);
    if (IsWithinDateRange(dayOfYear, leap, kHalloweenStart, kHalloweenEnd)) return SeasonalEvent::Halloween;
    if (IsWithinDateRange(dayOfYear, leap, kChristmasStart, kChristmasEnd)) return SeasonalEvent::Christmas;
    if (date.year < kMaximumEasterYear) {
        if (date.year < 2022) {
            // The original builds an invalid DateTime here (and throws); treat as no event.
            std::fprintf(stderr, "The year %d is not valid for Easter seasonal event.\n", date.year);
            return SeasonalEvent::None;
        }
        const unsigned char* e = kEaster[date.year - 2022];
        int start = Date{date.year, e[0], e[1]}.DayOfYear();
        if (IsWithinDateRange(dayOfYear, false, start, start + kEasterDuration)) return SeasonalEvent::Easter;
    }
    return SeasonalEvent::None;
}

} // namespace dd
