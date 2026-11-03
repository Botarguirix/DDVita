"""Debug: list every MonoBehaviour field (any file) that references a SoundCue or an AudioClip.

Usage: python -E -P find_audio_refs.py <game_root> [out.txt]
"""
import sys
from pathlib import Path
import UnityPy
from UnityPy.helpers.TypeTreeGenerator import TypeTreeGenerator

root = Path(sys.argv[1])
data_dir = next(p for p in root.iterdir() if p.name.endswith("_Data"))
gen = TypeTreeGenerator("2020.3.37f1")
gen.load_local_game(str(root))
env = UnityPy.load(str(data_dir))
objects = {(o.assets_file.name, o.path_id): o for o in env.objects}
externals = {}
for f in env.files.values():
    try:
        externals[f.name] = [Path(e.path).name for e in f.externals]
    except AttributeError:
        pass

script_cache = {}


def script_of(o):
    try:
        mb = o.read(check_read=False)
        s = mb.m_Script.read()
        return mb, s
    except Exception:
        return None, None


def target(file_name, ref):
    if not isinstance(ref, dict) or "m_PathID" not in ref or not ref["m_PathID"]:
        return None
    fid = ref["m_FileID"]
    if fid == 0:
        tf = file_name
    else:
        ex = externals.get(file_name, [])
        if fid - 1 >= len(ex):
            return None
        tf = ex[fid - 1]
    return objects.get((tf, ref["m_PathID"]))


def describe(o):
    if o.type.name == "AudioClip":
        return "clip:" + o.read().m_Name
    if o.type.name == "MonoBehaviour":
        mb, s = script_of(o)
        if s is not None and s.m_ClassName in ("SoundCue", "MusicPackConfig"):
            return s.m_ClassName + ":" + mb.m_Name
    return None


lines = []


def walk(file_name, v, path, out):
    if isinstance(v, dict):
        if "m_PathID" in v and "m_FileID" in v:
            t = target(file_name, v)
            d = describe(t) if t else None
            if d:
                out.append((path, d))
            return
        for k, x in v.items():
            walk(file_name, x, f"{path}.{k}" if path else k, out)
    elif isinstance(v, list):
        for i, x in enumerate(v):
            walk(file_name, x, f"{path}[{i}]", out)


for o in env.objects:
    if o.type.name != "MonoBehaviour":
        continue
    mb, s = script_of(o)
    if s is None or s.m_ClassName in ("SoundCue", "MusicPackConfig"):
        continue
    full = f"{s.m_Namespace + '.' if s.m_Namespace else ''}{s.m_ClassName}"
    asm = s.m_AssemblyName if s.m_AssemblyName.endswith(".dll") else s.m_AssemblyName + ".dll"
    try:
        tt = o.read_typetree(gen.get_nodes_up(asm, full), check_read=False)
    except Exception:
        continue
    out = []
    walk(o.assets_file.name, tt, "", out)
    if not out:
        continue
    go = None
    try:
        go = mb.m_GameObject.read().m_Name if mb.m_GameObject.path_id else mb.m_Name
    except Exception:
        pass
    for p, d in out:
        lines.append(f"{o.assets_file.name}\t{s.m_ClassName}\t{go}\t{p}\t{d}")

text = "\n".join(lines)
if len(sys.argv) > 2:
    Path(sys.argv[2]).write_text(text, encoding="utf-8")
else:
    print(text)
print(len(lines), "refs", file=sys.stderr)
