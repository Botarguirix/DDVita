"""Builds the game data from the user's own copy of the game (only the steps whose output is
missing or out of date) and packs it for the Vita.

Usage: python -E -P prepare_data.py <game_root> <project_root> [--vpk base.vpk] [--dist dir]

  extracted/            extract_assets.py (everything, audio included)
  data/gamedata.json    export_gamedata.py
  data/render/          export_render.py + make_font.py (extract_fonts.py: the menu fonts) and the
                        port's HUD font (port/vita/font)
  data/loc/             export_loc.py
  data/help.json        export_help.py (How to Play pages, tile info)
  data/achievements.json export_achievements.py (the achievement list, in the original's order)
  data/audio/           export_audio.py
  data/version.json     the data format the game checks, and the version of each step

Each step has a version (STEPS): bump it when its exporter changes, and the step runs again on the
next install. The game refuses data older than its DATA_FORMAT (app/game_app.cpp kDataFormat).

Without the game (and with its output missing) it warns and skips the game data: the build still
works, and only the VPK without data is copied to dist.

With --dist it also writes, when out of date:
  dist/ddvita_data.zip      ddvita/ data folder, to extract into ux0:data/
and with --vpk as well:
  dist/ddvita.vpk           the VPK without game data (a copy of base.vpk, unless it is that file)
  dist/ddvita_full.vpk      the same VPK with data/ inside (app0:data): one file to install

Everything with game data in it comes from the user's copy: for their own console only.
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

# the game refuses data with an older format (keep in step with kDataFormat in app/game_app.cpp)
DATA_FORMAT = 1
# render v2: the arenas' Billboard props (spectators)
STEPS = {"extract": 1, "gamedata": 1, "render": 2, "fonts": 1, "loc": 1, "help": 1, "achievements": 1, "audio": 2}

args = [a for a in sys.argv[1:] if not a.startswith("--")]
opts = {sys.argv[i]: sys.argv[i + 1] for i in range(1, len(sys.argv) - 1) if sys.argv[i].startswith("--")}
game = Path(args[0])
root = Path(args[1]).resolve()
tools = root / "tools"
extracted = root / "extracted"
data = root / "data"
version_json = data / "version.json"


def run(script, *script_args):
    print(f"prepare_data: {script} {' '.join(str(a) for a in script_args)}", flush=True)
    subprocess.run([sys.executable, "-E", "-P", str(tools / script), *map(str, script_args)], check=True, cwd=root)


def read_versions():
    try:
        return json.loads(version_json.read_text(encoding="utf-8")).get("steps", {})
    except (OSError, ValueError):
        return None


def prepare():
    """Runs the missing steps. False (with a warning) when one needs the game and it is not there."""
    have_game = game.is_dir()
    audio_json = data / "audio" / "audio.json"
    done = read_versions()
    if done is None:  # data made before version.json: version 1, audio 2 once it has the seasonal jingles
        done = {s: 1 for s in STEPS}
        if audio_json.is_file() and '"jingle.titleEaster"' in audio_json.read_text(encoding="utf-8"):
            done["audio"] = 2

    def stale(step, *outputs):
        return done.get(step, 0) < STEPS[step] or not all(p.exists() for p in outputs)

    def needs_game(what):
        if have_game:
            return True
        print(f"prepare_data: WARNING: {what} is missing and the game is not at {game} (set DDVITA_GAME_DIR); "
              "skipping the game data, only the VPK without data is made", flush=True)
        return False

    if stale("extract", *(extracted / d for d in ("configs", "audio", "scene_components"))):
        if not needs_game("extracted/"):
            return False
        run("extract_assets.py", game, extracted)
    if stale("gamedata", data / "gamedata.json"):
        run("export_gamedata.py", extracted, data / "gamedata.json")
    if stale("render", data / "render" / "animations.json", data / "render" / "font.tex"):
        if not needs_game("data/render"):
            return False
        run("export_render.py", game, extracted, data / "render")
        # the HUD font is the port's own (port/vita/font, also in the VPK): the same on PC and Vita
        for f in ("font.tex", "font.json"):
            shutil.copy2(root / "port" / "vita" / "font" / f, data / "render" / f)
    # the game's pixel fonts for the menus
    ui_fonts = {"font_s": "EffortsPro", "font_m": "SinsGold", "font_l": "CompassGold"}
    if stale("fonts", *(data / "render" / f"{n}.tex" for n in ui_fonts)):
        if not (extracted / "fonts" / "fonts.json").is_file():
            if not needs_game("extracted/fonts"):
                return False
            run("extract_fonts.py", game, extracted / "fonts")
        for name, ttf in ui_fonts.items():
            run("make_font.py", data / "render", extracted / "fonts" / f"{ttf}.ttf", 16, name)
    if stale("loc", data / "loc" / "en.json"):
        run("export_loc.py", extracted, data / "loc")
    if stale("help", data / "help.json"):
        run("export_help.py", extracted, data / "help.json")
    if stale("achievements", data / "achievements.json"):
        run("export_achievements.py", extracted, data / "achievements.json")
    # audio v2: the seasonal title jingles
    if stale("audio", audio_json):
        if not needs_game("data/audio"):
            return False
        run("export_audio.py", game, extracted, data / "audio")
    version = {"format": DATA_FORMAT, "steps": STEPS}
    if read_versions() != STEPS:
        version_json.write_text(json.dumps(version, indent=1) + "\n", encoding="utf-8")
    return True


have_data = prepare()
if "--dist" not in opts:
    sys.exit(0)

dist = Path(opts["--dist"])
dist.mkdir(parents=True, exist_ok=True)
base = Path(opts["--vpk"]) if "--vpk" in opts else None
files = sorted(p for p in data.rglob("*") if p.is_file()) if have_data else []

# Fingerprints instead of dates: the SDK rewrites the base VPK on every build, and a deleted data
# file changes no date. Each output records the fingerprint of what it was made from.
data_sig = hashlib.sha256("\n".join(f"{p.relative_to(data).as_posix()} {p.stat().st_size} {p.stat().st_mtime_ns}"
                                    for p in files).encode()).hexdigest()


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


if base:
    with zipfile.ZipFile(base) as z:  # contents only: vita-pack-vpk stamps the zip with the build time
        vpk_sig = hashlib.sha256("\n".join(f"{i.filename} {i.CRC} {i.file_size}" for i in z.infolist()).encode()).hexdigest()
    plain = dist / "ddvita.vpk"
    if plain.resolve() != base.resolve() and stale(plain, vpk_sig):
        shutil.copy2(base, plain)
        done(plain, vpk_sig)
        print(f"prepare_data: {plain}")

if not have_data:
    sys.exit(0)

if base:
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
