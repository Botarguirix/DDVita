#include "app/save.h"

#include <cstdio>
#include <fstream>

#include <nlohmann/json.hpp>

namespace dd::app {

using json = nlohmann::json;
using namespace dd::sim;

bool SaveRecord(const std::string& path, const RunRecord& r) {
    json j;
    j["version"] = 1;
    j["seed"] = r.seed;
    j["mode"] = static_cast<int>(r.mode);
    j["team"] = r.team;
    j["date"] = {r.date.year, r.date.month, r.date.day};
    j["stages"] = r.stages;
    // write then rename, so a crash or power loss never leaves half a file
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << j.dump();
        if (!f) return false;
    }
    std::remove(path.c_str());
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

bool LoadRecord(const std::string& path, RunRecord* r) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    try {
        json j;
        f >> j;
        if (j.value("version", 0) != 1) return false;
        r->seed = j["seed"];
        r->mode = static_cast<GameMode>(j["mode"].get<int>());
        r->team = j["team"];
        r->date = {j["date"][0], j["date"][1], j["date"][2]};
        r->stages = j["stages"].get<std::vector<std::vector<int>>>();
        r->current.clear();
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

void DeleteRecord(const std::string& path) { std::remove(path.c_str()); }

SessionRequest RequestFor(const GameData& data, const RunRecord& r) {
    SessionRequest req;
    req.buildVersion = data.buildVersion;
    req.date = r.date;
    req.seed = r.seed;
    req.mode = r.mode;
    req.team = TeamForMode(data, r.mode, r.team);
    return req;
}

bool ReplayStages(Run& run, const RunRecord& r) {
    for (const std::vector<int>& inputs : r.stages) {
        Game g(run);
        g.Start();
        for (int in : inputs) {
            if (g.phase() == Phase::SuggestUndo) {
                if (in >= 0) return false;
                g.RespondToUndoSuggestion(in == RunRecord::kUndoYes);
                continue;
            }
            std::vector<Command> cmds = g.GetCommands();
            if (in < 0 || in >= static_cast<int>(cmds.size()) || !g.Select(cmds[static_cast<size_t>(in)])) return false;
        }
        if (g.phase() != Phase::StageEnded || g.gameEndedInDefeat || !run.AdvanceStage()) return false;
    }
    return true;
}

} // namespace dd::app
