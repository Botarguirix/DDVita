"""Builds the game data from the user's own copy of the game (only the steps whose output is
missing) and packs it for the Vita.

Usage: python -E -P prepare_data.py <game_root> <project_root> [--vpk base.vpk] [--dist dir]

  extracted/            extract_assets.py (everything, audio included)
  data/gamedata.json    export_gamedata.py
  data/render/          export_render.py + make_font.py
  data/audio/           export_audio.py

With --vpk and --dist it also writes, when out of date:
  dist/ddvita.vpk           the VPK without game data (just a copy of base.vpk)
  dist/ddvita_full.vpk      the same VPK with data/ inside (app0:data): one file to install
  dist/ddvita_data.zip      ddvita/ data folder, to extract into ux0:data/ instead

Everything with game data in it comes from the user's copy: for their own console only.
"""
import hashlib
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

args = [a for a in sys.argv[1:] if not a.startswith("--")]
opts = {sys.argv[i]: sys.argv[i + 1] for i in range(1, len(sys.argv) - 1) if sys.argv[i].startswith("--")}
game = Path(args[0])
root = Path(args[1]).resolve()
tools = root / "tools"
extracted = root / "extracted"
data = root / "data"


def run(script, *script_args):
    print(f"prepare_data: {script} {' '.join(str(a) for a in script_args)}", flush=True)
    subprocess.run([sys.executable, "-E", "-P", str(tools / script), *map(str, script_args)], check=True, cwd=root)


def need_game():
    if not game.is_dir():
        sys.exit(f"prepare_data: game not found at {game} (set DDVITA_GAME_DIR)")


if not (extracted / "configs").is_dir() or not (extracted / "audio").is_dir() or not (extracted / "scene_components").is_dir():
    need_game()
    run("extract_assets.py", game, extracted)
if not (data / "gamedata.json").is_file():
    run("export_gamedata.py", extracted, data / "gamedata.json")
if not (data / "render" / "animations.json").is_file():
    need_game()
    run("export_render.py", game, extracted, data / "render")
if not (data / "render" / "font.tex").is_file():
    run("make_font.py", data / "render")
if not (data / "audio" / "audio.json").is_file():
    need_game()
    run("export_audio.py", game, extracted, data / "audio")

if "--vpk" not in opts or "--dist" not in opts:
    sys.exit(0)

base = Path(opts["--vpk"])
dist = Path(opts["--dist"])
dist.mkdir(parents=True, exist_ok=True)
files = sorted(p for p in data.rglob("*") if p.is_file())

# Fingerprints instead of dates: the SDK rewrites the base VPK on every build, and a deleted data
# file changes no date. Each output records the fingerprint of what it was made from.
data_sig = hashlib.sha256("\n".join(f"{p.relative_to(data).as_posix()} {p.stat().st_size} {p.stat().st_mtime_ns}"
                                    for p in files).encode()).hexdigest()
with zipfile.ZipFile(base) as z:  # contents only: vita-pack-vpk stamps the zip with the build time
    vpk_sig = hashlib.sha256("\n".join(f"{i.filename} {i.CRC} {i.file_size}" for i in z.infolist()).encode()).hexdigest()


def stale(out, sig):
    stamp = out.with_name(out.name + ".sig")
    return not out.exists() or not stamp.exists() or stamp.read_text() != sig


def done(out, sig):
    out.with_name(out.name + ".sig").write_text(sig)


def add_data(z, prefix):
    for p in files:
        # IMA ADPCM and DXT-like textures barely compress: store them, deflate the JSON
        method = zipfile.ZIP_DEFLATED if p.suffix == ".json" else zipfile.ZIP_STORED
        z.write(p, f"{prefix}/{p.relative_to(data).as_posix()}", compress_type=method)


if stale(dist / "ddvita.vpk", vpk_sig):
    shutil.copy2(base, dist / "ddvita.vpk")
    done(dist / "ddvita.vpk", vpk_sig)
    print(f"prepare_data: {dist / 'ddvita.vpk'}")

full = dist / "ddvita_full.vpk"
if stale(full, vpk_sig + data_sig):
    tmp = full.with_suffix(".tmp")
    with zipfile.ZipFile(base) as src, zipfile.ZipFile(tmp, "w") as z:
        for info in src.infolist():
            z.writestr(info, src.read(info.filename))
        add_data(z, "data")
    os.replace(tmp, full)
    done(full, vpk_sig + data_sig)
    print(f"prepare_data: {full} ({full.stat().st_size / 2**20:.1f} MB)")

pack = dist / "ddvita_data.zip"
if stale(pack, data_sig):
    tmp = pack.with_suffix(".tmp")
    with zipfile.ZipFile(tmp, "w") as z:
        add_data(z, "ddvita")
    os.replace(tmp, pack)
    done(pack, data_sig)
    print(f"prepare_data: {pack} ({pack.stat().st_size / 2**20:.1f} MB)")
