#!/usr/bin/env python3
"""roaddist.py - jarak ke jalan terdekat (dipakai fix_scene.py & verify_scene.py).

Versi tepat: grid 100 m, periksa 3x3 sel di sekitar titik; kalau hasil masih lebih
besar dari ukuran sel, jatuh ke pemeriksaan semua segmen (jarang, tidak lambat).
"""
import math


class RoadDist:
    def __init__(self, segs, grid=100.0):
        self.segs = segs
        self.grid = grid
        self.cells = {}
        for s in segs:
            for key in self._keys(s[0], s[1], s[2], s[3]):
                self.cells.setdefault(key, []).append(s)

    def _keys(self, ax, az, bx, bz):
        g = self.grid
        cx0, cx1 = int(math.floor(min(ax, bx) / g)), int(math.floor(max(ax, bx) / g))
        cz0, cz1 = int(math.floor(min(az, bz) / g)), int(math.floor(max(az, bz) / g))
        for cx in range(cx0, cx1 + 1):
            for cz in range(cz0, cz1 + 1):
                yield (cx, cz)

    @staticmethod
    def _seg_d(ax, az, bx, bz, x, z):
        vx, vz = bx - ax, bz - az
        L2 = vx * vx + vz * vz
        t = 0.0 if L2 < 1e-9 else max(0.0, min(1.0, ((x - ax) * vx + (z - az) * vz) / L2))
        return math.hypot(x - (ax + vx * t), z - (az + vz * t))

    def __call__(self, x, z):
        g = self.grid
        cx, cz = int(math.floor(x / g)), int(math.floor(z / g))
        best = 1e9
        for i in (-1, 0, 1):
            for j in (-1, 0, 1):
                for s in self.cells.get((cx + i, cz + j), ()):
                    d = self._seg_d(s[0], s[1], s[2], s[3], x, z)
                    if d < best:
                        best = d
        if best > g:   # sel kosong di sekitar -> periksa semua
            for s in self.segs:
                d = self._seg_d(s[0], s[1], s[2], s[3], x, z)
                if d < best:
                    best = d
        return best
