#!/usr/bin/env python3
"""build_world.py - penataan objek (M3) untuk Nusa Rush.

- Baca heightmap raw (tools/_h.npy dari extract_island.py) + biome + roads.json
- Sebar pohon dengan kepadatan per biome (hutan 100% acuan, desa ~50%, kota ~10%,
  gunung ~30%, pantai palem jarang) memakai grid-jitter (setara Poisson-disk, seed tetap)
- Rumah desa berkelompok di sepanjang jalan desa; gedung kota di sepanjang jalan kota,
  makin ke pusat kota makin tinggi (tipe 2 = pencakar langit 160-184 m)
- Ratakan terrain (pad) di bawah bangunan, blend halus
- Tulis: assets/generated/island.bin (final, terrain sudah diratakan) + props.bin
- Render validasi: output/preview/world_top.png
"""
import json
import math
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_roads import Terrain, GEN, PREV  # noqa: E402

SEED = 20261106
rng = np.random.default_rng(SEED)


def reseed(tag):
    """Seed per tahap: script ini dijalankan dua kali (sebelum & sesudah jalan
    dibuat) dan hasilnya harus identik. Tanpa ini, pemakaian rng bersama membuat
    pohon/gedung bergeser setiap kali urutan tahap berubah."""
    global rng
    off = 0
    for ch in tag:                      # hash stabil (hash() berubah antar proses)
        off = (off * 131 + ord(ch)) % 100000
    rng = np.random.default_rng(SEED + off)

# tipe objek: 0 oak,1 spruce,2 sakura,3 palm, 4..15 rumah desa A1..D3, 16..27 gedung kota A1..D3
TYPE_TREE_OAK, TYPE_TREE_SPRUCE, TYPE_TREE_SAKURA, TYPE_TREE_PALM = 0, 1, 2, 3
TYPE_DESA0, TYPE_KOTA0 = 4, 16

# kepadatan: satu objek per luas (m2). 0 = tidak ada
DENSITY = {
    4: 48.0,      # hutan: ~2,5x lebih rapat (satu pohon per 48 m2)
    3: 210.0,     # desa
    2: 1200.0,    # kota 10% (hanya tepi jalan & taman)
    5: 320.0,     # dataran tinggi
    1: 1500.0,    # pantai: palem jarang
    0: 0.0,
    6: 0.0,       # danau
}

# campuran jenis pohon per biome (bobot)
MIX = {
    4: [(TYPE_TREE_OAK, 0.55), (TYPE_TREE_SPRUCE, 0.45)],
    3: [(TYPE_TREE_OAK, 0.5), (TYPE_TREE_SPRUCE, 0.2), (TYPE_TREE_SAKURA, 0.3)],
    2: [(TYPE_TREE_OAK, 0.7), (TYPE_TREE_SAKURA, 0.3)],
    5: [(TYPE_TREE_SPRUCE, 0.65), (TYPE_TREE_OAK, 0.35)],
    1: [(TYPE_TREE_PALM, 1.0)],
}


class World:
    def __init__(self):
        self.t = Terrain()
        with open(os.path.join(GEN, "island.bin"), "rb") as f:
            hdr = f.read(36)
            magic, ver, N, x0, x1, z0, z1, _a, _b = struct.unpack("<4sII6f", hdr)
        self.N, self.x0, self.x1, self.z0, self.z1 = N, x0, x1, z0, z1
        self.H0 = np.load(os.path.join(GEN, "_h.npy")).astype(np.float64)   # raw
        self.H = self.H0.copy()
        self.bio = np.load(os.path.join(GEN, "_b.npy")).astype(np.uint8)
        self.roads = json.load(open(os.path.join(GEN, "roads.json")))
        self.dx = (x1 - x0) / (N - 1)
        self.dz = (z1 - z0) / (N - 1)
        # peta jarak ke jalan (untuk larangan pohon di jalan) + penanda kasar
        self.road_grid = np.zeros((N, N), np.uint8)
        self.road_d = np.full((N, N), 999.0, np.float32)
        self.raster_roads()
        self.slope = self.compute_slope(self.H0)

    # ---------------------------------------------------------------- util
    def raster_roads(self, extra_cells=2):
        N, dx = self.N, self.dx
        for j in self.roads["jalan"]:
            w = j["lebar"] * 0.5 + 3.0
            for k in range(len(j["titik"]) - 1):
                a = j["titik"][k]; b = j["titik"][k + 1]
                x0, z0 = a[0], a[1]; x1, z1 = b[0], b[1]
                i0 = int((x0 - self.x0) / self.dx); i1 = int((x1 - self.x0) / self.dx)
                j0 = int((z0 - self.z0) / self.dz); j1 = int((z1 - self.z0) / self.dz)
                r = int(math.ceil(w / self.dx)) + extra_cells
                for jj in range(max(0, min(j0, j1) - r), min(N, max(j0, j1) + r + 1)):
                    for ii in range(max(0, min(i0, i1) - r), min(N, max(i0, i1) + r + 1)):
                        px = self.x0 + ii * self.dx; pz = self.z0 + jj * self.dz
                        # jarak titik ke segmen
                        vx, vz = x1 - x0, z1 - z0
                        L2 = vx * vx + vz * vz
                        if L2 < 1e-9:
                            t = 0.0
                        else:
                            t = max(0.0, min(1.0, ((px - x0) * vx + (pz - z0) * vz) / L2))
                        ddx = px - (x0 + t * vx); ddz = pz - (z0 + t * vz)
                        d2 = ddx * ddx + ddz * ddz
                        if d2 <= w * w:
                            self.road_grid[jj, ii] = 1
                        # jarak presisi ke tepi jalan (badan + trotoar)
                        edge = math.sqrt(d2) - w
                        if edge < self.road_d[jj, ii]:
                            self.road_d[jj, ii] = edge

    def compute_slope(self, H):
        gy, gx = np.gradient(H, self.dz, self.dx)
        return np.degrees(np.arctan(np.hypot(gx, gy)))

    def height(self, x, z):
        fx = (x - self.x0) / self.dx; fz = (z - self.z0) / self.dz
        fx = np.clip(fx, 0, self.N - 1.001); fz = np.clip(fz, 0, self.N - 1.001)
        i0 = np.floor(fx).astype(int); j0 = np.floor(fz).astype(int)
        tx = fx - i0; tz = fz - j0
        H = self.H
        return (H[j0, i0] * (1 - tx) * (1 - tz) + H[j0, i0 + 1] * tx * (1 - tz)
                + H[j0 + 1, i0] * (1 - tx) * tz + H[j0 + 1, i0 + 1] * tx * tz)

    def cell(self, x, z):
        i = int(np.clip((x - self.x0) / self.dx, 0, self.N - 1))
        j = int(np.clip((z - self.z0) / self.dz, 0, self.N - 1))
        return j, i

    def valid_spot(self, x, z, max_slope=22.0, min_h=1.2, edge_clear=3.0):
        j, i = self.cell(x, z)
        if self.bio[j, i] in (0, 6):
            return False
        if self.H0[j, i] < min_h:
            return False
        if self.slope[j, i] > max_slope:
            return False
        # uji jarak presisi: grid 37,5 m terlalu kasar untuk tepi jalan
        if self.road_d[j, i] < edge_clear:
            return False
        return True

    # ------------------------------------------------------- flatten (pad bangunan)
    def flatten(self, x, z, radius, blend, y=None):
        j, i = self.cell(x, z)
        y = self.H[j, i] if y is None else y
        rr = radius + blend
        ci = int(math.ceil(rr / self.dx)) + 1
        j0, j1 = max(0, j - ci), min(self.N, j + ci + 1)
        i0, i1 = max(0, i - ci), min(self.N, i + ci + 1)
        zz = self.z0 + np.arange(j0, j1) * self.dz
        xx = self.x0 + np.arange(i0, i1) * self.dx
        ZZ, XX = np.meshgrid(zz, xx, indexing="ij")
        d = np.hypot(XX - x, ZZ - z)
        w = np.clip((rr - d) / blend, 0, 1)
        sub = self.H[j0:j1, i0:i1]
        self.H[j0:j1, i0:i1] = sub * (1 - w) + y * w

    # -------------------------------------------------------------- penempatan
    def place_trees(self):
        reseed("trees")
        out = []
        N = self.N
        dxs = (self.x1 - self.x0) / N
        # iterasi per sel grid biome, sebar titik jitter di dalam sel
        for k, dens in DENSITY.items():
            if dens <= 0:
                continue
            m = self.bio == k
            jj, ii = np.nonzero(m)
            if len(jj) == 0:
                continue
            cell_area = self.dx * self.dz
            per_cell = cell_area / dens
            n_per = int(per_cell)
            frac = per_cell - n_per
            counts = np.full(len(jj), n_per, np.int32)
            counts += (rng.random(len(jj)) < frac).astype(np.int32)
            tot = int(counts.sum())
            print(f"  biome {k}: {len(jj)} sel, {tot} pohon")
            for (jjj, iii, c) in zip(jj, ii, counts):
                if c <= 0:
                    continue
                xs = self.x0 + (iii + rng.random(c)) * self.dx
                zs = self.z0 + (jjj + rng.random(c)) * self.dz
                for x, z in zip(xs, zs):
                    if not self.valid_spot(x, z):
                        continue
                    mix = MIX.get(k, [(TYPE_TREE_OAK, 1.0)])
                    tp = mix[int(rng.integers(0, len(mix)))][0]
                    out.append((x, z, tp, float(rng.uniform(0, 2 * math.pi)),
                                float(rng.uniform(0.8, 1.35))))
        return out

    def road_points(self, kinds, step_m=8.0):
        """titik sampel sepanjang jalan (x, z, tangent, jenis)."""
        pts = []
        for j in self.roads["jalan"]:
            if j["jenis"] not in kinds:
                continue
            tk = np.array(j["titik"], np.float64)
            for k in range(len(tk) - 1):
                a, b = tk[k], tk[k + 1]
                seg = np.hypot(b[0] - a[0], b[1] - a[1])
                n = max(1, int(seg / step_m))
                for t in range(n):
                    f = t / n
                    x = a[0] + (b[0] - a[0]) * f; z = a[1] + (b[1] - a[1]) * f
                    tx, tz = b[0] - a[0], b[1] - a[1]
                    L = math.hypot(tx, tz) + 1e-9
                    pts.append((x, z, tx / L, tz / L, j["lebar"]))
        return pts

    def place_buildings(self):
        reseed("buildings")
        buildings = []
        occupied = []

        def far_enough(x, z, d):
            for (ox, oz) in occupied[-4000:]:
                if abs(ox - x) < d and abs(oz - z) < d:
                    return False
            return True

        # ---- rumah desa: berkelompok di sepanjang jalan desa
        pts = self.road_points(["desa"], 8.0)
        rng.shuffle(pts)
        cluster_left = 0
        for (x, z, tx, tz, w) in pts:
            if cluster_left <= 0:
                if rng.random() > 0.35:
                    continue
                cluster_left = int(rng.integers(3, 7))
            cluster_left -= 1
            side = 1.0 if rng.random() < 0.5 else -1.0
            off = w * 0.5 + 9.0 + rng.uniform(0, 6)
            bx = x + (-tz) * off * side; bz = z + (tx) * off * side
            if not self.valid_spot(bx, bz, max_slope=12.0, edge_clear=4.5):
                continue
            if not far_enough(bx, bz, 14.0):
                continue
            j, i = self.cell(bx, bz)
            if self.road_grid[j, i]:
                continue
            tp = TYPE_DESA0 + int(rng.integers(0, 12))
            yaw = math.atan2(-tz * side, tx * side) + math.pi  # pintu menghadap jalan (+Z lokal)
            yaw = math.atan2(bx - x, bz - z)
            buildings.append((bx, bz, tp, yaw, float(rng.uniform(0.95, 1.08))))
            occupied.append((bx, bz))
            self.flatten(bx, bz, 9.0, 12.0)

        print(f"  rumah desa: {len(buildings)}")

        # ---- gedung kota: di sepanjang jalan kota, tinggi menurun dari pusat kota
        city_center = np.array([-1350.0, 1250.0])
        n_city = len(buildings)
        pts = self.road_points(["kota"], 10.0)
        rng.shuffle(pts)
        for (x, z, tx, tz, w) in pts:
            for side in (1.0, -1.0):
                if rng.random() > 0.55:
                    continue
                off = w * 0.5 + 2.0 + 10.0 + rng.uniform(0, 8)
                bx = x + (-tz) * off * side; bz = z + (tx) * off * side
                if not self.valid_spot(bx, bz, max_slope=10.0, edge_clear=4.5):
                    continue
                if not far_enough(bx, bz, 26.0):
                    continue
                d = math.hypot(bx - city_center[0], bz - city_center[1])
                # tipe 2 = pencakar langit di pusat (<600 m), tipe 3 menengah, tipe 1 rendah
                if d < 550:
                    tier = 2 if rng.random() < 0.85 else 3
                elif d < 1100:
                    tier = 2 if rng.random() < 0.25 else 3
                else:
                    tier = 1 if rng.random() < 0.8 else 3
                tp = TYPE_KOTA0 + (tier - 1)
                yaw = math.atan2(bx - x, bz - z)
                buildings.append((bx, bz, tp, yaw, float(rng.uniform(0.95, 1.1))))
                occupied.append((bx, bz))
                self.flatten(bx, bz, 14.0, 16.0)
        print(f"  gedung kota: {len(buildings) - n_city}")

        # ---- taman kota: sedikit pohon di dalam blok grid
        return buildings


def main():
    w = World()
    print("penataan objek...")
    trees = w.place_trees()
    buildings = w.place_buildings()
    w.roadd_slope_check = None

    # ---- tulis island.bin final (H sudah diratakan di bawah bangunan)
    col = w.t.col if hasattr(w.t, "col") else None
    with open(os.path.join(GEN, "island.bin"), "rb") as f:
        raw = f.read()
    hdr = raw[:36]
    n = w.N * w.N
    colbytes = raw[36 + n * 4: 36 + n * 4 + n * 3]
    biobytes = raw[36 + n * 4 + n * 3:]
    with open(os.path.join(GEN, "island.bin"), "wb") as f:
        f.write(hdr)
        f.write(w.H.astype("<f4").tobytes())
        f.write(colbytes)
        f.write(biobytes)
    print("  island.bin diperbarui (pad bangunan diratakan)")

    # ---- props.bin: header + instance (x,y,z,yaw,scale,type)
    inst = []
    for (x, z, tp, yaw, sc) in trees:
        y = float(w.height(x, z))
        inst.append((x, y, z, yaw, sc, tp))
    for (x, z, tp, yaw, sc) in buildings:
        y = float(w.H[w.cell(x, z)])
        inst.append((x, y, z, yaw, sc, tp))
    arr = np.array(inst, np.float32)
    types = np.array([t[5] for t in inst], np.uint16)
    # urutkan per chunk 200 m agar streaming mudah
    cx = ((arr[:, 0] - w.x0) / 200.0).astype(np.int32)
    cz = ((arr[:, 2] - w.z0) / 200.0).astype(np.int32)
    cidx = cz * 1000 + cx
    order = np.argsort(cidx, kind="stable")
    arr, types, cidx = arr[order], types[order], cidx[order]
    cid, start, count = np.unique(cidx, return_index=True, return_counts=True)
    with open(os.path.join(GEN, "props.bin"), "wb") as f:
        f.write(struct.pack("<4sIII", b"NPRP", 1, len(arr), len(cid)))
        f.write(arr[:, :5].astype("<f4").tobytes())
        f.write(types.astype("<u2").tobytes())
        f.write(cid.astype("<i4").tobytes())
        f.write(start.astype("<i4").tobytes())
        f.write(count.astype("<i4").tobytes())
    print(f"  props.bin: {len(arr)} instance, {len(cid)} chunk, "
          f"{os.path.getsize(os.path.join(GEN,'props.bin'))/1e6:.2f} MB")

    # ---- render validasi
    W = 900
    bi = np.clip((np.arange(W) * w.N // W), 0, w.N - 1)
    bio = w.bio[bi][:, bi]
    hh = w.H[bi][:, bi]
    shade = 0.55 + 0.55 * (hh - hh.min()) / (hh.max() - hh.min())
    palette = {0: (10, 26, 44), 1: (238, 224, 182), 2: (128, 130, 126), 3: (178, 196, 96),
               4: (34, 80, 42), 5: (116, 110, 60), 6: (34, 98, 132)}
    colr = np.zeros((W, W, 3), np.float64)
    for k, c in palette.items():
        m = bio == k
        colr[m] = np.array(c) * shade[m][:, None]
    img = Image.fromarray(np.clip(colr, 0, 255).astype(np.uint8))
    dr = ImageDraw.Draw(img)

    def tx(x, z):
        return ((x - w.x0) / (w.x1 - w.x0) * W, (z - w.z0) / (w.z1 - w.z0) * W)

    for j in w.roads["jalan"]:
        dr.line([tx(p[0], p[1]) for p in j["titik"]],
                fill=dict(kota=(255, 90, 60), desa=(255, 220, 90), tanah=(180, 120, 60))[j["jenis"]],
                width=2)
    for (x, y, z, yaw, sc, tp) in inst:
        if tp < 4:
            dr.ellipse([tx(x, z)[0] - 1, tx(x, z)[1] - 1, tx(x, z)[0] + 1, tx(x, z)[1] + 1],
                       fill=(20, 90, 30) if tp != 3 else (40, 130, 60))
        else:
            px, py = tx(x, z)
            dr.rectangle([px - 2, py - 2, px + 2, py + 2],
                         fill=(230, 230, 230) if tp >= TYPE_KOTA0 else (200, 60, 60))
    dr.text((8, 8), f"pohon+bangunan: {len(arr)} instance, {len(cid)} chunk", fill=(255, 255, 255))
    img.save(os.path.join(PREV, "world_top.png"))
    print("  render:", os.path.join(PREV, "world_top.png"))

    # statistik ringkas
    tcount = {}
    for (x, y, z, yaw, sc, tp) in inst:
        tcount[int(tp)] = tcount.get(int(tp), 0) + 1
    print("  jumlah per tipe:", dict(sorted(tcount.items())))


if __name__ == "__main__":
    main()
