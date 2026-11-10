#include "app/save.h"

#include <cstdio>
#include <fstream>

#include <nlohmann/json.hpp>

namespace dd::app {

using json = nlohmann::json;
using namespace dd::sim;

namespace {
constexpr int kVersion = 2;
}

bool SaveRecord(const std::string& path, const RunRecord& r) {
    json j;
    j["version"] = kVersion;
    j["seed"] = r.seed;
    j["mode"] = static_cast<int>(r.mode);
    j["team"] = r.team;
    j["date"] = {r.date.year, r.date.month, r.date.day};
    j["restartUsed"] = r.restartUsed;
    json stages = json::array();
    for (const StageRecord& s : r.stages) stages.push_back({{"ball", s.ball}, {"inputs", s.inputs}, {"meta", s.meta}});
    j["stages"] = std::move(stages);
    j["ball"] = r.current.ball; // the ball already chosen for the next stage
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
        if (j.value("version", 0) != kVersion) return false;
        RunRecord rec;
        rec.seed = j["seed"];
        rec.mode = static_cast<GameMode>(j["mode"].get<int>());
        rec.team = j["team"];
        rec.date = {j["date"][0], j["date"][1], j["date"][2]};
        rec.restartUsed = j.value("restartUsed", false);
        for (const json& s : j["stages"]) {
            StageRecord sr;
            sr.ball = s.value("ball", 0);
            sr.inputs = s["inputs"].get<std::vector<int>>();
            sr.meta = s.value("meta", std::vector<int>{});
            rec.stages.push_back(std::move(sr));
        }
        rec.current.ball = j.value("ball", 0);
        *r = std::move(rec);
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool RecordExists(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return static_cast<bool>(f);
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

bool ApplyMeta(Run& run, int code) {
    if (code >= 100) return run.TryRecruit(code - 100);
    PlayerStat s = static_cast<PlayerStat>(code % 10);
    if (s != PlayerStat::Stamina && s != PlayerStat::Strength && s != PlayerStat::Perk) return false;
    return run.TryUpgrade(code / 10, s);
}

bool ReplayStages(Run& run, const RunRecord& r) {
    for (const StageRecord& sr : r.stages) {
        run.ballEffect = static_cast<DifficultyEffect>(sr.ball);
        Game g(run);
        g.Start();
        for (int in : sr.inputs) {
            if (g.phase() == Phase::SuggestUndo) {
                if (in >= 0) return false;
                g.RespondToUndoSuggestion(in == RunRecord::kUndoYes);
                continue;
            }
            std::vector<Command> cmds = g.GetCommands();
            if (in < 0 || in >= static_cast<int>(cmds.size()) || !g.Select(cmds[static_cast<size_t>(in)])) return false;
        }
        if (g.phase() != Phase::StageEnded || g.gameEndedInDefeat) return false;
        for (int code : sr.meta)
            if (!ApplyMeta(run, code)) return false;
        if (!run.AdvanceStage()) return false;
    }
    return true;
}

} // namespace dd::app
