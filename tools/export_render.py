"""Export what the port's renderer needs from the user's own copy of the game.

Usage: python -E -P export_render.py <game_root> <extracted_dir> <out_dir>   (e.g. extracted data\\render)

The ScriptableObject configs (texture swaps, player looks, animations) are read from the JSON that
extract_assets.py wrote to <extracted_dir>/configs; their references are resolved here.

Outputs (Unity coordinates: x right, y up, z forward; textures bottom row first, like Unity UVs):
  tex/<name>.tex        raw RGBA8: "DDTX", uint16 width, uint16 height, pixels
  sprites.json          sprite name -> [texture, x, y, w, h, pivotX, pivotY, pixelsPerUnit, [border l,b,r,t]] (pixels)
  arena_S/M/L.json      background scene: textured meshes (world space) and prop sprites (world matrices)
  trial_swaps.json      per trial: [[original texture, swapped texture], ...] (SwapMaterialByTrial)
  player_look.json      textures composited into player sprites (SpriteMaker)
  animations.json       SimpleAnimationConfig frames
"""
import sys, os, json, struct, math
from pathlib import Path
import UnityPy
from UnityPy.helpers.MeshHelper import MeshHandler

root = Path(sys.argv[1])
extracted = Path(sys.argv[2]).resolve()
out = Path(sys.argv[3]).resolve()
if os.name == "nt":  # config file names push paths past MAX_PATH
    extracted = Path("\\\\?\\" + str(extracted))
    out = Path("\\\\?\\" + str(out))
data_dir = next(p for p in root.iterdir() if p.name.endswith("_Data"))
(out / "tex").mkdir(parents=True, exist_ok=True)

print("Cargando juego...", flush=True)
env = UnityPy.load(str(data_dir))
by_file = {}
objects = {}  # (file, path_id) -> ObjectReader
for o in env.objects:
    by_file.setdefault(o.assets_file.name, []).append(o)
    objects[(o.assets_file.name, o.path_id)] = o
externals = json.loads((extracted / "file_externals.json").read_text(encoding="utf-8"))


def resolve(file_name, ref):
    """Object behind a serialized PPtr ({m_FileID, m_PathID}) found in file_name."""
    if not ref or not ref.get("m_PathID"):
        return None
    fid = ref["m_FileID"]
    target = file_name if fid == 0 else externals[file_name][fid - 1]
    return objects.get((target, ref["m_PathID"]))

# ---------------------------------------------------------------------------------------------
# Textures
# ---------------------------------------------------------------------------------------------
tex_names = {}  # (file, path_id) -> exported name
written = set()
MAX_PIXELS = 1024 * 1024


def tex_key(obj):
    return (obj.assets_file.name, obj.path_id)


def export_texture(obj):
    key = tex_key(obj)
    if key in tex_names:
        return tex_names[key]
    t = obj.read()
    name = t.m_Name or f"tex_{obj.path_id}"
    if name in written:  # same name, different texture: keep both
        i = 1
        while f"{name}__{i}" in written:
            i += 1
        name = f"{name}__{i}"
    tex_names[key] = name
    if not t.m_Width or not t.m_Height or t.m_Width * t.m_Height > MAX_PIXELS:
        return name
    img = t.image.convert("RGBA").transpose(1)  # FLIP_TOP_BOTTOM: bottom row first
    with open(out / "tex" / f"{name}.tex", "wb") as f:
        f.write(b"DDTX" + struct.pack("<HH", img.width, img.height) + img.tobytes())
    written.add(name)
    return name


def tex_from_pptr(p):
    if not p or not p.path_id:
        return None
    return export_texture(p.deref())


for o in env.objects:
    if o.type.name == "Texture2D":
        try:
            export_texture(o)
        except Exception as e:
            print("  textura", o.path_id, e)
print("texturas:", len(written), flush=True)

# ---------------------------------------------------------------------------------------------
# Sprites
# ---------------------------------------------------------------------------------------------
sprites = {}
for o in env.objects:
    if o.type.name != "Sprite":
        continue
    try:
        s = o.read()
        rd = s.m_RD
        tex = tex_from_pptr(rd.texture)
        tr = rd.textureRect
        # m_Pivot is normalised over m_Rect; the packed textureRect may be trimmed (textureRectOffset).
        off = rd.textureRectOffset
        px = s.m_Pivot.x * s.m_Rect.width - off.x
        py = s.m_Pivot.y * s.m_Rect.height - off.y
        name = s.m_Name
        if name in sprites:
            continue
        b = s.m_Border  # 9-slice borders in pixels: left, bottom, right, top
        sprites[name] = [tex, tr.x, tr.y, tr.width, tr.height, round(px, 4), round(py, 4), s.m_PixelsToUnits,
                         [b.x, b.y, b.z, b.w]]
    except Exception as e:
        print("  sprite", o.path_id, e)
(out / "sprites.json").write_text(json.dumps(sprites, separators=(",", ":")), encoding="utf-8")
print("sprites:", len(sprites), flush=True)

# ---------------------------------------------------------------------------------------------
# Scene helpers
# ---------------------------------------------------------------------------------------------
def quat_mat(q):
    x, y, z, w = q.x, q.y, q.z, q.w
    return [
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ]


def local_matrix(tr):
    r = quat_mat(tr.m_LocalRotation)
    s = tr.m_LocalScale
    p = tr.m_LocalPosition
    m = [[r[i][0] * s.x, r[i][1] * s.y, r[i][2] * s.z, [p.x, p.y, p.z][i]] for i in range(3)]
    m.append([0, 0, 0, 1])
    return m


def mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def world_matrix(tr):
    m = local_matrix(tr)
    while tr.m_Father.path_id:
        tr = tr.m_Father.read()
        m = mul(local_matrix(tr), m)
    return m


def active_in_hierarchy(go):
    tr = None
    for c in go.m_Component:
        if c.component.type.name in ("Transform", "RectTransform"):
            tr = c.component.read()
    while True:
        if not go.m_IsActive:
            return False
        if tr is None or not tr.m_Father.path_id:
            return True
        tr = tr.m_Father.read()
        go = tr.m_GameObject.read()


def component(go, type_name):
    for c in go.m_Component:
        if c.component.type.name == type_name:
            return c.component.read()
    return None


def has_script(go, class_name):
    # MonoBehaviours have no type tree here: m_Script is read from the raw base fields
    # (m_GameObject PPtr, m_Enabled + align, then m_Script PPtr at byte 16).
    for c in go.m_Component:
        if c.component.type.name != "MonoBehaviour":
            continue
        mb = c.component.deref()
        fid, pid = struct.unpack_from("<iq", mb.get_raw_data(), 16)
        script = resolve(mb.assets_file.name, {"m_FileID": fid, "m_PathID": pid})
        if script is not None and script.read().m_ClassName == class_name:
            return True
    return False


def material_info(mat):
    tex = None
    color = [1, 1, 1, 1]
    for name, te in mat.m_SavedProperties.m_TexEnvs:
        if name == "_MainTex":
            tex = tex_from_pptr(te.m_Texture)
    for name, c in mat.m_SavedProperties.m_Colors:
        if name == "_Color":
            color = [round(c.r, 4), round(c.g, 4), round(c.b, 4), round(c.a, 4)]
    shader = mat.m_Shader.read().m_ParsedForm.m_Name if mat.m_Shader.path_id else ""
    return tex, color, shader


mesh_cache = {}


def mesh_data(mesh):
    key = (mesh.object_reader.assets_file.name, mesh.object_reader.path_id)
    if key not in mesh_cache:
        h = MeshHandler(mesh)
        h.process()
        verts = h.m_Vertices
        uvs = h.m_UV0 or [(0, 0)] * len(verts)
        idx = list(h.m_IndexBuffer)
        subs = []
        for sm in mesh.m_SubMeshes:
            start = sm.firstByte // (2 if mesh.m_IndexFormat == 0 else 4)
            tris = [idx[start + i] + sm.baseVertex for i in range(sm.indexCount)]
            subs.append(tris)
        mesh_cache[key] = (verts, uvs, subs)
    return mesh_cache[key]


def transform_point(m, v):
    return [m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2] + m[i][3] for i in range(3)]


# ---------------------------------------------------------------------------------------------
# Arenas (BG_Trial1_S/M/L = level9/8/7)
# ---------------------------------------------------------------------------------------------
ARENAS = {"S": "level9", "M": "level8", "L": "level7"}
for size, level in ARENAS.items():
    draws, props = [], []
    for o in by_file.get(level, []):
        t = o.type.name
        if t == "MeshRenderer":
            mr = o.read()
            go = mr.m_GameObject.read()
            if not mr.m_Enabled or not active_in_hierarchy(go):
                continue
            mf = component(go, "MeshFilter")
            if mf is None or not mf.m_Mesh.path_id:
                continue
            verts, uvs, subs = mesh_data(mf.m_Mesh.read())
            sbi = mr.m_StaticBatchInfo
            if sbi.subMeshCount:
                sub_ids = range(sbi.firstSubMesh, sbi.firstSubMesh + sbi.subMeshCount)
                mtx = None  # static batches are already in world space
            else:
                sub_ids = range(len(subs))
                mtx = world_matrix(component(go, "Transform"))
            for n, si in enumerate(sub_ids):
                mat_ptr = mr.m_Materials[min(n, len(mr.m_Materials) - 1)]
                tex, color, shader = material_info(mat_ptr.read())
                tris = subs[si]
                used = sorted(set(tris))
                remap = {v: i for i, v in enumerate(used)}
                pos, uv = [], []
                for v in used:
                    p = list(verts[v][:3])
                    if mtx:
                        p = transform_point(mtx, p)
                    pos += [round(c, 4) for c in p]
                    uv += [round(uvs[v][0], 5), round(uvs[v][1], 5)]
                draws.append({"texture": tex, "color": color, "shader": shader, "pos": pos, "uv": uv,
                              "idx": [remap[i] for i in tris]})
        elif t == "SpriteRenderer":
            sr = o.read()
            go = sr.m_GameObject.read()
            if not sr.m_Enabled or not active_in_hierarchy(go) or not sr.m_Sprite.path_id:
                continue
            tr = component(go, "Transform")
            billboard = has_script(go, "Billboard") or has_script(tr.m_Father.read().m_GameObject.read(), "Billboard") \
                if tr.m_Father.path_id else has_script(go, "Billboard")
            c = sr.m_Color
            props.append({"sprite": sr.m_Sprite.read().m_Name, "matrix": [x for row in world_matrix(tr) for x in row],
                          "color": [round(c.r, 4), round(c.g, 4), round(c.b, 4), round(c.a, 4)],
                          "flipX": bool(sr.m_FlipX), "flipY": bool(sr.m_FlipY), "billboard": bool(billboard),
                          "name": go.m_Name})
    (out / f"arena_{size}.json").write_text(json.dumps({"draws": draws, "props": props}, separators=(",", ":")), encoding="utf-8")
    print(f"arena {size}: {len(draws)} mallas, {len(props)} sprites", flush=True)


# ---------------------------------------------------------------------------------------------
# Configs (ScriptableObjects already in extracted/configs as JSON)
# ---------------------------------------------------------------------------------------------
def load_configs(cls):
    d = extracted / "configs" / cls
    return [json.loads(p.read_text(encoding="utf-8")) for p in sorted(d.glob("*.json"))]


def tex_ref(cfg, ref):
    o = resolve(cfg["_file"], ref)
    return export_texture(o) if o is not None else None


def sprite_ref(cfg, ref):
    o = resolve(cfg["_file"], ref)
    return o.read().m_Name if o is not None else None


# SwapMaterialByTrial.backgroundTextureSwapConfigs[trial] -> BackgroundTextureSwapConfig, in trial order.
swap_by_name = {c["m_Name"]: c for c in load_configs("BackgroundTextureSwapConfig")}
trial_swaps = []
for name in sorted(swap_by_name):
    c = swap_by_name[name]
    pairs = [] if c.get("skipSwaps") else [[tex_ref(c, s["originalTexture"]), tex_ref(c, s["swappedTexture"])]
                                           for s in c.get("textureSwaps", [])]
    trial_swaps.append({"name": name, "swaps": pairs})
(out / "trial_swaps.json").write_text(json.dumps(trial_swaps, indent=1), encoding="utf-8")
print("swaps:", [(t["name"], len(t["swaps"])) for t in trial_swaps], flush=True)

# SpriteMaker layers: body, clothes (by class), face (by expression), hair.
general = load_configs("HumanPlayerGeneralConfig")[0]
look = {
    "bodyTextures": [tex_ref(general, r) for r in general["bodyTextures"]],
    "faces": {k: tex_ref(general, general["face" + k]) for k in ("Plain", "Fear", "Joy", "Tired", "Yell")},
}
for c in load_configs("HumanPlayerConfig"):
    sex = "male" if "Male" in c["m_Name"] else "female"
    look[sex] = {
        "clothes": {k: tex_ref(c, c["clothesTexture" + k]) for k in ("Brawler", "Carrier", "Runner")},
        "hair": [tex_ref(c, h["texture"]) for h in c["hairTextures"]],
    }
(out / "player_look.json").write_text(json.dumps(look, indent=1), encoding="utf-8")

anims = {}
for c in load_configs("SimpleAnimationConfig"):
    anims[c["m_Name"]] = [[sprite_ref(c, f["sprite"]), f["duration"]] for f in c["frames"]]
(out / "animations.json").write_text(json.dumps(anims, indent=1), encoding="utf-8")
print("animaciones:", list(anims), flush=True)
