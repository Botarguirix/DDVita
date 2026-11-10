// Run save: the session request plus, for every completed stage, its ball, its inputs and the team
// purchases made after it. The simulation is deterministic, so loading regenerates the session and
// replays those stages headless, which puts the run back at the start of the current stage (as the
// original restores SessionStageBackup).
#pragma once
#include <string>
#include <vector>

#include "core/calendar.h"
#include "core/session_generator.h"
#include "sim/sim.h"

namespace dd::app {

struct StageRecord {
    int ball = 0;            // DifficultyEffect chosen on the stage start panel
    std::vector<int> inputs; // >= 0 index into Game::GetCommands(), kUndoYes / kUndoNo answer SuggestUndo
    std::vector<int> meta;   // team management after the stage: RunRecord::Upgrade / Recruit codes
};

struct RunRecord {
    static constexpr int kUndoYes = -1, kUndoNo = -2;
    // purchase codes: player * 10 + PlayerStat, or 100 + reserve index
    static int Upgrade(int activeIndex, sim::PlayerStat s) { return activeIndex * 10 + static_cast<int>(s); }
    static int Recruit(int reserveIndex) { return 100 + reserveIndex; }
    int seed = 0;
    GameMode mode = GameMode::Normal;
    int team = 0;
    Date date;
    bool restartUsed = false;        // GameFlow.usedRestartOnCurrentStage (saved: survives Save & Quit)
    std::vector<StageRecord> stages; // completed stages
    StageRecord current;             // the stage being played (not saved)
};

bool SaveRecord(const std::string& path, const RunRecord& r);
bool LoadRecord(const std::string& path, RunRecord* r);
bool RecordExists(const std::string& path);
void DeleteRecord(const std::string& path);
SessionRequest RequestFor(const GameData& data, const RunRecord& r);
// One purchase (Upgrade / Recruit code); false if it is not allowed now.
bool ApplyMeta(sim::Run& run, int code);
// Plays the completed stages on a fresh Run; false if the inputs no longer fit (other data version).
bool ReplayStages(sim::Run& run, const RunRecord& r);

} // namespace dd::app
