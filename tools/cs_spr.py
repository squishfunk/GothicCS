"""GothicCS: reader for GoldSrc sprites (.spr, version 2) - own implementation
of the public file layout. Reads the user's own CS files only.

Header (40 bytes): "IDSP", version, type, texFormat, boundingradius(f), width,
height, numframes, beamlength(f), synctype; then u16 palette size + RGB
palette; then frames: i32 frametype (0 = single, else group of n frames with
float intervals), each single frame = i32 originX, originY, width, height +
w*h palette indices.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

import numpy as np

SPR_NORMAL, SPR_ADDITIVE, SPR_INDEXALPHA, SPR_ALPHTEST = 0, 1, 2, 3
TYPE_NAMES = {0: "parallel_upright", 1: "facing_upright", 2: "parallel", 3: "oriented", 4: "parallel_oriented"}


@dataclass
class Frame:
    origin: tuple
    width: int
    height: int
    pixels: np.ndarray   # [h, w] indices


@dataclass
class Sprite:
    type: int
    tex_format: int
    width: int
    height: int
    palette: np.ndarray  # [n, 3]
    frames: list

    def rgba(self, i: int) -> np.ndarray:
        f = self.frames[i]
        pal = np.zeros((256, 3), np.uint8)
        pal[: len(self.palette)] = self.palette
        img = np.zeros((f.height, f.width, 4), np.uint8)
        if self.tex_format == SPR_INDEXALPHA:
            img[..., :3] = pal[255]
            img[..., 3] = f.pixels
        else:
            img[..., :3] = pal[f.pixels]
            img[..., 3] = 255
            if self.tex_format == SPR_ALPHTEST:
                img[f.pixels == 255, 3] = 0
        return img


def load(path: Path) -> Sprite:
    d = Path(path).read_bytes()
    if d[:4] != b"IDSP":
        raise ValueError(f"{path}: not a sprite")
    version, typ, fmt = struct.unpack_from("<3i", d, 4)
    if version != 2:
        raise ValueError(f"{path}: sprite version {version}")
    w, h, n = struct.unpack_from("<3i", d, 20)
    p = 40
    ncol = struct.unpack_from("<H", d, p)[0]
    p += 2
    pal = np.frombuffer(d, np.uint8, ncol * 3, p).reshape(ncol, 3)
    p += ncol * 3
    frames = []

    def single(p):
        ox, oy, fw, fh = struct.unpack_from("<4i", d, p)
        p += 16
        pix = np.frombuffer(d, np.uint8, fw * fh, p).reshape(fh, fw)
        return Frame((ox, oy), fw, fh, pix), p + fw * fh

    for _ in range(n):
        ftype = struct.unpack_from("<i", d, p)[0]
        p += 4
        if ftype == 0:
            fr, p = single(p)
            frames.append(fr)
        else:
            gn = struct.unpack_from("<i", d, p)[0]
            p += 4 + gn * 4
            for _ in range(gn):
                fr, p = single(p)
                frames.append(fr)
    return Sprite(typ, fmt, w, h, pal, frames)
