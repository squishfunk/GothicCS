"""GothicCS: texture / mesh writers and the world-asset part of convert_cs.py
(world and player-weapon models -> .3DS, sprites -> textures + index).
All inputs are the user's own CS files; outputs stay in the local Gothic folder.
"""
from __future__ import annotations

import struct
from pathlib import Path

import numpy as np
from PIL import Image

import cs_mdl
import cs_spr

CSTRIKE = Path()
VALVE = Path()


def find_dir(parent: Path, name: str) -> Path:
    """Case-insensitive child lookup; created when absent."""
    if parent.is_dir():
        for child in parent.iterdir():
            if child.is_dir() and child.name.lower() == name.lower():
                return child
    path = parent / name
    path.mkdir(parents=True, exist_ok=True)
    return path


# ---------------------------------------------------------------- textures
def write_tga(img: Image.Image, path: Path):
    rgb = np.asarray(img.convert("RGB"), dtype=np.uint8)
    h, w, _ = rgb.shape
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, w, h, 24, 0)  # bottom-left origin
    path.write_bytes(header + rgb[::-1, :, ::-1].tobytes())


def write_tga32(img: Image.Image, path: Path):
    rgba = np.asarray(img.convert("RGBA"), dtype=np.uint8)
    h, w, _ = rgba.shape
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, w, h, 32, 8)  # bottom-left origin, 8 alpha bits
    path.write_bytes(header + rgba[::-1][:, :, [2, 1, 0, 3]].tobytes())


def write_ztex(img: Image.Image, path: Path, alpha: bool = False):
    """ZTEX v0, full mip chain stored smallest first.
    Format 8 = R5G6B5 (opaque), format 6 = A4R4G4B4 (with alpha)."""
    img = img.convert("RGBA" if alpha else "RGB")
    w, h = img.size
    mips = []
    x, y = w, h
    while True:
        mips.append(img.resize((x, y), Image.BOX) if (x, y) != (w, h) else img)
        if x == 1 and y == 1:
            break
        x, y = max(1, x >> 1), max(1, y >> 1)
    a = np.asarray(img.convert("RGB"), dtype=np.float64).reshape(-1, 3).mean(0).round().astype(int)
    avg = (0xFF << 24) | (int(a[0]) << 16) | (int(a[1]) << 8) | int(a[2])
    out = bytearray(b"ZTEX" + struct.pack("<8I", 0, 6 if alpha else 8, w, h, len(mips), w, h, avg))
    for m in reversed(mips):
        p = np.asarray(m, dtype=np.uint16)
        if alpha:
            v = ((p[..., 3] >> 4) << 12) | ((p[..., 0] >> 4) << 8) | ((p[..., 1] >> 4) << 4) | (p[..., 2] >> 4)
        else:
            v = ((p[..., 0] >> 3) << 11) | ((p[..., 1] >> 2) << 5) | (p[..., 2] >> 3)
        out += v.astype("<u2").tobytes()
    path.write_bytes(bytes(out))


def pow2(v: int) -> int:
    p = 1
    while p < v:
        p *= 2
    return p


# ---------------------------------------------------------------- 3DS writer
def _chunk(cid: int, *payload: bytes) -> bytes:
    body = b"".join(payload)
    return struct.pack("<HI", cid, 6 + len(body)) + body


def _cstr(s: str) -> bytes:
    return s.encode("ascii") + bytes(1)


def _rgb24(cid, r, g, b):
    return _chunk(cid, _chunk(0x0011, bytes([r, g, b])))


def write_3ds(path: Path, obj_name: str, pos3: np.ndarray, uv: np.ndarray, tris: np.ndarray,
              face_mat: list, materials: dict):
    """pos3 in 3DS axes (= CS axes) in cm, uv with v up, faces counter-clockwise seen from outside."""
    if len(pos3) > 65535 or len(tris) > 65535:
        raise ValueError(f"{obj_name}: too large for 3DS")
    mats = b""
    for mname, tex in materials.items():
        mats += _chunk(0xAFFF,
                       _chunk(0xA000, _cstr(mname)),
                       _rgb24(0xA010, 150, 150, 150),
                       _rgb24(0xA020, 255, 255, 255),
                       _rgb24(0xA030, 0, 0, 0),
                       _chunk(0xA200, _chunk(0x0030, struct.pack("<H", 100)), _chunk(0xA300, _cstr(tex))))
    faces_payload = struct.pack("<H", len(tris)) + b"".join(
        struct.pack("<4H", int(a), int(b), int(c), 0x7) for a, b, c in tris)
    sub = b""
    for mname in materials:
        ids = [i for i, m in enumerate(face_mat) if m == mname]
        if ids:
            sub += _chunk(0x4130, _cstr(mname), struct.pack("<H", len(ids)), struct.pack(f"<{len(ids)}H", *ids))
    sub += _chunk(0x4150, np.ones(len(tris), "<u4").tobytes())
    trimesh = _chunk(0x4100,
                     _chunk(0x4110, struct.pack("<H", len(pos3)), pos3.astype("<f4").tobytes()),
                     _chunk(0x4140, struct.pack("<H", len(uv)), uv.astype("<f4").tobytes()),
                     _chunk(0x4160, np.array([1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0], "<f4").tobytes()),
                     _chunk(0x4120, faces_payload, sub))
    edit = _chunk(0x3D3D, _chunk(0x3D3E, struct.pack("<I", 3)), mats,
                  _chunk(0x0100, struct.pack("<f", 1.0)),
                  _chunk(0x4000, _cstr(obj_name), trimesh))
    path.write_bytes(_chunk(0x4D4D, _chunk(0x0002, struct.pack("<I", 3)), edit))


# ---------------------------------------------------------------- static models
def convert_static(mdl: Path, prefix: str, mesh_dir: Path, tex_dir: Path, tex_c_dir: Path,
                   anchor_bone: str | None = None) -> str:
    """Pose frame 0 of the first sequence, skin it and write one .3DS (cm, CS axes).
    anchor_bone: express the mesh in that bone's space (p_ models: 'Bip01 R Hand')."""
    model = cs_mdl.load(mdl)
    name = f"{prefix}_{mdl.stem.split('_', 1)[1].upper()}"
    bones = cs_mdl.bone_matrices(model, model.sequences[0], 0)
    to_anchor = np.eye(4)
    if anchor_bone:
        idx = next(i for i, b in enumerate(model.bones) if b.name.lower() == anchor_bone.lower())
        m = np.eye(4)
        m[:3, :4] = bones[idx]
        to_anchor = np.linalg.inv(m)
    materials, tex_names = {}, []
    for i, t in enumerate(model.textures):
        tname = f"{name}_T{i:02d}"
        img = Image.fromarray(t.rgba()[..., :3])
        # Gothic (D3D7) needs power-of-two textures on world meshes, otherwise the
        # mesh renders white (e.g. the C4 is 204x56). UVs are relative -> stretch.
        pw, ph = pow2(img.width), pow2(img.height)
        if (pw, ph) != img.size:
            img = img.resize((pw, ph), Image.LANCZOS)
        write_tga(img, tex_dir / f"{tname}.TGA")
        write_ztex(img, tex_c_dir / f"{tname}-C.TEX")
        tex_names.append(tname)
    pos, uv, tris, face_mat = [], [], [], []
    for bp in model.bodyparts:
        sm = bp.models[0]
        wv = np.einsum("nij,nj->ni", bones[sm.vert_bone][:, :, :3], sm.verts) + bones[sm.vert_bone][:, :, 3]
        wv = (to_anchor[:3, :3] @ wv.T).T + to_anchor[:3, 3]
        wn = np.einsum("nij,nj->ni", bones[sm.norm_bone][:, :, :3], sm.norms)
        wn = (to_anchor[:3, :3] @ wn.T).T
        for mesh in sm.meshes:
            tex = int(model.skins[0][mesh.skinref])
            tw, th = model.textures[tex].width, model.textures[tex].height
            mname = tex_names[tex]
            materials[mname] = f"{mname}.TGA"
            for tri in mesh.tris:
                tri = list(tri)
                a, b, c = (wv[t[0]] for t in tri)
                if np.dot(np.cross(b - a, c - a), sum(wn[t[1]] for t in tri)) < 0:
                    tri[1], tri[2] = tri[2], tri[1]
                base = len(pos)
                for t in tri:   # unwelded: every corner keeps its own UV
                    pos.append(wv[t[0]] * 2.54)
                    uv.append((t[2] / tw, 1.0 - t[3] / th))
                tris.append((base, base + 1, base + 2))
                face_mat.append(mname)
    write_3ds(mesh_dir / f"{name}.3DS", name[:10], np.array(pos), np.array(uv), np.array(tris), face_mat, materials)
    return name


# ---------------------------------------------------------------- sprites
def convert_sprite(spr: Path, tex_dir: Path, tex_c_dir: Path, out_dir: Path) -> str:
    """Every frame -> CSS_<NAME>_Fnn.TGA (+ -C.TEX), padded to a power of two.
    Index <NAME>.txt: 'type T format F width W height H frames N', then per frame
    'w h originX originY uMax vMax'."""
    sp = cs_spr.load(spr)
    name = spr.stem.upper()
    alpha = sp.tex_format in (cs_spr.SPR_INDEXALPHA, cs_spr.SPR_ALPHTEST)
    lines = [f"type {sp.type} format {sp.tex_format} width {sp.width} height {sp.height} frames {len(sp.frames)}"]
    for i, fr in enumerate(sp.frames):
        rgba = sp.rgba(i)
        pw, ph = pow2(fr.width), pow2(fr.height)
        canvas = np.zeros((ph, pw, 4), np.uint8)
        canvas[: fr.height, : fr.width] = rgba
        img = Image.fromarray(canvas, "RGBA")
        tname = f"CSS_{name}_F{i:02d}"
        if alpha:
            write_tga32(img, tex_dir / f"{tname}.TGA")
        else:
            write_tga(img, tex_dir / f"{tname}.TGA")
        write_ztex(img, tex_c_dir / f"{tname}-C.TEX", alpha)
        lines.append(f"{fr.width} {fr.height} {fr.origin[0]} {fr.origin[1]} {fr.width / pw:.6f} {fr.height / ph:.6f}")
    (out_dir / f"{name}.txt").write_text("\n".join(lines) + "\n", encoding="ascii")
    return name


def convert_world_assets(data: Path, tex_dir: Path, tex_c_dir: Path):
    mesh_dir = find_dir(data, "Meshes")
    mesh_c = find_dir(mesh_dir, "_compiled")
    for f in list(mesh_c.glob("CSW_*.MRM")) + list(mesh_c.glob("CSP_*.MRM")):
        f.unlink()   # stale compiled meshes: Gothic recompiles from the new .3DS
    statics = 0
    for m in sorted((CSTRIKE / "models").glob("w_*.mdl")):
        if m.stem.lower().endswith("t") and (CSTRIKE / "models" / (m.stem[:-1] + ".mdl")).exists():
            continue   # texture-only companion file
        try:
            convert_static(m, "CSW", mesh_dir, tex_dir, tex_c_dir)
            statics += 1
        except Exception as e:
            print(f"  {m.name}: skipped ({e})")
    for m in sorted((CSTRIKE / "models").glob("p_*.mdl")):
        try:
            convert_static(m, "CSP", mesh_dir, tex_dir, tex_c_dir, "Bip01 R Hand")
            statics += 1
        except Exception as e:
            print(f"  {m.name}: skipped ({e})")
    print(f"world / player weapon meshes: {statics} -> {mesh_dir}")

    spr_dir = data / "GothicCS" / "sprites"
    spr_dir.mkdir(parents=True, exist_ok=True)
    sprites = {}
    for root in (VALVE / "sprites", CSTRIKE / "sprites"):   # cstrike overrides valve
        for f in root.glob("*.spr"):
            sprites[f.stem.lower()] = f
    done = 0
    for f in sorted(sprites.values()):
        try:
            convert_sprite(f, tex_dir, tex_c_dir, spr_dir)
            done += 1
        except Exception as e:
            print(f"  {f.name}: skipped ({e})")
    print(f"sprites: {done} -> {spr_dir}")

    # sniper scope arcs (TGA with alpha) -> CSS_SCOPE_ARC*.TGA
    for f in sorted((CSTRIKE / "sprites").glob("scope_arc*.tga")):
        img = Image.open(f).convert("RGBA")
        tname = "CSS_" + f.stem.upper()
        write_tga32(img, tex_dir / f"{tname}.TGA")
        write_ztex(img, tex_c_dir / f"{tname}-C.TEX", True)

    # plain white texture for solid HUD fills (crosshair, bars, flashbang white-out)
    white = Image.new("RGB", (8, 8), (255, 255, 255))
    write_tga(white, tex_dir / "CSS_WHITE.TGA")
    write_ztex(white, tex_c_dir / "CSS_WHITE-C.TEX")

    # old-style menu texts (buy menus) as shipped with the user's CS
    import shutil
    shutil.copy2(CSTRIKE / "titles.txt", data / "GothicCS" / "titles.txt")
    shutil.copy2(CSTRIKE / "sprites" / "hud.txt", data / "GothicCS" / "hud.txt")

    # engine sounds CS uses from the Half-Life base game (explosions, debris, steps)
    sfx_dir = find_dir(find_dir(data, "Sound"), "SFX")
    extra = ["weapons/explode3.wav", "weapons/explode4.wav", "weapons/explode5.wav", "weapons/debris1.wav",
             "weapons/debris2.wav", "weapons/debris3.wav", "common/bodysplat.wav", "player/pl_tile1.wav",
             "player/pl_tile2.wav", "player/pl_tile3.wav", "player/pl_tile4.wav", "items/9mmclip1.wav",
             "items/ammopickup2.wav", "items/gunpickup2.wav"]
    n = 0
    for rel in extra:
        for root in (CSTRIKE / "sound", VALVE / "sound"):
            if (root / rel).exists():
                shutil.copy2(root / rel, sfx_dir / ("CS_" + rel.replace("/", "_").upper()))
                n += 1
                break
    print(f"extra sounds: {n}, titles.txt, scope arcs")
