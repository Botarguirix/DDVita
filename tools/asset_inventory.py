"""Inventory Unity assets of Dungeon Deathball: counts/sizes per type, textures, meshes, audio, shaders, MonoBehaviour scripts."""
import sys, json, collections
from pathlib import Path
import UnityPy

data_dir = Path(sys.argv[1])
out = Path(sys.argv[2])

env = UnityPy.load(str(data_dir))
by_type = collections.Counter()
size_by_type = collections.Counter()
textures, meshes, audio, shaders = [], [], [], []
mb_scripts = collections.Counter()

for obj in env.objects:
    t = obj.type.name
    by_type[t] += 1
    size_by_type[t] += obj.byte_size
    try:
        if t == "Texture2D":
            d = obj.read()
            textures.append((d.m_Name, d.m_Width, d.m_Height, str(d.m_TextureFormat)))
        elif t == "Mesh":
            d = obj.read()
            meshes.append((d.m_Name, getattr(d, "m_VertexCount", None) or len(getattr(d, "m_Vertices", []) or []) // 3))
        elif t == "AudioClip":
            d = obj.read()
            audio.append((d.m_Name, round(d.m_Length, 1), d.m_Frequency, d.m_Channels, str(getattr(d, "m_CompressionFormat", ""))))
        elif t == "Shader":
            d = obj.read()
            name = getattr(getattr(d, "m_ParsedForm", None), "m_Name", None) or d.m_Name
            shaders.append(name)
        elif t == "MonoBehaviour":
            d = obj.read(check_read=False)
            s = d.m_Script.read() if d.m_Script else None
            if s:
                mb_scripts[s.m_ClassName] += 1
    except Exception as e:
        pass

report = {
    "types": {k: {"count": v, "bytes": size_by_type[k]} for k, v in by_type.most_common()},
    "textures": sorted(textures, key=lambda x: -x[1] * x[2]),
    "meshes": sorted(meshes, key=lambda x: -(x[1] or 0)),
    "audio": sorted(audio, key=lambda x: -x[1]),
    "shaders": sorted(set(shaders)),
    "monobehaviours": mb_scripts.most_common(),
}
out.write_text(json.dumps(report, indent=1), encoding="utf-8")

print("== Tipos (top 25)")
for k, v in by_type.most_common(25):
    print(f"  {k:28} {v:6}  {size_by_type[k]/1024/1024:7.2f} MB")
print(f"== Texturas: {len(textures)}  mayores:", textures and sorted(textures, key=lambda x: -x[1]*x[2])[:8])
fmts = collections.Counter(t[3] for t in textures)
print("   formatos:", dict(fmts))
print(f"== Mallas: {len(meshes)}  mayores:", report["meshes"][:8])
print(f"== Audio: {len(audio)} clips, {sum(a[1] for a in audio)/60:.1f} min total; mayores:", report["audio"][:6])
print("   formatos:", dict(collections.Counter(a[4] for a in audio)))
print(f"== Shaders ({len(set(shaders))}):", sorted(set(shaders)))
print(f"== MonoBehaviours (top 30):", mb_scripts.most_common(30))
