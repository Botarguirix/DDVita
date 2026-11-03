// Port of SessionGenerator (plus the RNG-consuming parts of PerkManager, PlayerClasses,
// HumanPlayerGenerator and PlayerSpriteGenerator). Must stay bit-exact with the original:
// the same seed has to yield the same recruits and dungeons.
#pragma once
#include "gamedata.h"
#include "session.h"

namespace dd {

struct SessionRequest {
    int seed = 0;
    GameMode mode = GameMode::Normal;
    const TeamConfig* team = nullptr;  // Teams.GetCurrentTeamConfigForCurrentMode()
    BuildVersion buildVersion;         // BuildConfig.buildVersion (or the saved run's version)
    Date date;                         // AppDeathball.GetDateTime() (Steam server time, UTC)
};

Session GenerateSession(const GameData& data, const SessionRequest& request);

// Teams.GetCurrentTeamConfigForCurrentMode: the daily challenge always uses team 0.
const TeamConfig* TeamForMode(const GameData& data, GameMode mode, int currentTeam);

} // namespace dd
