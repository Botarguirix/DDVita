"""Debug: print the GameObject hierarchy of one serialized file with transforms and renderers.

Usage: python -E -P dump_hierarchy.py <game_root> <file name, e.g. level4> [out.txt]
"""
import sys
from pathlib import Path
import UnityPy

root = Path(sys.argv[1])
data_dir = next(p for p in root.iterdir() if p.name.endswith("_Data"))
env = UnityPy.load(str(data_dir / sys.argv[2]))
out = open(sys.argv[3], "w", encoding="utf-8") if len(sys.argv) > 3 else sys.stdout

objs = {}
for o in env.objects:
    objs[o.path_id] = o

def name_of_pptr(p):
    try:
        if p.path_id == 0:
            return None
        t = p.read()
        return getattr(t, "m_Name", None) or getattr(t, "name", None) or type(t).__name__
    except Exception as e:
        return f"<{p.file_id}:{p.path_id}>"

def describe(go):
    parts = []
    for c in go.m_Component:
        ptr = c.component if hasattr(c, "component") else c[1]
        try:
            comp = ptr.read()
        except Exception:
            parts.append("?")
            continue
        tn = type(comp).__name__
        if tn == "SpriteRenderer":
            col = comp.m_Color
            parts.append(f"Sprite[{name_of_pptr(comp.m_Sprite)} col=({col.r:.2f},{col.g:.2f},{col.b:.2f},{col.a:.2f}) flip={comp.m_FlipX} order={comp.m_SortingOrder}]")
        elif tn == "MeshFilter":
            parts.append(f"Mesh[{name_of_pptr(comp.m_Mesh)}]")
        elif tn == "MeshRenderer":
            mats = [name_of_pptr(m) for m in comp.m_Materials]
            parts.append(f"MR{mats}")
        elif tn == "Camera":
            parts.append(f"Camera[fov={comp.field_of_view} ortho={comp.orthographic} size={comp.orthographic_size}]")
        elif tn == "MonoBehaviour":
            try:
                s = comp.m_Script.read()
                parts.append(s.m_ClassName)
            except Exception:
                parts.append("MB")
        elif tn not in ("Transform", "RectTransform"):
            parts.append(tn)
    return " ".join(parts)

def walk(tr, depth):
    go = tr.m_GameObject.read()
    p = tr.m_LocalPosition
    r = tr.m_LocalRotation
    s = tr.m_LocalScale
    out.write(f"{'  '*depth}{go.m_Name}{'' if go.m_IsActive else ' (off)'} p=({p.x:.2f},{p.y:.2f},{p.z:.2f}) r=({r.x:.3f},{r.y:.3f},{r.z:.3f},{r.w:.3f}) s=({s.x:.2f},{s.y:.2f},{s.z:.2f}) {describe(go)}\n")
    for c in tr.m_Children:
        walk(c.read(), depth + 1)

roots = []
for o in env.objects:
    if o.type.name in ("Transform", "RectTransform"):
        t = o.read()
        if t.m_Father.path_id == 0:
            roots.append(t)
for t in roots:
    walk(t, 0)
