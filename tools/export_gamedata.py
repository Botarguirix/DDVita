"""Build the port's gamedata.json from extract_assets.py output.

Usage: python -E -P export_gamedata.py <extracted_dir> <out_json>

Every exported object gets a stable id "<file>:<path_id>". PPtr fields become:
  {"$ref": id}            -> another exported object (config / scene component)
  {"$asset": "Type:Name"} -> a non-script asset (texture, sprite, audio...)
  null                    -> null pointer
"""
import sys, os, json
from pathlib import Path

src = Path(sys.argv[1]).resolve()
dst = Path(sys.argv[2]).resolve()
if os.name == "nt":
    src = Path("\\\\?\\" + str(src))

# Asset config classes that are editor/runtime plumbing, not game data.
SKIP_CONFIG_CLASSES = {
    "ActivationPlayableAsset", "ActivationTrack", "AnimationPlayableAsset", "AnimationTrack", "AudioPlayableAsset",
    "AudioTrack", "CinemachineBlenderSettings", "CinemachineShot", "CinemachineTrack", "ControllerDataFiles",
    "CustomCalculation_Accelerometer", "CustomCalculation_CompareElementValues", "CustomCalculation_FirstNonZero",
    "CustomCalculation_LogitechGRacingWheelPedals", "ES3GlobalReferences", "GUISkin", "GroupTrack",
    "HardwareJoystickMap", "HardwareJoystickTemplateMap", "MarkerTrack", "NoiseSettings", "SignalAsset",
    "SignalEmitter", "SignalTrack", "TimelineAsset", "EditorConfig", "StringTable", "SharedTableData", "Locale",
    "LocalizationSettings",
}
# Scene singletons whose serialized fields feed game logic.
SCENE_SINGLETONS = ["Teams", "PlayerClasses", "AppDeathball", "Configs", "PerkManager", "SessionManager",
                    "LeagueManager", "GameController", "FieldManager", "FieldGenerator", "EnemyManager",
                    "PlayerManager", "HazardManager", "PlayerStatUpgrader", "GlobalStatTracker", "Jukebox",
                    # Prefab components (one prefab each): actor flags and per-prefab configs used by gameplay.
                    "Player", "Ball", "Enemy", "BreakableWall", "FloorSpikes", "LightningHazard", "Treasure"]

externals = json.loads((src / "file_externals.json").read_text(encoding="utf-8"))
pptr_index = json.loads((src / "pptr_index.json").read_text(encoding="utf-8"))

raw = {}          # id -> raw typetree dict
singletons = {}   # class -> id

for cls_dir in sorted((src / "configs").iterdir()):
    if cls_dir.name in SKIP_CONFIG_CLASSES:
        continue
    for f in sorted(cls_dir.glob("*.json")):
        d = json.loads(f.read_text(encoding="utf-8"))
        raw[f"{d['_file']}:{d['_path_id']}"] = d

for name in SCENE_SINGLETONS:
    p = src / "scene_components" / f"{name}.json"
    if not p.exists():
        continue
    items = json.loads(p.read_text(encoding="utf-8"))
    for d in items:
        raw[f"{d['_file']}:{d['_path_id']}"] = d
    singletons[name] = f"{items[0]['_file']}:{items[0]['_path_id']}"

def resolve(ptr, file):
    fid, pid = ptr["m_FileID"], ptr["m_PathID"]
    if pid == 0:
        return None
    target_file = file if fid == 0 else externals.get(file, [])[fid - 1]
    key = f"{target_file}:{pid}"
    if key in raw:
        return {"$ref": key}
    hit = pptr_index.get(key)
    if hit:
        return {"$asset": f"{hit[0]}:{hit[1]}"}
    return {"$missing": key}

def convert(v, file):
    if isinstance(v, dict):
        if set(v.keys()) == {"m_FileID", "m_PathID"}:
            return resolve(v, file)
        return {k: convert(x, file) for k, x in v.items()}
    if isinstance(v, list):
        return [convert(x, file) for x in v]
    return v

DROP = {"m_GameObject", "m_Enabled", "m_Script", "_file", "_path_id", "_partial"}
objects = {}
missing = 0
for key, d in raw.items():
    o = {"_class": d["_class"], "_name": d.get("m_Name", "")}
    for k, v in d.items():
        if k in DROP or k == "m_Name" or k == "_class":
            continue
        o[k] = convert(v, d["_file"])
    objects[key] = o

# SessionConfig/BuildConfig are loaded by name in the original (Resources), expose them as singletons too.
for key, o in objects.items():
    if o["_class"] in ("SessionConfig", "BuildConfig", "AppConfig_DD", "TimingConfig", "ScoringConfig", "LevelUpConfig"):
        singletons.setdefault(o["_class"], key)

txt = json.dumps({"singletons": singletons, "objects": objects}, ensure_ascii=False, separators=(",", ":"))
missing = txt.count('"$missing"')
dst.parent.mkdir(parents=True, exist_ok=True)
dst.write_text(txt, encoding="utf-8")
print(f"{len(objects)} objetos, {len(singletons)} singletons, {missing} referencias sin resolver, {len(txt)/1024:.0f} KB -> {dst.name}")
print("singletons:", ", ".join(sorted(singletons)))
