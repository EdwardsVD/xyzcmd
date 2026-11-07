#!/usr/bin/env python3
"""fix_scene.py - perbaikan akhir tata letak (dijalankan SETELAH build_world.py).

Kenapa perlu: build_world.py meratakan tanah (pad) di bawah bangunan, sehingga
heightmap akhir berbeda dari saat jalan dibuat -> jalan bisa tertimbun sampai
beberapa meter. Skrip ini:

  1. Menyesuaikan tinggi verteks jalan di roads_*.glb + roads.json ke heightmap
     FINAL (terrain + 6 cm), supaya jalan selalu menempel di permukaan.
  2. Membuang instance objek yang tidak valid: di laut (y < 1.0 m), terlalu dekat
     jalan (gedung < 7.5 m, pohon < 2.6 m dari tepi), atau di lereng > 26 derajat.
  3. Menulis ulang props.bin (dengan pengelompokan chunk yang benar).
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

# ------------------------------------------------------------------ terrain final
_d = open(os.path.join(GEN, "island.bin"), "rb").read()
magic, ver, N, x0, x1, z0, z1, _m1, _m2 = struct.unpack_from("<4sII6f", _d, 0)
assert magic == b"NISA", magic
H = np.frombuffer(_d, dtype="<f4", count=N * N, offset=36).reshape(N, N).astype(np.float64)
OFF_COL = 36 + N * N * 4
OFF_BIO = OFF_COL + N * N * 3
BIO = np.frombuffer(_d, dtype=np.uint8, count=N * N, offset=OFF_BIO).reshape(N, N)


def height(x, z):
    fx = np.clip((np.asarray(x, np.float64) - x0) / (x1 - x0) * (N - 1), 0, N - 1.001)
    fz = np.clip((np.asarray(z, np.float64) - z0) / (z1 - z0) * (N - 1), 0, N - 1.001)
    i = np.floor(fx).astype(int)
    j = np.floor(fz).astype(int)
    tx, tz = fx - i, fz - j
    return (H[j, i] * (1 - tx) * (1 - tz) + H[j, i + 1] * tx * (1 - tz)
            + H[j + 1, i] * (1 - tx) * tz + H[j + 1, i + 1] * tx * tz)


def slope_deg(x, z):
    e = 8.0
    hx = (height(x + e, z) - height(x - e, z)) / (2 * e)
    hz = (height(x, z + e) - height(x, z - e)) / (2 * e)
    return np.degrees(np.arctan(np.hypot(hx, hz)))


# ------------------------------------------------------------------ 1. jalan menempel
LIFT = 0.06
print("== 1. menempelkan jalan ke terrain final")
for kind in ("desa", "kota", "tanah"):
    p = os.path.join(GEN, f"roads_{kind}.glb")
    if not os.path.exists(p):
        continue
    js, bin_ = load(p)
    data = bytearray(bin_)
    n_adj = 0
    worst = 0.0
    for m in js["meshes"]:
        for pr in m["primitives"]:
            a = js["accessors"][pr["attributes"]["POSITION"]]
            bv = js["bufferViews"][a["bufferView"]]
            stride = bv.get("byteStride", 0) or 12
            base = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
            n4 = stride // 4
            arr = np.frombuffer(bin_, dtype="<f4", count=a["count"] * n4, offset=base).reshape(-1, n4)
            xs, zs = arr[:, 0].astype(np.float64), arr[:, 2].astype(np.float64)
            ys = height(xs, zs) + LIFT
            worst = max(worst, float(np.abs(arr[:, 1] - ys).max()))
            for k in range(a["count"]):
                struct.pack_into("<f", data, base + k * stride + 4, float(ys[k]))
            n_adj += a["count"]
    # tulis ulang chunk BIN di glb (panjang sama, hanya isi berubah)
    out = bytearray(open(p, "rb").read())
    # cari awal chunk BIN: 12 + 8 + jsonLen + 8
    jlen = struct.unpack_from("<I", out, 12)[0]
    bin_start = 20 + jlen + 8
    out[bin_start:bin_start + len(data)] = data
    open(p, "wb").write(bytes(out))
    print(f"   {kind:6s}: {n_adj} verteks disesuaikan (selisih maks sebelumnya {worst:.2f} m)")

# roads.json: tinggi titik tengah ikut terrain (dipakai grip/spawn)
rp = os.path.join(GEN, "roads.json")
roads = json.load(open(rp))
npts = 0
for path in roads["jalan"]:
    pts = path["titik"]
    xs = np.array([q[0] for q in pts])
    zs = np.array([q[1] for q in pts])
    ys = height(xs, zs) + LIFT
    for k, q in enumerate(pts):
        q[2] = round(float(ys[k]), 2)
    npts += len(pts)
roads["meta"]["y_offset"] = LIFT
json.dump(roads, open(rp, "w"), separators=(",", ":"))
print(f"   roads.json: {npts} titik tinggi diperbarui")

# ------------------------------------------------------------------ 2. index jalan untuk filter
segs = []
for path in roads["jalan"]:
    pts = path["titik"]
    for a, b in zip(pts, pts[1:]):
        segs.append((a[0], a[1], b[0], b[1]))
from roaddist import RoadDist  # noqa: E402

dist_road = RoadDist(segs)


# ------------------------------------------------------------------ 3. bersihkan props
print("\n== 2. membersihkan objek tidak valid")
pb = open(os.path.join(GEN, "props.bin"), "rb").read()
_magic, _ver, cnt, nch = struct.unpack_from("<4sIII", pb, 0)
arr = np.frombuffer(pb, dtype="<f4", count=cnt * 5, offset=16).reshape(cnt, 5).copy()
tps = np.frombuffer(pb, dtype="<u2", count=cnt, offset=16 + cnt * 20).copy()

xs, ys, zs = arr[:, 0].astype(np.float64), arr[:, 1].astype(np.float64), arr[:, 2].astype(np.float64)
gh = height(xs, zs)
keep = np.ones(cnt, bool)
n_sea = n_road = n_steep = n_slope = 0

# laut / terrain turun di bawah 1 m
mask = (gh < 1.2) | (ys < 1.0)
n_sea = int(mask.sum()); keep &= ~mask
# lereng terlalu tajam untuk pohon
sl = slope_deg(xs, zs)
mask = (tps < 4) & (sl > 26.0)
n_slope = int(mask.sum()); keep &= ~mask
# jarak ke jalan
need = np.ones(cnt, bool)
for i in np.nonzero(keep)[0]:
    lim = 2.8 if tps[i] < 4 else 9.0
    if dist_road(float(xs[i]), float(zs[i])) < lim:
        need[i] = False
n_road = int((keep & ~need).sum()); keep &= need

# pohon yang tumbuh menembus bangunan (jarak < 8 m dari gedung) dibuang
bld = np.nonzero((tps >= 4) & keep)[0]
if len(bld):
    from collections import defaultdict
    bg = defaultdict(list)
    for i in bld:
        bg[(int(xs[i] // 40), int(zs[i] // 40))].append(i)
    kill = np.zeros(cnt, bool)
    for i in np.nonzero((tps < 4) & keep)[0]:
        gx, gz = int(xs[i] // 40), int(zs[i] // 40)
        for dx in (-1, 0, 1):
            for dz in (-1, 0, 1):
                for j in bg.get((gx + dx, gz + dz), ()):
                    if abs(xs[j] - xs[i]) < 9 and abs(zs[j] - zs[i]) < 9:
                        kill[i] = True
    n_steep = int(kill.sum())
    keep &= ~kill

print(f"   dibuang: {n_sea} di laut, {n_road} di/terlalu dekat jalan, "
      f"{n_slope} di lereng tajam, {n_steep} menembus bangunan")
print(f"   tersisa {int(keep.sum())} dari {cnt} instance")

arr = arr[keep]
tps = tps[keep]
# y selalu tepat di permukaan terrain (rumah/gedung memakai pad datar)
arr[:, 1] = height(arr[:, 0].astype(np.float64), arr[:, 2].astype(np.float64)).astype(np.float32)

# kelompokkan per chunk 200 m
cx = ((arr[:, 0] - x0) / 200.0).astype(np.int32)
cz = ((arr[:, 2] - z0) / 200.0).astype(np.int32)
cidx = cz * 1000 + cx
order = np.argsort(cidx, kind="stable")
arr, tps, cidx = arr[order], tps[order], cidx[order]
cid, start, count = np.unique(cidx, return_index=True, return_counts=True)
with open(os.path.join(GEN, "props.bin"), "wb") as f:
    f.write(struct.pack("<4sIII", b"NPRP", 1, len(arr), len(cid)))
    f.write(arr.astype("<f4").tobytes())
    f.write(tps.astype("<u2").tobytes())
    f.write(cid.astype("<i4").tobytes())
    f.write(start.astype("<i4").tobytes())
    f.write(count.astype("<i4").tobytes())
print(f"   props.bin: {len(arr)} instance, {len(cid)} chunk, "
      f"{os.path.getsize(os.path.join(GEN, 'props.bin'))/1e6:.2f} MB")
# salin hasil perbaikan ke paket runtime (app/assets) agar tidak tertimpa
import shutil
_rt = os.path.join(ROOT, "app", "assets", "generated")
os.makedirs(_rt, exist_ok=True)
for f in ("props.bin", "roads.json"):
    shutil.copyfile(os.path.join(GEN, f), os.path.join(_rt, f))
print("   disalin ke app/assets/generated")

# ------------------------------------------------------------------ 4. spawn mobil
sp = os.path.join(GEN, "car_spawn.json")
best = None
for path in roads["jalan"]:
    if path["jenis"] != "kota":
        continue
    cx0 = sum(q[0] for q in path["titik"]) / len(path["titik"])
    cz0 = sum(q[1] for q in path["titik"]) / len(path["titik"])
    d = math.hypot(cx0 + 1393, cz0 - 1309)     # dekat pusat kota
    if best is None or d < best[0]:
        best = (d, path)
path = best[1]
q = path["titik"][len(path["titik"]) // 2]
q2 = path["titik"][len(path["titik"]) // 2 + 1]
yaw = math.atan2(q2[0] - q[0], q2[1] - q[1])
spawn = dict(x=round(q[0], 2), y=round(float(height(q[0], q[1])) + LIFT, 3), z=round(q[1], 2),
             yaw=round(yaw, 4))
json.dump(spawn, open(sp, "w"))
# salin juga ke paket runtime supaya pack_assets.py tidak menimpanya
out_dir = os.path.join(ROOT, "app", "assets", "generated")
os.makedirs(out_dir, exist_ok=True)
json.dump(spawn, open(os.path.join(out_dir, "car_spawn.json"), "w"))
print(f"\n== 3. spawn mobil: {spawn}")
