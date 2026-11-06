"""GothicCS: convert the user's own Counter-Strike 1.6 files for the plugin.

  python convert_cs.py --gothic GAME --cstrike CS   convert all assets
  Add --only v_ak47 to convert selected models
  python convert_cs.py --gothic GAME --uninstall    remove converted assets

Reads   paths supplied through --cstrike and --valve
Writes  <Gothic>\\_work\\Data\\GothicCS\\viewmodels\\<NAME>.csvm   model + animation data
        <Gothic>\\_work\\Data\\Textures\\CSV_<NAME>_Tnn.TGA          textures (+ _compiled\\-C.TEX)
        <Gothic>\\_work\\Data\\Sound\\SFX\\CS_<DIR>_<FILE>.WAV        sounds (weapons, player, items, radio)
These are the user's own game files: they stay on this PC (never in the repo).

Viewmodels are mirrored to the right hand, like CS 1.6 with the default
cl_righthand 1 (the .mdl files are modelled left-handed). Triangles are
re-wound so that every one is counter-clockwise seen from outside
(right-handed CS space), checked against the model's own normals.

.csvm v1 (little-endian):
  "CSVM" u32 version u32 flags(1 = mirrored)
  u32 n, n x { char[64] texture, u16 w, u16 h, u32 flags }
  u32 n, n x { i32 parent, char[32] name }                 bones
  u32 n, n x { f32[3] pos, i32 bone }                      vertices (bone space, inches)
  u32 n, n x { f32[3] normal, i32 bone }                   normals (bone space)
  u32 n, n x { u16 v[3], u16 n[3], f32 uv[6], u16 tex, u16 bodypart }
  u32 n, n x { char[32] name }                             bodyparts
  u32 n, n x { i32 bone, f32[3] org }                      attachments
  u32 n, n x { char[32] name, f32 fps, i32 flags, u32 frames, u32 events,
               events x { i32 frame, i32 event, char[64] options },
               frames x bones x { f32 pos[3], f32 quat[4] (x y z w) } }
  Event 5004 options are rewritten to the Gothic SFX name of the sound.
"""
from __future__ import annotations

import argparse
import shutil
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import cs_assets
import cs_mdl
from cs_assets import convert_world_assets, write_tga, write_ztex

CSTRIKE = Path()
GOTHIC = Path()
VALVE = Path()
SOUND_DIRS = ["weapons", "player", "items", "radio"]


def find_dir(parent: Path, name: str) -> Path:
    """Case-insensitive child lookup; created when absent."""
    if parent.is_dir():
        for child in parent.iterdir():
            if child.is_dir() and child.name.lower() == name.lower():
                return child
    path = parent / name
    path.mkdir(parents=True, exist_ok=True)
    return path


def sfx_name(cs_path: str) -> str:
    """'weapons/ak47-1.wav' -> 'CS_WEAPONS_AK47-1.WAV'"""
    p = cs_path.replace("\\", "/").lstrip("/")
    return "CS_" + p.replace("/", "_").upper()


def fixed(s: str, n: int) -> bytes:
    b = s.encode("latin-1")[: n - 1]
    return b + b"\0" * (n - len(b))


# ---------------------------------------------------------------- viewmodel
def convert_viewmodel(mdl: Path, tex_dir: Path, tex_c_dir: Path, out_dir: Path) -> str:
    model = cs_mdl.load(mdl)
    name = mdl.stem[2:].upper() if mdl.stem.lower().startswith("v_") else mdl.stem.upper()
    mirror = True

    # textures
    tex_names = []
    for i, t in enumerate(model.textures):
        tname = f"CSV_{name}_T{i:02d}"
        img = Image.fromarray(t.rgba()[..., :3])
        write_tga(img, tex_dir / f"{tname}.TGA")
        write_ztex(img, tex_c_dir / f"{tname}-C.TEX")
        tex_names.append((f"{tname}.TGA", t.width, t.height, t.flags))

    # bind pose (first frame of the first sequence) for the winding check
    bind = cs_mdl.bone_matrices(model, model.sequences[0], 0)

    verts, vbones, norms, nbones, tris, bodyparts = [], [], [], [], [], []
    flipped = 0
    for bpi, bp in enumerate(model.bodyparts):
        bodyparts.append(bp.name)
        sm = bp.models[0]
        v0, n0 = len(verts), len(norms)
        verts += [tuple(v) for v in sm.verts]
        vbones += [int(b) for b in sm.vert_bone]
        norms += [tuple(n) for n in sm.norms]
        nbones += [int(b) for b in sm.norm_bone]
        wv = np.einsum("nij,nj->ni", bind[sm.vert_bone][:, :, :3], sm.verts) + bind[sm.vert_bone][:, :, 3]
        wn = np.einsum("nij,nj->ni", bind[sm.norm_bone][:, :, :3], sm.norms)
        for mesh in sm.meshes:
            tex = int(model.skins[0][mesh.skinref])
            tw, th = model.textures[tex].width, model.textures[tex].height
            for tri in mesh.tris:
                a, b, c = (wv[t[0]] for t in tri)
                face = np.cross(b - a, c - a)
                avg = sum(wn[t[1]] for t in tri)
                tri = list(tri)
                if np.dot(face, avg) < 0:
                    tri[1], tri[2] = tri[2], tri[1]
                    flipped += 1
                if mirror:   # a reflection reverses the winding
                    tri[1], tri[2] = tri[2], tri[1]
                tris.append(([t[0] + v0 for t in tri], [t[1] + n0 for t in tri],
                             [c for t in tri for c in (t[2] / tw, t[3] / th)], tex, bpi))

    def mir(v):
        return (v[0], -v[1], v[2]) if mirror else tuple(v)

    out = bytearray(b"CSVM" + struct.pack("<II", 1, 1 if mirror else 0))
    out += struct.pack("<I", len(tex_names))
    for tname, w, h, fl in tex_names:
        out += fixed(tname, 64) + struct.pack("<HHI", w, h, fl)
    out += struct.pack("<I", len(model.bones))
    for b in model.bones:
        out += struct.pack("<i", b.parent) + fixed(b.name, 32)
    out += struct.pack("<I", len(verts))
    for v, b in zip(verts, vbones):
        out += struct.pack("<3fi", *mir(v), b)
    out += struct.pack("<I", len(norms))
    for n, b in zip(norms, nbones):
        out += struct.pack("<3fi", *mir(n), b)
    out += struct.pack("<I", len(tris))
    for vi, ni, uv, tex, bpi in tris:
        out += struct.pack("<3H3H6fHH", *vi, *ni, *uv, tex, bpi)
    out += struct.pack("<I", len(bodyparts))
    for bpn in bodyparts:
        out += fixed(bpn, 32)
    out += struct.pack("<I", len(model.attachments))
    for att in model.attachments:
        out += struct.pack("<i3f", att.bone, *mir(att.org))
    out += struct.pack("<I", len(model.sequences))
    sounds = set()
    for seq in model.sequences:
        out += fixed(seq.name, 32) + struct.pack("<fiII", seq.fps, seq.flags, seq.num_frames, len(seq.events))
        for ev in seq.events:
            opt = ev.options
            if ev.event == 5004:
                sounds.add(opt)
                opt = sfx_name(opt)
            out += struct.pack("<ii", ev.frame, ev.event) + fixed(opt, 64)
        frames = np.zeros((seq.num_frames, len(model.bones), 7), np.float32)
        for f in range(seq.num_frames):
            for b in range(len(model.bones)):
                q = cs_mdl.euler_to_quat(seq.rot[f, b])
                p = seq.pos[f, b]
                if mirror:   # S R S with S = diag(1,-1,1): quaternion (-x, y, -z, w)
                    q = np.array([-q[0], q[1], -q[2], q[3]])
                    p = np.array([p[0], -p[1], p[2]])
                frames[f, b, :3] = p
                frames[f, b, 3:] = q
        out += frames.astype("<f4").tobytes()
    (out_dir / f"{name}.csvm").write_bytes(bytes(out))
    print(f"  {mdl.name}: {name}.csvm  {len(model.bones)} bones, {len(tris)} tris ({flipped} re-wound), "
          f"{len(model.sequences)} sequences, {len(tex_names)} textures")
    return name


def main() -> int:
    global CSTRIKE, GOTHIC, VALVE
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--only", nargs="*", default=None, help="model names, e.g. v_ak47 v_knife")
    ap.add_argument("--uninstall", action="store_true")
    ap.add_argument("--gothic", type=Path, required=True, help="Gothic installation directory")
    ap.add_argument("--cstrike", type=Path, help="Counter-Strike cstrike directory")
    ap.add_argument("--valve", type=Path, help="Half-Life valve directory (defaults to sibling of cstrike)")
    a = ap.parse_args()
    GOTHIC = a.gothic.resolve()
    if not a.uninstall:
        if a.cstrike is None:
            ap.error("--cstrike is required for conversion")
        CSTRIKE = a.cstrike.resolve()
        VALVE = a.valve.resolve() if a.valve else CSTRIKE.parent / "valve"
        if not (CSTRIKE / "models" / "v_ak47.mdl").is_file() or not (VALVE / "sprites").is_dir():
            ap.error("Expected installed Counter-Strike models and Half-Life valve/sprites")
        cs_assets.CSTRIKE, cs_assets.VALVE = CSTRIKE, VALVE
    if not (GOTHIC / "system" / "GOTHIC.EXE").is_file():
        print(f"{GOTHIC}: no system\\GOTHIC.EXE")
        return 1
    data = find_dir(find_dir(GOTHIC, "_work"), "Data")
    tex_dir = find_dir(data, "Textures")
    tex_c_dir = find_dir(tex_dir, "_compiled")
    sfx_dir = find_dir(find_dir(data, "Sound"), "SFX")
    vm_dir = data / "GothicCS" / "viewmodels"

    if a.uninstall:
        n = 0
        mesh_dir = find_dir(data, "Meshes")
        mesh_c = find_dir(mesh_dir, "_compiled")
        for folder, pattern in [(tex_dir, "CSV_*.TGA"), (tex_c_dir, "CSV_*-C.TEX"), (sfx_dir, "CS_*.WAV"),
                                (tex_dir, "CSW_*.TGA"), (tex_c_dir, "CSW_*-C.TEX"), (tex_dir, "CSP_*.TGA"),
                                (tex_c_dir, "CSP_*-C.TEX"), (tex_dir, "CSS_*.TGA"), (tex_c_dir, "CSS_*-C.TEX"),
                                (mesh_dir, "CSW_*.3DS"), (mesh_dir, "CSP_*.3DS"), (mesh_c, "CSW_*.MRM"),
                                (mesh_c, "CSP_*.MRM")]:
            for f in folder.glob(pattern):
                f.unlink()
                n += 1
        if (data / "GothicCS").exists():
            shutil.rmtree(data / "GothicCS")
        print(f"removed {n} files and _work\\Data\\GothicCS")
        return 0

    if not (CSTRIKE / "models").is_dir():
        print(f"{CSTRIKE}\\models not found - verify the Counter-Strike files in Steam")
        return 1
    vm_dir.mkdir(parents=True, exist_ok=True)
    models = sorted((CSTRIKE / "models").glob("v_*.mdl"))
    if a.only:
        wanted = {m.lower().removesuffix(".mdl") for m in a.only}
        models = [m for m in models if m.stem.lower() in wanted]
    print(f"viewmodels -> {vm_dir}")
    for m in models:
        if m.stem.lower().endswith("_r"):   # shield / knife_r variants: not needed
            continue
        convert_viewmodel(m, tex_dir, tex_c_dir, vm_dir)

    if not a.only:
        convert_world_assets(data, tex_dir, tex_c_dir)

    n = 0
    for d in SOUND_DIRS:
        src = CSTRIKE / "sound" / d
        if not src.is_dir():
            continue
        for f in src.rglob("*.wav"):
            rel = f.relative_to(CSTRIKE / "sound").as_posix()
            shutil.copy2(f, sfx_dir / sfx_name(rel))
            n += 1
    print(f"sounds: {n} files -> {sfx_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
