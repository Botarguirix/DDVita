// Prints a generated session in a canonical text format, for diffing against the C# oracle.
//
//   sessiondump <gamedata.json> <seed> [mode] [team] [YYYY-MM-DD]
//   sessiondump <gamedata.json> --daily YYYY-MM-DD       (daily challenge of that date)
//   sessiondump --rng <seed> <count> <min> <max>          (raw MonoRandom.Next(min,max) values)
//   sessiondump --hash <text>                             (Mono String.GetHashCode)
//
// mode: 0 Normal, 1 DailyChallenge, 2 Boss, 3 Action.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/calendar.h"
#include "core/gamedata.h"
#include "core/mono_random.h"
#include "core/mono_string.h"
#include "core/session_generator.h"

using namespace dd;

static bool ParseDate(const char* s, Date* d) {
    return std::sscanf(s, "%d-%d-%d", &d->year, &d->month, &d->day) == 3;
}

static void Dump(const Session& s, const SessionRequest& req) {
    std::printf("seed=%d mode=%s team=%s season=%s treasure=%s\n", s.seed, ToString(req.mode), req.team->name.c_str(),
                ToString(s.seasonalEvent), s.treasureConfig ? s.treasureConfig->name.c_str() : "null");
    for (const auto& p : s.players) {
        std::printf("P%02d %s voice=%d name=%s class=%s perk=%s body=%s hair=%s\n", p.playerNumber,
                    p.isMale ? "M" : "F", p.voice, p.name.c_str(), p.playerClassConfig->name.c_str(),
                    p.perkChain ? p.perkChain->name.c_str() : "null", p.bodyTexture.c_str(), p.hairTexture.c_str());
    }
    for (size_t t = 0; t < s.trials.size(); t++) {
        for (size_t i = 0; i < s.trials[t].stages.size(); i++) {
            const Stage& st = s.trials[t].stages[i];
            std::printf("T%zuS%zu size=%s grid=%dx%d par=%d budget=%d\n", t, i, ToString(st.fieldSize), st.gridX, st.gridY,
                        st.par, st.enemyBudget);
            for (int x = 0; x < st.gridX; x++) {
                for (int y = 0; y < st.gridY; y++) {
                    const TileData& td = st.At(x, y);
                    if (td.enemyConfig) std::printf("  %d,%d E %s\n", x, y, td.enemyConfig->name.c_str());
                    if (td.obstacleConfig) std::printf("  %d,%d W %s\n", x, y, td.obstacleConfig->name.c_str());
                    if (td.hazardConfig) std::printf("  %d,%d H %s\n", x, y, td.hazardConfig->name.c_str());
                    if (td.treasureConfig) std::printf("  %d,%d T %s\n", x, y, td.treasureConfig->name.c_str());
                }
            }
        }
    }
}

int main(int argc, char** argv) {
    if (argc >= 6 && std::string(argv[1]) == "--rng") {
        MonoRandom rng(std::atoi(argv[2]));
        int count = std::atoi(argv[3]), lo = std::atoi(argv[4]), hi = std::atoi(argv[5]);
        for (int i = 0; i < count; i++) std::printf("%d\n", rng.Next(lo, hi));
        return 0;
    }
    if (argc >= 3 && std::string(argv[1]) == "--hash") {
        std::printf("%d\n", MonoStringHash(std::string(argv[2])));
        return 0;
    }
    if (argc < 3) {
        std::fprintf(stderr, "usage: sessiondump <gamedata.json> <seed> [mode] [team] [YYYY-MM-DD]\n"
                             "       sessiondump <gamedata.json> --daily YYYY-MM-DD\n");
        return 2;
    }
    GameData data;
    std::string err;
    if (!data.Load(argv[1], &err)) {
        std::fprintf(stderr, "load failed: %s\n", err.c_str());
        return 1;
    }

    SessionRequest req;
    req.buildVersion = data.buildVersion;
    req.date = Date::TodayUtc();
    int team = 0;
    if (std::string(argv[2]) == "--daily") {
        if (argc < 4 || !ParseDate(argv[3], &req.date)) {
            std::fprintf(stderr, "bad date\n");
            return 2;
        }
        req.mode = GameMode::DailyChallenge;
        req.seed = DailyChallengeSeed(data.platform, req.date);
    } else {
        req.seed = std::atoi(argv[2]);
        if (argc > 3) req.mode = static_cast<GameMode>(std::atoi(argv[3]));
        if (argc > 4) team = std::atoi(argv[4]);
        if (argc > 5 && !ParseDate(argv[5], &req.date)) {
            std::fprintf(stderr, "bad date\n");
            return 2;
        }
    }
    if (team < 0 || team >= static_cast<int>(data.teams.size())) {
        std::fprintf(stderr, "team out of range (0..%zu)\n", data.teams.size() - 1);
        return 2;
    }
    req.team = TeamForMode(data, req.mode, team);
    Dump(GenerateSession(data, req), req);
    return 0;
}
