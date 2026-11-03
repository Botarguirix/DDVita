"""Debug: inspect the mesh renderers, materials and meshes of one arena scene."""
import sys
from pathlib import Path
import UnityPy
from UnityPy.helpers.MeshHelper import MeshHandler

root = Path(sys.argv[1])
data_dir = next(p for p in root.iterdir() if p.name.endswith("_Data"))
env = UnityPy.load(str(data_dir / sys.argv[2]))
seen = set()
for o in env.objects:
    if o.type.name != "MeshRenderer":
        continue
    mr = o.read()
    sbi = getattr(mr, "m_StaticBatchInfo", None)
    mats = []
    for m in mr.m_Materials:
        mat = m.read()
        tex = None
        for name, te in mat.m_SavedProperties.m_TexEnvs:
            if name == "_MainTex" and te.m_Texture.path_id:
                t = te.m_Texture.read()
                tex = f"{t.m_Name} {t.m_Width}x{t.m_Height} fmt={t.m_TextureFormat}"
        cols = {n: (round(c.r, 2), round(c.g, 2), round(c.b, 2), round(c.a, 2)) for n, c in mat.m_SavedProperties.m_Colors if n == "_Color"}
        shader = mat.m_Shader.read().m_ParsedForm.m_Name if mat.m_Shader.path_id else None
        mats.append((mat.m_Name, shader, tex, cols))
    go = mr.m_GameObject.read()
    mf = [c.component.read() for c in go.m_Component if c.component.type.name == "MeshFilter"]
    mesh = mf[0].m_Mesh.read() if mf else None
    print(go.m_Name, "sbi", (sbi.firstSubMesh, sbi.subMeshCount) if sbi else None, "mesh", mesh.m_Name if mesh else None, mats)
    if mesh is not None and mesh.path_id not in seen if hasattr(mesh, "path_id") else True:
        pass
    if mesh is not None and mesh.m_Name not in seen:
        seen.add(mesh.m_Name)
        h = MeshHandler(mesh)
        h.process()
        print("   verts", len(h.m_Vertices), "uv0", len(h.m_UV0 or []), "normals", len(h.m_Normals or []), "colors", len(h.m_Colors or []),
              "submeshes", [[a for a in dir(s) if not a.startswith("_")] for s in mesh.m_SubMeshes][:1], len(mesh.m_SubMeshes))
        print("   indices attr", [a for a in dir(h) if 'ndex' in a])
