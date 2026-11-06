"""GothicCS: reader for GoldSrc studio models (.mdl, version 10).

Own implementation written from the publicly documented file layout
(little-endian, every *index field is a byte offset from the file start).
Reads the user's own Counter-Strike files; nothing is redistributed.

Model space is the GoldSrc one: X forward, Y left, Z up, units = inches.
Every vertex belongs to exactly one bone and is stored in that bone's space.
"""
from __future__ import annotations

import math
import struct
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

STUDIO_NF_CHROME = 0x02
STUDIO_NF_ADDITIVE = 0x20
STUDIO_NF_MASKED = 0x40
STUDIO_LOOPING = 0x01


def _cstr(b: bytes) -> str:
    return b.split(b"\0", 1)[0].decode("latin-1")


@dataclass
class Bone:
    name: str
    parent: int
    value: tuple   # default pos xyz, rot xyz (radians)
    scale: tuple


@dataclass
class Event:
    frame: int
    event: int
    options: str


@dataclass
class Sequence:
    name: str
    fps: float
    flags: int
    num_frames: int
    events: list
    # baked local pose per frame: pos [frames, bones, 3], euler [frames, bones, 3]
    pos: np.ndarray = None
    rot: np.ndarray = None


@dataclass
class Texture:
    name: str
    flags: int
    width: int
    height: int
    pixels: np.ndarray   # [h, w] palette indices
    palette: np.ndarray  # [256, 3]

    def rgba(self) -> np.ndarray:
        img = np.zeros((self.height, self.width, 4), np.uint8)
        img[..., :3] = self.palette[self.pixels]
        img[..., 3] = 255
        if self.flags & STUDIO_NF_MASKED:
            img[self.pixels == 255, 3] = 0
        return img


@dataclass
class Mesh:
    skinref: int
    # triangles as (vertindex, normindex, s, t) tuples, in strip/fan order
    tris: list


@dataclass
class SubModel:
    name: str
    verts: np.ndarray       # [n, 3] in bone space
    vert_bone: np.ndarray   # [n]
    norms: np.ndarray       # [m, 3] in bone space
    norm_bone: np.ndarray   # [m]
    meshes: list


@dataclass
class BodyPart:
    name: str
    models: list


@dataclass
class Attachment:
    bone: int
    org: tuple


@dataclass
class Model:
    name: str
    bones: list
    sequences: list
    textures: list
    skins: np.ndarray       # [families, skinref]
    bodyparts: list
    attachments: list = field(default_factory=list)


class _Reader:
    def __init__(self, data: bytes):
        self.d = data

    def i(self, off, n=1):
        v = struct.unpack_from(f"<{n}i", self.d, off)
        return v if n > 1 else v[0]

    def f(self, off, n=1):
        v = struct.unpack_from(f"<{n}f", self.d, off)
        return v if n > 1 else v[0]


def _anim_value(d: bytes, base: int, frame: int) -> int:
    """Decode one RLE channel (mstudioanimvalue_t stream starting at `base`)."""
    p = base
    k = frame
    while True:
        valid, total = d[p], d[p + 1]
        if total == 0:
            return 0   # malformed stream; treat as no motion
        if k < total:
            break
        k -= total
        p += (valid + 1) * 2
    if valid > k:
        return struct.unpack_from("<h", d, p + (k + 1) * 2)[0]
    return struct.unpack_from("<h", d, p + valid * 2)[0]


def load(path: Path) -> Model:
    path = Path(path)
    d = path.read_bytes()
    r = _Reader(d)
    if d[:4] != b"IDST" or r.i(4) != 10:
        raise ValueError(f"{path}: not a v10 studio model")
    name = _cstr(d[8:72])
    (numbones, boneindex, numbc, bcindex, numhb, hbindex, numseq, seqindex, numseqgroups, seqgroupindex,
     numtex, texindex, texdataindex, numskinref, numskinfam, skinindex, numbodyparts, bodypartindex,
     numatt, attindex) = r.i(140, 20)

    bones = []
    for b in range(numbones):
        o = boneindex + b * 112
        bones.append(Bone(_cstr(d[o:o + 32]), r.i(o + 32), r.f(o + 64, 6), r.f(o + 88, 6)))

    attachments = []
    for a in range(numatt):
        o = attindex + a * 88
        attachments.append(Attachment(r.i(o + 36), r.f(o + 40, 3)))

    # external sequence groups: <name>01.mdl ...
    group_data = {0: d}
    for g in range(1, numseqgroups):
        ext = path.with_name(f"{path.stem}{g:02d}.mdl")
        if ext.exists():
            group_data[g] = ext.read_bytes()

    sequences = []
    for s in range(numseq):
        o = seqindex + s * 176
        label = _cstr(d[o:o + 32])
        fps, flags = r.f(o + 32), r.i(o + 36)
        numevents, eventindex, numframes = r.i(o + 48), r.i(o + 52), r.i(o + 56)
        animindex = r.i(o + 124)   # numblends @120, animindex @124
        seqgroup = r.i(o + 156)
        events = []
        for e in range(numevents):
            eo = eventindex + e * 76
            events.append(Event(r.i(eo), r.i(eo + 4), _cstr(d[eo + 12:eo + 76])))
        seq = Sequence(label, fps, flags, numframes, events)
        gd = group_data.get(seqgroup)
        if gd is None:
            raise ValueError(f"{path}: missing sequence group {seqgroup} for {label}")
        pos = np.zeros((numframes, numbones, 3), np.float32)
        rot = np.zeros((numframes, numbones, 3), np.float32)
        for b, bone in enumerate(bones):
            ao = animindex + b * 12
            offsets = struct.unpack_from("<6H", gd, ao)
            for j in range(6):
                for fr in range(numframes):
                    raw = _anim_value(gd, ao + offsets[j], fr) if offsets[j] else 0
                    v = bone.value[j] + raw * bone.scale[j]
                    if j < 3:
                        pos[fr, b, j] = v
                    else:
                        rot[fr, b, j - 3] = v
        seq.pos, seq.rot = pos, rot
        sequences.append(seq)

    # textures may live in <name>T.mdl
    td, tr = d, r
    if numtex == 0:
        tpath = path.with_name(path.stem + "T.mdl")
        td = tpath.read_bytes()
        tr = _Reader(td)
        numtex, texindex, texdataindex, numskinref, numskinfam, skinindex = tr.i(180, 6)
    textures = []
    for t in range(numtex):
        o = texindex + t * 80
        tname = _cstr(td[o:o + 64])
        flags, w, h, index = tr.i(o + 64, 4)
        pix = np.frombuffer(td, np.uint8, w * h, index).reshape(h, w)
        pal = np.frombuffer(td, np.uint8, 768, index + w * h).reshape(256, 3)
        textures.append(Texture(tname, flags, w, h, pix, pal))
    skins = np.frombuffer(td, "<i2", numskinref * numskinfam, skinindex).reshape(numskinfam, numskinref)

    bodyparts = []
    for bp in range(numbodyparts):
        o = bodypartindex + bp * 76
        bpname = _cstr(d[o:o + 64])
        nummodels, base, modelindex = r.i(o + 64, 3)
        models = []
        for m in range(nummodels):
            mo = modelindex + m * 112
            mname = _cstr(d[mo:mo + 64])
            nummesh, meshindex, numverts, vertinfoindex, vertindex, numnorms, norminfoindex, normindex = r.i(mo + 72, 8)
            verts = np.frombuffer(d, "<f4", numverts * 3, vertindex).reshape(numverts, 3).copy()
            vbone = np.frombuffer(d, np.uint8, numverts, vertinfoindex).copy()
            norms = np.frombuffer(d, "<f4", numnorms * 3, normindex).reshape(numnorms, 3).copy()
            nbone = np.frombuffer(d, np.uint8, numnorms, norminfoindex).copy()
            meshes = []
            for me in range(nummesh):
                meo = meshindex + me * 20
                numtris, triindex, skinref = r.i(meo, 3)
                meshes.append(Mesh(skinref, _read_tricmds(d, triindex)))
            models.append(SubModel(mname, verts, vbone, norms, nbone, meshes))
        bodyparts.append(BodyPart(bpname, models))

    return Model(name, bones, sequences, textures, skins, bodyparts, attachments)


def _read_tricmds(d: bytes, off: int) -> list:
    """Triangle strips (n > 0) and fans (n < 0); each vertex = vertindex, normindex, s, t."""
    tris = []
    p = off
    while True:
        n = struct.unpack_from("<h", d, p)[0]
        p += 2
        if n == 0:
            break
        fan = n < 0
        n = abs(n)
        vs = []
        for _ in range(n):
            vi, ni, s, t = struct.unpack_from("<4h", d, p)
            p += 8
            vs.append((vi, ni, s, t))
        for k in range(2, n):
            if fan:
                tris.append((vs[0], vs[k - 1], vs[k]))
            elif k % 2 == 0:
                tris.append((vs[k - 2], vs[k - 1], vs[k]))
            else:
                tris.append((vs[k - 1], vs[k - 2], vs[k]))
    return tris


# ---------------------------------------------------------------- pose math
def euler_to_quat(a) -> np.ndarray:
    """GoldSrc angle order: a[0] about X (roll), a[1] about Y (pitch), a[2] about Z (yaw);
    R = Rz(a2) * Ry(a1) * Rx(a0). Returns (x, y, z, w)."""
    sr, cr = math.sin(a[0] * 0.5), math.cos(a[0] * 0.5)
    sp, cp = math.sin(a[1] * 0.5), math.cos(a[1] * 0.5)
    sy, cy = math.sin(a[2] * 0.5), math.cos(a[2] * 0.5)
    return np.array([
        sr * cp * cy - cr * sp * sy,
        cr * sp * cy + sr * cp * sy,
        cr * cp * sy - sr * sp * cy,
        cr * cp * cy + sr * sp * sy,
    ], np.float64)


def quat_to_matrix(q) -> np.ndarray:
    x, y, z, w = q
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ])


def bone_matrices(model: Model, seq: Sequence, frame: int) -> np.ndarray:
    """World (model-space) 3x4 matrices of every bone for one frame."""
    out = np.zeros((len(model.bones), 3, 4))
    for b, bone in enumerate(model.bones):
        m = np.zeros((3, 4))
        m[:, :3] = quat_to_matrix(euler_to_quat(seq.rot[frame, b]))
        m[:, 3] = seq.pos[frame, b]
        if bone.parent >= 0:
            p = out[bone.parent]
            m = np.hstack([p[:, :3] @ m[:, :3], (p[:, :3] @ m[:, 3] + p[:, 3])[:, None]])
        out[b] = m
    return out
