"""Debug: print the serialized fields of every MonoBehaviour of the given classes in one file.

Usage: python -E -P dump_mb.py <game_root> <file, e.g. level4> <Class> [Class...]
"""
import sys, json
from pathlib import Path
import UnityPy
from UnityPy.helpers.TypeTreeGenerator import TypeTreeGenerator

root = Path(sys.argv[1])
data_dir = next(p for p in root.iterdir() if p.name.endswith("_Data"))
classes = set(sys.argv[3:])
gen = TypeTreeGenerator("2020.3.37f1")
gen.load_local_game(str(root))
env = UnityPy.load(str(data_dir / sys.argv[2]))


def plain(v):
    if isinstance(v, dict):
        return {k: plain(x) for k, x in v.items()}
    if isinstance(v, list):
        return [plain(x) for x in v]
    if isinstance(v, (bytes, bytearray)):
        return f"<{len(v)} bytes>"
    return v


for o in env.objects:
    if o.type.name != "MonoBehaviour":
        continue
    try:
        mb = o.read(check_read=False)
        s = mb.m_Script.read()
    except Exception:
        continue
    if s.m_ClassName not in classes:
        continue
    full = f"{s.m_Namespace + '.' if s.m_Namespace else ''}{s.m_ClassName}"
    asm = s.m_AssemblyName if s.m_AssemblyName.endswith(".dll") else s.m_AssemblyName + ".dll"
    try:
        tt = o.read_typetree(gen.get_nodes_up(asm, full), check_read=False)
    except Exception as e:
        print(s.m_ClassName, o.path_id, "ERROR", e)
        continue
    go = mb.m_GameObject.read().m_Name if mb.m_GameObject.path_id else None
    print(f"== {s.m_ClassName} [{go}] #{o.path_id}")
    print(json.dumps(plain(tt), indent=1, default=str)[:4000])
