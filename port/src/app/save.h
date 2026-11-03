// Run save: the session request plus every input of the completed stages. The simulation is
// deterministic, so loading regenerates the session and replays those stages headless, which puts
// the run back at the start of the current stage (as the original restores SessionStageBackup).
#pragma once
#include <string>
#include <vector>

#include "core/calendar.h"
#include "core/session_generator.h"
#include "sim/sim.h"

namespace dd::app {

struct RunRecord {
    // inputs: >= 0 index into Game::GetCommands(), kUndoYes / kUndoNo answer SuggestUndo
    static constexpr int kUndoYes = -1, kUndoNo = -2;
    int seed = 0;
    GameMode mode = GameMode::Normal;
    int team = 0;
    Date date;
    std::vector<std::vector<int>> stages; // completed stages
    std::vector<int> current;             // the stage being played (not saved)
};

bool SaveRecord(const std::string& path, const RunRecord& r);
bool LoadRecord(const std::string& path, RunRecord* r);
void DeleteRecord(const std::string& path);
SessionRequest RequestFor(const GameData& data, const RunRecord& r);
// Plays the completed stages on a fresh Run; false if the inputs no longer fit (other data version).
bool ReplayStages(sim::Run& run, const RunRecord& r);

} // namespace dd::app
