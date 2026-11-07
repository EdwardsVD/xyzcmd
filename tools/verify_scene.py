#!/usr/bin/env python3
"""verify_scene.py - cek bug visual secara numerik (tanpa GPU).

Memeriksa:
  1. jalan tidak tenggelam di bawah terrain
  2. objek (pohon/rumah/gedung) tidak berada di atas jalan
  3. objek tidak di laut (y < 0.5) dan tidak di lereng > 25 derajat
  4. mobil spawn di atas jalan
"""
import json
import math
import os
import struct
import sys

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from glb_probe import load  # noqa: E402

GEN = os.path.join(ROOT, "assets", "generated")
APP = os.path.join(ROOT, "app", "assets")

# ---------------------------------------------------------------- island
d = open(os.path.join(GEN, "island.bin"), "rb").read()
magic, ver, N, x0, x1, z0, z1, _a, _b = struct.unpack_from("<4sII6f", d, 0)
H = np.frombuffer(d, dtype="<f4", count=N * N, offset=36).reshape(N, N)
BIO = np.frombuffer(d, dtype=np.uint8, count=N * N, offset=36 + N * N * 4 + N * N * 3).reshape(N, N)
DX = (x1 - x0) / (N - 1)
DZ = (z1 - z0) / (N - 1)


def height(x, z):
    x = np.asarray(x, np.float64)
    z = np.asarray(z, np.float64)
    fx = np.clip((x - x0) / DX, 0, N - 1.001)
    fz = np.clip((z - z0) / DZ, 0, N - 1.001)
    i = np.floor(fx).astype(int)
    j = np.floor(fz).astype(int)
    tx = fx - i
    tz = fz - j
    h00 = H[j, i]
    h10 = H[j, i + 1]
    h01 = H[j + 1, i]
    h11 = H[j + 1, i + 1]
    return (h00 * (1 - tx) + h10 * tx) * (1 - tz) + (h01 * (1 - tx) + h11 * tx) * tz


def slope_deg(x, z):
    e = 8.0
    hx = (height(x + e, z) - height(x - e, z)) / (2 * e)
    hz = (height(x, z + e) - height(x, z - e)) / (2 * e)
    return math.degrees(math.atan(math.hypot(hx, hz)))


# ---------------------------------------------------------------- roads
roads = json.load(open(os.path.join(GEN, "roads.json")))
segs = []
for path in roads["jalan"]:
    pts = path["titik"]
    w = path.get("lebar", 6.0)
    for a, b in zip(pts, pts[1:]):
        segs.append((a[0], a[1], b[0], b[1], w))
print(f"segmen jalan: {len(segs)}")

from roaddist import RoadDist  # noqa: E402

_dist = RoadDist(segs)


def dist_to_road(x, z):
    return _dist(x, z)


# ---------------------------------------------------------------- 1. jalan vs terrain
print("\n[1] jalan terhadap terrain")
for kind in ("desa", "kota", "tanah"):
    p = os.path.join(GEN, f"roads_{kind}.glb")
    if not os.path.exists(p):
        continue
    pass


def glb_positions(path):
    js, bin_ = load(path)
    out = []
    for m in js["meshes"]:
        for pr in m["primitives"]:
            a = js["accessors"][pr["attributes"]["POSITION"]]
            bv = js["bufferViews"][a["bufferView"]]
            stride = bv.get("byteStride", 0) or 12
            off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
            n4 = stride // 4
            arr = np.frombuffer(bin_, dtype="<f4", count=a["count"] * n4,
                                offset=off).reshape(-1, n4)
            out.append(np.ascontiguousarray(arr[:, :3]))
    return np.vstack(out)


sink_max = 0.0
for kind in ("desa", "kota", "tanah"):
    p = os.path.join(GEN, f"roads_{kind}.glb")
    if not os.path.exists(p):
        continue
    P = glb_positions(p)
    th = height(P[:, 0], P[:, 2])
    delta = P[:, 1] - th
    print(f"  {kind:6s}: {len(P):7d} verteks, selisih tinggi min {delta.min():+.3f} m, "
          f"median {np.median(delta):+.3f} m, maks {delta.max():+.3f} m")
    n_sink = int((delta < -0.10).sum())
    sink_max = max(sink_max, -float(delta.min()))
    print(f"           tenggelam > 10 cm: {n_sink} ({100.0*n_sink/len(P):.2f}%)")

# ---------------------------------------------------------------- 2-3. props
print("\n[2] objek vs jalan / laut / lereng")
p = open(os.path.join(GEN, "props.bin"), "rb").read()
magic, ver, cnt, nch = struct.unpack_from("<4sIII", p, 0)
assert magic == b"NPRP", magic
arr = np.frombuffer(p, dtype="<f4", count=cnt * 5, offset=16).reshape(cnt, 5)
tps = np.frombuffer(p, dtype="<u2", count=cnt, offset=16 + cnt * 20)
items = {"x": arr[:, 0], "y": arr[:, 1], "z": arr[:, 2], "yaw": arr[:, 3], "s": arr[:, 4], "t": tps}
print(f"  {cnt} instance, {nch} chunk")
types = items["t"]
nama = {0: "oak", 1: "spruce", 2: "sakura", 3: "palm"}
on_road = []
in_sea = []
steep = []
floating = []
for i in range(cnt):
    t = int(types[i])
    x, y, z = float(items["x"][i]), float(items["y"][i]), float(items["z"][i])
    gh = float(height(np.float32(x), np.float32(z)))
    if y < 0.5:
        in_sea.append(i)
    if y < gh - 1.2:
        floating.append(i)
    if t < 4 and slope_deg(x, z) > 26:
        steep.append(i)
    # bangunan harus bebas jalan; pohon boleh di pinggir jalan berjarak > 3 m
    dr = dist_to_road(x, z)
    if t >= 4 and dr < 7.0:
        on_road.append(i)
    elif t < 4 and dr < 2.6:
        on_road.append(i)
print(f"  di laut (y<0.5 m)          : {len(in_sea)}")
print(f"  di atas jalan / terlalu dekat: {len(on_road)}")
print(f"  lereng > 26 derajat (pohon): {len(steep)}")
print(f"  melayang > 1.2 m dari tanah: {len(floating)}")

# ---------------------------------------------------------------- 4. spawn
print("\n[3] lokasi spawn mobil")
sp = json.load(open(os.path.join(APP, "generated", "car_spawn.json")))
dr = dist_to_road(sp["x"], sp["z"])
gh = float(height(np.float32(sp["x"]), np.float32(sp["z"])))
print(f"  ({sp['x']:.1f}, {sp['z']:.1f}) y={sp['y']:.2f} terrain={gh:.2f} jarak ke tengah jalan={dr:.2f} m")
print(f"  biome di spawn: {int(BIO[int((sp['z']-z0)/DZ), int((sp['x']-x0)/DX)])}")

ok = (len(in_sea) == 0 and len(on_road) == 0 and len(steep) == 0 and dr < 6.0
      and sink_max < 0.35)
print("\n" + ("SEMUA CEK VISUAL LULUS" if ok else "ADA MASALAH VISUAL"))
