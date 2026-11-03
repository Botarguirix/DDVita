"""Extract Dungeon Deathball assets from the user's own Steam copy.

Usage: python -E -P extract_assets.py <game_root> <out_dir> [--no-audio]

Outputs:
  textures/  Texture2D as PNG
  sprites/   Sprite as PNG (cropped from atlas) + sprites.json (rect/pivot/ppu/texture)
  audio/     AudioClip as WAV
  configs/<Class>/<name>.json   asset MonoBehaviours (ScriptableObjects: *Config etc.)
  scenes/<file>.json            GameObject hierarchy summary per scene/level
  localization/                 string tables from Addressables bundles
"""
import sys, os, json, re, collections
from pathlib import Path
import UnityPy
from UnityPy.helpers.TypeTreeGenerator import TypeTreeGenerator
from UnityPy.helpers.TypeTreeNode import TypeTreeNode

UNITY_VERSION = "2020.3.37f1"

root = Path(sys.argv[1])
out = Path(sys.argv[2]).resolve()
if os.name == "nt":
    out = Path("\\\\?\\" + str(out))  # long-path prefix: config names push paths past MAX_PATH
only_data = "--only-data" in sys.argv
do_audio = "--no-audio" not in sys.argv and not only_data
data_dir = next(p for p in root.iterdir() if p.name.endswith("_Data"))

gen = TypeTreeGenerator(UNITY_VERSION)
gen.load_local_game(str(root))

def safe(name, fallback):
    name = re.sub(r'[<>:"/\\|?*\x00-\x1f]', "_", name or "").strip() or fallback
    return name[:120]

def unique(path: Path):
    if not path.exists():
        return path
    i = 1
    while True:
        p = path.with_name(f"{path.stem}__{i}{path.suffix}")
        if not p.exists():
            return p
        i += 1

def jsonable(v):
    if isinstance(v, (bytes, bytearray)):
        return v.hex() if len(v) <= 64 else f"<{len(v)} bytes>"
    if isinstance(v, dict):
        return {k: jsonable(x) for k, x in v.items()}
    if isinstance(v, (list, tuple)):
        return [jsonable(x) for x in v]
    return v

for sub in ("textures", "sprites", "audio", "configs", "scenes", "localization"):
    (out / sub).mkdir(parents=True, exist_ok=True)

stats = collections.Counter()
errors = collections.Counter()
sprite_meta = {}
script_cache = {}

def script_of(obj, mb):
    ptr = mb.m_Script
    if not ptr or not ptr.path_id:
        return None
    key = (id(obj.assets_file), ptr.file_id, ptr.path_id)
    if key not in script_cache:
        try:
            s = ptr.read()
            script_cache[key] = (s.m_AssemblyName, f"{s.m_Namespace + '.' if s.m_Namespace else ''}{s.m_ClassName}", s.m_ClassName)
        except Exception:
            script_cache[key] = None
    return script_cache[key]

node_cache = {}

def node_without_refs(asm, full, max_fields=None):
    # UnityPy can't resolve this build's [SerializeReference] registry (used by LocalizedString),
    # and it always sits last in the object, so drop it and read every other field.
    # max_fields keeps only the first N top-level fields (fallback for partially unreadable objects).
    key = (asm, full, max_fields)
    if key not in node_cache:
        nodes = gen.get_nodes(asm if asm.endswith(".dll") else asm + ".dll", full)
        keep, skip, fields = [], False, 0
        for i, n in enumerate(nodes):
            if n.m_Level == 1:
                fields += 1
                skip = n.m_Type == "ManagedReferencesRegistry" or (max_fields is not None and fields > max_fields)
            if not skip:
                t = n.m_Type
                # Generator bug: string[] fields come out typed "string" (Array of string, not of char),
                # which UnityPy then reads as a single string and misaligns everything after it.
                if t == "string" and i + 3 < len(nodes) and nodes[i + 1].m_Type == "Array" and nodes[i + 3].m_Type != "char":
                    t = "vector"
                keep.append(TypeTreeNode(n.m_Level, t, n.m_Name, 0, 0, m_MetaFlag=n.m_MetaFlag))
        node_cache[key] = TypeTreeNode.from_list(keep)
    return node_cache[key]

def read_partial(obj, asm, full):
    total = len(node_without_refs(asm, full).m_Children)
    for n in range(total - 1, 3, -1):
        try:
            tt = obj.read_typetree(node_without_refs(asm, full, n), check_read=False)
            tt["_partial"] = f"{n}/{total} campos"
            return tt
        except Exception:
            continue
    return None

pptr_index = {}

# Scene components from the game's own assemblies (singletons such as Teams, PlayerClasses, AppDeathball).
GAME_ASSEMBLIES = {"_Project.Runtime.dll", "_Project.Runtime", "com.mattglanville.eggtools-base.dll",
                   "com.mattglanville.eggtools-base", "com.mattglanville.eggtools-menus.dll", "com.mattglanville.eggtools-menus"}
MAX_SCENE_INSTANCES = 20
scene_components = collections.defaultdict(list)

def script_cache_asm(obj, mb):
    sc = script_of(obj, mb)
    return sc[0] if sc else None

def read_mb_typetree(obj):
    mb = obj.read(check_read=False)
    sc = script_of(obj, mb)
    if not sc:
        return mb, None, None
    asm, full, cls = sc
    try:
        tt = obj.read_typetree(node_without_refs(asm, full), check_read=False)
    except Exception as e:
        tt = read_partial(obj, asm, full)
        errors[f"typetree:{cls}:{type(e).__name__}{'(parcial)' if tt else ''}"] += 1
        if tt is None:
            return mb, cls, None
    tt["_file"] = obj.assets_file.name
    tt["_path_id"] = obj.path_id
    tt["_class"] = cls
    return mb, cls, tt

file_externals = {}

def process_env(env, source_label):
    scene_objs = collections.defaultdict(list)
    for f in env.files.values():
        for af in ([f] if hasattr(f, "externals") else getattr(f, "files", {}).values()):
            if hasattr(af, "externals"):
                file_externals[af.name] = [Path(e.path.replace("\\", "/")).name for e in af.externals]
    for obj in env.objects:
        t = obj.type.name
        if t not in ("MonoBehaviour", "Transform", "RectTransform", "CanvasRenderer"):
            try:
                nm = obj.peek_name()
            except Exception:
                nm = None
            pptr_index[f"{obj.assets_file.name}:{obj.path_id}"] = [t, nm]
        if only_data and t in ("Texture2D", "Sprite", "AudioClip"):
            continue
        try:
            if t == "Texture2D":
                d = obj.read()
                if d.m_Width and d.m_Height:
                    p = unique(out / "textures" / f"{safe(d.m_Name, f'tex_{obj.path_id}')}.png")
                    d.image.save(p)
                    stats["textures"] += 1
            elif t == "Sprite":
                d = obj.read()
                name = safe(d.m_Name, f"sprite_{obj.path_id}")
                p = unique(out / "sprites" / f"{name}.png")
                d.image.save(p)
                r = d.m_Rect
                tex_name = None
                try:
                    tex_name = d.m_RD.texture.read().m_Name
                except Exception:
                    pass
                sprite_meta[p.stem] = {
                    "rect": [r.x, r.y, r.width, r.height],
                    "pivot": [d.m_Pivot.x, d.m_Pivot.y],
                    "ppu": d.m_PixelsToUnits,
                    "texture": tex_name,
                }
                stats["sprites"] += 1
            elif t == "AudioClip" and do_audio:
                d = obj.read()
                for name, data in d.samples.items():
                    p = unique(out / "audio" / f"{safe(name, f'clip_{obj.path_id}')}.wav")
                    p.write_bytes(data)
                    stats["audio"] += 1
            elif t == "MonoBehaviour":
                mb, cls, tt = read_mb_typetree(obj)
                if cls is None:
                    continue
                go = mb.m_GameObject
                if not go or not go.path_id:
                    # Asset-level MonoBehaviour (ScriptableObject): game config data
                    if tt is not None:
                        d = out / "configs" / safe(cls, "Unknown")
                        d.mkdir(exist_ok=True)
                        p = unique(d / f"{safe(tt.get('m_Name'), str(obj.path_id))}.json")
                        p.write_text(json.dumps(jsonable(tt), indent=1, ensure_ascii=False), encoding="utf-8")
                        stats["configs"] += 1
                else:
                    scene_objs[obj.assets_file.name].append(cls)
                    if tt is not None and script_cache_asm(obj, mb) in GAME_ASSEMBLIES:
                        scene_components[cls].append(tt)
        except Exception as e:
            errors[f"{t}:{type(e).__name__}:{str(e)[:60]}"] += 1
    for f, classes in scene_objs.items():
        c = collections.Counter(classes)
        p = out / "scenes" / f"{safe(source_label + '_' + f, 'scene')}.json"
        p.write_text(json.dumps(c.most_common(), indent=1), encoding="utf-8")

print("Cargando datos principales...", flush=True)
process_env(UnityPy.load(str(data_dir)), "main")

# Addressables bundles (localization string tables)
aa = data_dir / "StreamingAssets" / "aa"
for b in aa.rglob("*.bundle"):
    env = UnityPy.load(str(b))
    for obj in env.objects:
        if obj.type.name != "MonoBehaviour":
            continue
        try:
            try:
                mb, cls, tt = read_mb_typetree(obj)
            except ValueError:
                # Bundle header carries an unresolvable ref registry; these are string tables.
                tt = obj.read_typetree(node_without_refs("Unity.Localization", "UnityEngine.Localization.Tables.StringTable"), check_read=False)
            if tt is None:
                continue
            p = unique(out / "localization" / f"{safe(tt.get('m_Name'), cls)}.json")
            p.write_text(json.dumps(jsonable(tt), indent=1, ensure_ascii=False), encoding="utf-8")
            stats["localization"] += 1
        except Exception as e:
            errors[f"loc:{type(e).__name__}"] += 1

sc_dir = out / "scene_components"
sc_dir.mkdir(exist_ok=True)
for cls, items in scene_components.items():
    if len(items) <= MAX_SCENE_INSTANCES:
        (sc_dir / f"{safe(cls, 'Unknown')}.json").write_text(
            json.dumps(jsonable(items), indent=1, ensure_ascii=False), encoding="utf-8")
        stats["scene_components"] += 1
(out / "pptr_index.json").write_text(json.dumps(pptr_index, ensure_ascii=False), encoding="utf-8")
(out / "file_externals.json").write_text(json.dumps(file_externals, indent=1), encoding="utf-8")
if not only_data:
    (out / "sprites" / "sprites.json").write_text(json.dumps(sprite_meta, indent=1), encoding="utf-8")
print("== Extraído:", dict(stats))
print("== Errores:", dict(errors.most_common(25)))
