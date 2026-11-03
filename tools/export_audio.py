"""Export the audio the port uses from the user's own copy of the game.

Usage: python -E -P export_audio.py <game_root> <extracted_dir> <out_dir>   (e.g. extracted data\\audio)

Reads the SoundCue / MusicPackConfig / EnemyConfig / AttackConfig JSON that extract_assets.py wrote,
the Resources container (player voices are loaded by path) and the WAVs in <extracted_dir>/audio.

Outputs:
  audio.json      cues, music, voices and the clips bound to scene components (see NAMED below)
  clips/<n>.dda   "DDAU", uint32 rate, uint16 channels, uint16 0, uint32 frames, uint32 blocks, then
                  blocks of up to 4096 frames; per block and channel: int16 predictor, uint8 step
                  index, uint8 0, then the IMA ADPCM nibbles (low nibble first, padded to a byte)
"""
import sys, os, json, struct, wave
from pathlib import Path
import UnityPy

try:
    import warnings
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        import audioop  # C IMA ADPCM encoder (removed in Python 3.13; a slow fallback is below)
except ImportError:
    audioop = None

root = Path(sys.argv[1])
extracted = Path(sys.argv[2]).resolve()
out = Path(sys.argv[3]).resolve()
if os.name == "nt":  # config file names push paths past MAX_PATH
    extracted = Path("\\\\?\\" + str(extracted))
    out = Path("\\\\?\\" + str(out))
data_dir = next(p for p in root.iterdir() if p.name.endswith("_Data"))
(out / "clips").mkdir(parents=True, exist_ok=True)

print("Cargando juego...", flush=True)
env = UnityPy.load(str(data_dir))
objects = {(o.assets_file.name, o.path_id): o for o in env.objects}
externals = json.loads((extracted / "file_externals.json").read_text(encoding="utf-8"))


def resolve(file_name, ref):
    if not ref or not ref.get("m_PathID"):
        return None
    fid = ref["m_FileID"]
    target = file_name if fid == 0 else externals[file_name][fid - 1]
    return objects.get((target, ref["m_PathID"]))


def load_configs(cls):
    d = extracted / "configs" / cls
    return [json.loads(p.read_text(encoding="utf-8")) for p in sorted(d.glob("*.json"))] if d.exists() else []


used_clips = set()


def clip_name(file_name, ref):
    o = resolve(file_name, ref)
    if o is None or o.type.name != "AudioClip":
        return None
    n = o.read().m_Name
    used_clips.add(n)
    return n


# --- SoundCues ---------------------------------------------------------------------------------
cues = {}       # cue key -> definition
cue_key = {}    # (file, path_id) -> cue key
for c in load_configs("SoundCue"):
    cue_key[(c["_file"], c["_path_id"])] = c["m_Name"]
voice_cues = {}  # (file, path_id) -> "VO/<female|male>/<voice>/<Event>"
for o in env.objects:
    if o.type.name != "ResourceManager":
        continue
    for path, ref in o.read_typetree()["m_Container"]:
        parts = path.lower().split("/")
        if parts[:2] != ["audio", "playervoices"]:
            continue
        t = resolve(o.assets_file.name, ref)
        if t is None:
            continue
        event = {"attack": "Attack", "death": "Death", "lowstamina": "LowStamina", "move": "Move",
                 "movewithball": "MoveWithBall", "rest": "Rest"}[parts[4].replace("playervo_", "")]
        voice_cues[(t.assets_file.name, t.path_id)] = f"VO/{parts[2]}/{parts[3]}/{event}"
for c in load_configs("SoundCue"):
    k = (c["_file"], c["_path_id"])
    key = voice_cues.get(k, c["m_Name"])
    cue_key[k] = key
    clips = [clip_name(c["_file"], r) for r in c.get("audioClips", [])]
    cues[key] = {"pitch": c["pitchBase"], "pitchVar": c["pitchVariance"], "vol": c["volumeBase"],
                 "volVar": c["volumeVariance"], "clips": [x for x in clips if x]}


def cue_ref(file_name, ref):
    o = resolve(file_name, ref)
    return cue_key.get((o.assets_file.name, o.path_id)) if o is not None else None


enemy_death = {}
for c in load_configs("EnemyConfig"):
    k = cue_ref(c["_file"], c.get("soundOnDeath"))
    if k:
        enemy_death[c["m_Name"]] = k
attack_sound = {}
for c in load_configs("AttackConfig"):
    k = cue_ref(c["_file"], c.get("soundOnAttack"))
    if k:
        attack_sound[c["m_Name"]] = k

# --- Music -------------------------------------------------------------------------------------
packs = {}
for c in load_configs("MusicPackConfig"):
    packs[c["m_Name"]] = [clip_name(c["_file"], r) for r in c["tracks"]]

# Clips and cues bound to scene/prefab components (tools/find_audio_refs.py, analysis/audio_refs.txt)
NAMED = {
    "music.boss": "Aggressive9_Boss",           # Jukebox.bossMusic
    "music.outOfTime": "Stage9_OutOfTime",      # Jukebox.outOfTimeMusic
    "music.triumphant": "Omake-luke-loop",      # Jukebox.triumphantMusic
    "music.title": "DrumOnly2",                 # TitleScreen.titleMusic
    "music.endingVictory": "Cave3",             # EndingScreen.endingMusicVictory
    "jingle.title": "Event 16",                 # TitleScreen.introJingle
    "jingle.playerDeath": "Event 11",           # PlayerManager.playerDeathJingle
    "jingle.enemiesAppear": "Event 24",         # StageStartSequences.enemiesAppearJingle
    "jingle.enemiesAppearBoss": "Event 21",     # StageStartSequences.enemiesAppearJingleBossStage
    "jingle.introducePlayer": "Event 20",       # StageStartSequences.introducePlayerJingle
    "jingle.endingVictory": "Fanfale1",         # EndingScreen.endingJingleVictory
    "jingle.halloweenTreat": "jingles_NES12",   # Treasure.jingleOnHalloweenTreat
    "jingle.halloweenTrick": "jingles_NES00",   # Treasure.jingleOnHalloweenTrick
    "sfx.playerSwitch": "Coin 10",              # PlayerManager.playerSwitchSound
    "sfx.button": "Button 1",                   # AudioManager.defaultButtonClick
}
used_clips.update(NAMED.values())

# --- Encode ------------------------------------------------------------------------------------
STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80,
        88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
        598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749,
        3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635,
        13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]
INDEX = [-1, -1, -1, -1, 2, 4, 6, 8]


def encode_py(samples, pred, idx):
    """IMA ADPCM, the same algorithm as audioop.lin2adpcm (slow fallback)."""
    codes = []
    for s in samples:
        step = STEP[idx]
        diff = s - pred
        code = 0
        if diff < 0:
            code = 8
            diff = -diff
        vp = step >> 3
        if diff >= step:
            code |= 4; diff -= step; vp += step
        step >>= 1
        if diff >= step:
            code |= 2; diff -= step; vp += step
        step >>= 1
        if diff >= step:
            code |= 1; vp += step
        pred = pred - vp if code & 8 else pred + vp
        pred = max(-32768, min(32767, pred))
        idx = max(0, min(88, idx + INDEX[code & 7]))
        codes.append(code)
    return codes, pred, idx


BLOCK = 4096


def encode_channel_block(pcm, pred, idx):
    """pcm: mono int16 bytes. Returns (packed nibbles low-first, pred, idx)."""
    if audioop is not None:
        data, (pred2, idx2) = audioop.lin2adpcm(pcm, 2, (pred, idx))
        # audioop packs the first sample in the high nibble: swap to low-first
        packed = bytes(((b >> 4) | ((b & 15) << 4)) for b in data)
        if (len(pcm) // 2) % 2:  # odd count: audioop leaves the last nibble out
            pass
        return packed, pred2, idx2
    samples = struct.unpack(f"<{len(pcm) // 2}h", pcm)
    codes, pred2, idx2 = encode_py(samples, pred, idx)
    if len(codes) % 2:
        codes.append(0)
    return bytes(codes[i] | (codes[i + 1] << 4) for i in range(0, len(codes), 2)), pred2, idx2


def safe(name):
    return "".join(ch if ch.isalnum() or ch in "-_." else "_" for ch in name)


clip_info = {}
missing = []
for n in sorted(used_clips):
    src = extracted / "audio" / f"{n}.wav.wav"
    if not src.exists():
        missing.append(n)
        continue
    dst = out / "clips" / f"{safe(n)}.dda"
    with wave.open(str(src)) as w:
        rate, ch, width, frames = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
        raw = w.readframes(frames)
    if width != 2:
        raw = audioop.lin2lin(raw, width, 2) if audioop else raw
    clip_info[n] = {"file": safe(n), "rate": rate, "channels": ch, "frames": frames}
    if dst.exists() and dst.stat().st_mtime >= src.stat().st_mtime:
        continue
    chans = []
    for c in range(ch):
        if ch == 1:
            chans.append(raw)
        else:
            chans.append(b"".join(raw[i * 4 + c * 2:i * 4 + c * 2 + 2] for i in range(frames)) if audioop is None
                         else audioop.tomono(raw, 2, 1 if c == 0 else 0, 0 if c == 0 else 1))
    blocks = (frames + BLOCK - 1) // BLOCK
    state = [(0, 0)] * ch
    parts = [b"DDAU", struct.pack("<IHHII", rate, ch, 0, frames, blocks)]
    for b in range(blocks):
        start, end = b * BLOCK, min(frames, (b + 1) * BLOCK)
        for c in range(ch):
            pred, idx = state[c]
            nib, pred2, idx2 = encode_channel_block(chans[c][start * 2:end * 2], pred, idx)
            parts.append(struct.pack("<hBB", pred, idx, 0))
            parts.append(nib.ljust((end - start + 1) // 2, b"\0"))
            state[c] = (pred2, idx2)
    dst.write_bytes(b"".join(parts))
    print(" ", n, rate, ch, frames, flush=True)

manifest = {"cues": cues, "enemyDeath": enemy_death, "attack": attack_sound,
            "music": {"packs": [packs.get(k, []) for k in ("MusicPackA", "MusicPackB", "MusicPackC", "MusicPackD")]},
            "named": NAMED, "clips": clip_info}
(out / "audio.json").write_text(json.dumps(manifest, indent=1), encoding="utf-8")
print(f"{len(clip_info)} clips, {len(cues)} cues, missing {missing}")
