#!/usr/bin/env python3
"""build_roads.py - membangun 3 jenis jalan Nusa Rush menempel di permukaan pulau.

Pengganti Blender headless (blender.org diblokir di sandbox, lihat DECISIONS.md D2).
Input : assets/generated/island.bin (heightmap+b biome) -> output tools/_h.npy/_b.npy
Output: assets/generated/roads_desa.glb, roads_kota.glb, roads_tanah.glb, roads.json
        output/preview/roads_top.png (render atas untuk validasi)

Aturan:
- tidak ada jalan di laut / di bawah y=0,5 m
- kemiringan maksimum 12% (koridor pegunungan 10%)
- lebar: desa 6 m, kota 12-14 m (+trotoar 2 m/sisi), tanah 4-5 m
- mesh dibagi per segmen <= 200 m, tekstur prosedural 512 px tertanam di glb,
  UV memanjang (v = jarak/8 m), offset +0,05 m di atas terrain.
"""
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from glbwrite import GLBScene, Mesh, png_bytes, probe  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(ROOT, "assets", "generated")
PREV = os.path.join(ROOT, "output", "preview")
os.makedirs(PREV, exist_ok=True)

# ------------------------------------------------------------------ terrain sampler
class Terrain:
    def __init__(self):
        with open(os.path.join(GEN, "island.bin"), "rb") as f:
            hdr = f.read(36)
            magic, ver, N, x0, x1, z0, z1, _a, _b = __import__("struct").unpack("<4sII6f", hdr)
            assert magic == b"NISA", magic
            self.N, self.x0, self.x1, self.z0, self.z1 = N, x0, x1, z0, z1
            n = N * N
            self.H = np.frombuffer(f.read(n * 4), dtype="<f4").reshape(N, N).astype(np.float64)
            self.col = np.frombuffer(f.read(n * 3), dtype=np.uint8).reshape(N, N, 3)
            self.bio = np.frombuffer(f.read(n), dtype=np.uint8).reshape(N, N)

    def gi(self, x):
        return (x - self.x0) / (self.x1 - self.x0) * (self.N - 1)

    def gj(self, z):
        return (z - self.z0) / (self.z1 - self.z0) * (self.N - 1)

    def height(self, x, z):
        fx, fz = self.gi(np.asarray(x)), self.gj(np.asarray(z))
        fx = np.clip(fx, 0, self.N - 1.001)
        fz = np.clip(fz, 0, self.N - 1.001)
        i0 = np.floor(fx).astype(int); j0 = np.floor(fz).astype(int)
        tx = fx - i0; tz = fz - j0
        H = self.H
        h = (H[j0, i0] * (1 - tx) * (1 - tz) + H[j0, i0 + 1] * tx * (1 - tz)
             + H[j0 + 1, i0] * (1 - tx) * tz + H[j0 + 1, i0 + 1] * tx * tz)
        return h

    def biome(self, x, z):
        i = np.clip(np.rint(self.gi(x)).astype(int), 0, self.N - 1)
        j = np.clip(np.rint(self.gj(z)).astype(int), 0, self.N - 1)
        return self.bio[j, i]


T = Terrain()

# ------------------------------------------------------------------ tekstur prosedural
def tex_desa(w=1024):
    rng = np.random.default_rng(11)
    base = np.zeros((w, w, 3), np.float32); base[:] = (62, 62, 66)
    n = rng.normal(0, 9, (w, w, 1))
    img = np.clip(base + n, 0, 255)
    # bintik agregat
    for _ in range(int(900 * (w / 512) ** 2)):        # bintik agregat sebanding luas
        y, x = rng.integers(0, w, 2); r = rng.integers(1, 3 + w // 256)
        img[y:y + r, x:x + r] = np.clip(img[y:y + r, x:x + r] + rng.normal(18, 6), 0, 255)
    im = Image.fromarray(img.astype(np.uint8))
    dr = ImageDraw.Draw(im)
    # garis tengah putus-putus pudar (U memanjang, V melintang: v=0 kiri, v=1 kanan)
    for y in range(-w, w, w // 16):
        dr.rectangle([w * 0.5 - w // 85, y, w * 0.5 + w // 85, y + w // 27], fill=(196, 190, 168))
    im = im.filter(ImageFilter.GaussianBlur(0.8))
    # tambah bahu kerikil di tepi (u < 0.12 / > 0.88)
    a = np.asarray(im).astype(np.float32)
    rng2 = np.random.default_rng(3)
    edge = rng2.normal(0, 14, (w, w, 1))
    m = np.zeros((w, 1, 1)); m[:int(w * 0.10)] = 1; m[int(w * 0.90):] = 1
    a = a * (1 - m * 0.55) + (np.array([110, 104, 88]) + edge) * m * 0.55
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))


def tex_kota(w=1024):
    rng = np.random.default_rng(7)
    a = np.zeros((w, w, 3), np.float32); a[:] = (52, 52, 55)
    a += rng.normal(0, 7, (w, w, 1))
    im = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))
    dr = ImageDraw.Draw(im)
    # marka: garis tepi + garis tengah ganda/tirus (U memanjang sepanjang jalan)
    dr.rectangle([w * 0.06, 0, w * 0.10, w], fill=(224, 224, 218))     # tepi kiri (trotoar side)
    dr.rectangle([w * 0.90, 0, w * 0.94, w], fill=(224, 224, 218))
    dr.rectangle([w * 0.485, 0, w * 0.497, w], fill=(226, 220, 200))   # tengah
    dr.rectangle([w * 0.503, 0, w * 0.515, w], fill=(226, 220, 200))
    for lane_u in (0.30, 0.70):                                        # garis putus lajur
        for y in range(0, w, w // 10):
            dr.rectangle([w * lane_u - w // 128, y, w * lane_u + w // 128, y + w // 19],
                         fill=(220, 216, 200))
    # trotoar (abu terang) di kedua tepi luar
    sx = int(w * 0.10)
    im2 = np.asarray(im).astype(np.float32)
    im2[:, :sx] = (150, 148, 142); im2[:, -sx:] = (150, 148, 142)
    im2[:, :sx] += rng.normal(0, 8, (w, sx, 1)); im2[:, -sx:] += rng.normal(0, 8, (w, sx, 1))
    return Image.fromarray(np.clip(im2, 0, 255).astype(np.uint8))


def tex_beton(w=1024):
    """Tekstur trotoar: ubin beton 60x60 cm dengan nat, licin dan terang."""
    rng = np.random.default_rng(23)
    a = np.zeros((w, w, 3), np.float32); a[:] = (150, 148, 143)
    a += rng.normal(0, 7, (w, w, 1))
    # butiran agregat halus
    for _ in range(int(1400 * (w / 512) ** 2)):
        y, x = rng.integers(0, w, 2)
        r = rng.integers(1, 2 + w // 512)
        a[y:y + r, x:x + r] += rng.normal(14, 5)
    # nat ubin (grid 1/4 lebar -> ubin ~1.5 m kalau tekstur diulang tiap 6 m)
    line = max(2, w // 256)
    for k in range(0, w, w // 4):
        a[k:k + line, :, :] *= 0.72
        a[:, k:k + line, :] *= 0.72
    im = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))
    return im.filter(ImageFilter.GaussianBlur(0.6))


def tex_tanah(w=1024):
    rng = np.random.default_rng(5)
    a = np.zeros((w, w, 3), np.float32); a[:] = (96, 74, 50)
    for oct_, amp in ((4, 16), (16, 9), (64, 5)):
        small = rng.normal(0, amp, (oct_, oct_, 1))
        a += np.kron(small, np.ones((w // oct_, w // oct_, 1)))[:w, :w]
    im = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))
    dr = ImageDraw.Draw(im)
    # jejak ban: 2 pasang alur gelap memanjang
    for u in (0.30, 0.42, 0.58, 0.70):
        dr.rectangle([w * u - w // 57, 0, w * u + w // 57, w], fill=(64, 48, 32))
    dr.rectangle([0, 0, w * 0.06, w], fill=(78, 86, 46))   # bahu rumput
    dr.rectangle([w * 0.94, 0, w, w], fill=(78, 86, 46))
    im = im.filter(ImageFilter.GaussianBlur(1.2))
    # batu kecil
    dr = ImageDraw.Draw(im)
    for _ in range(int(300 * (w / 512) ** 2)):
        y, x = rng.integers(0, w, 2); r = rng.integers(1, 2 + w // 512)
        dr.ellipse([x, y, x + r, y + r], fill=(122, 112, 92))
    return im


# ------------------------------------------------------------------ jaringan jalan
def catmull(pts, seg=6):
    """kurva halus lewat titik kontrol (Catmull-Rom), pts list (x,z)."""
    P = np.array(pts, np.float64)
    if len(P) < 3:
        return P
    P = np.vstack([P[0] - (P[1] - P[0]), P, P[-1] + (P[-1] - P[-2])])
    out = []
    for i in range(1, len(P) - 2):
        p0, p1, p2, p3 = P[i - 1], P[i], P[i + 1], P[i + 2]
        for t in np.linspace(0, 1, seg, endpoint=False):
            t2, t3 = t * t, t * t * t
            q = 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2
                       + (-p0 + 3 * p1 - 3 * p2 + p3) * t3)
            out.append(q)
    out.append(P[-2])
    return np.array(out)


def resample(pts, step=4.0):
    d = np.linalg.norm(np.diff(pts, axis=0), axis=1)
    s = np.concatenate([[0], np.cumsum(d)])
    n = max(2, int(s[-1] / step))
    si = np.linspace(0, s[-1], n)
    x = np.interp(si, s, pts[:, 0]); z = np.interp(si, s, pts[:, 1])
    return np.stack([x, z], 1), si


def smooth_profile(xz, h, max_slope, iters=4000):
    """haluskan profil tinggi agar |gradien| <= max_slope (vektor, cepat)."""
    h = h.copy()
    d = np.sqrt(((np.diff(xz, axis=0)) ** 2).sum(1))
    step = float(np.median(d)) if len(d) else 4.0
    lim = max_slope * step
    for _ in range(iters):
        h1 = h.copy()
        np.minimum(h[:-1] + lim, h[1:], out=h[1:])
        np.minimum(h[1:] + lim, h[:-1], out=h[:-1])
        if np.array_equal(h1, h):
            break
    for _ in range(3):
        h[1:-1] = (h[:-2] + h[1:-1] * 2 + h[2:]) / 4
    return h


def split_by_deviation(xz, h_ter, h_smooth, max_dev=0.75, min_len=80.0):
    """pisahkan jalan jadi potongan yang benar-benar menempel di terrain."""
    dev = np.abs(h_smooth - h_ter)
    ok = dev <= max_dev
    runs = []
    s = None
    for i, v in enumerate(ok):
        if v and s is None:
            s = i
        if (not v) and s is not None:
            runs.append((s, i)); s = None
    if s is not None:
        runs.append((s, len(ok)))
    step = 4.0
    out = []
    for a, b in runs:
        if (b - a) * step >= min_len:
            out.append((a, b))
    return out


def build_ribbon(xz, h, width, uv_scale=8.0, curb=0.0, step_tex=1 / 8.0, curb_h=0.18):
    """Bangun pita jalan: posisi, normal, uv, indeks.

    Bagian 1 = badan jalan (material 0), bagian 2 = trotoar + kerb (material 1).
    Trotoar dibuat 3 pita: permukaan atas, dinding kerb menghadap jalan, dan
    dinding luar. curb = lebar trotoar (m) di tiap sisi, 0 = tanpa trotoar.
    """
    n = len(xz)
    tan = np.gradient(xz, axis=0)
    tan /= (np.linalg.norm(tan, axis=1, keepdims=True) + 1e-9)
    nrm = np.stack([-tan[:, 1], tan[:, 0]], 1)            # vektor samping (x,z)
    dist = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(xz, axis=0), axis=1))])

    def strip(offset_a, offset_b, y_a, y_b, u0, u1):
        """pita antara dua offset melintang, dengan tinggi masing-masing sisi."""
        pa = xz + nrm * offset_a[:, None]
        pb = xz + nrm * offset_b[:, None]
        pos = np.zeros((n * 2, 3), np.float32)
        pos[0::2, 0] = pa[:, 0]; pos[0::2, 2] = pa[:, 1]; pos[0::2, 1] = h + y_a
        pos[1::2, 0] = pb[:, 0]; pos[1::2, 2] = pb[:, 1]; pos[1::2, 1] = h + y_b
        uv = np.zeros((n * 2, 2), np.float32)
        uv[0::2, 0] = u0; uv[1::2, 0] = u1
        uv[0::2, 1] = dist * step_tex; uv[1::2, 1] = dist * step_tex
        idx = []
        for i in range(n - 1):
            a, b, c, d = 2 * i, 2 * i + 1, 2 * i + 2, 2 * i + 3
            idx += [a, c, b, b, c, d]
        return pos, uv, np.array(idx, np.uint32)

    hw = width * 0.5
    parts = []
    o1 = -np.full(n, hw); o2 = np.full(n, hw)
    parts.append(strip(o1, o2, 0.05, 0.05, 0.0, 1.0))          # badan jalan
    if curb > 0:
        top = 0.05 + curb_h
        for sgn in (-1.0, 1.0):
            outer = sgn * (hw + curb)
            inner = sgn * hw
            # permukaan trotoar (dari tepi jalan ke luar)
            parts.append(strip(np.full(n, inner), np.full(n, outer), top, top, 0.02, 0.98))
            # dinding kerb menghadap jalan (tegak, tinggi 0.18 m)
            parts.append(strip(np.full(n, inner), np.full(n, inner), top, 0.05, 0.05, 0.95))

    pos = np.concatenate([p[0] for p in parts])
    uv = np.concatenate([p[1] for p in parts])
    off = 0
    idxs = []
    for p in parts:
        idxs.append(p[2] + off)
        off += len(p[0])
    idx = np.concatenate(idxs)
    # normal dari segitiga (flat) supaya tepi tetap tajam
    tri = pos[idx].reshape(-1, 3, 3)
    nvec = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    ln = np.linalg.norm(nvec, axis=1, keepdims=True) + 1e-9
    nvec /= ln
    nvec[nvec[:, 1] < -0.2] *= -1        # permukaan atas menghadap langit
    vn = np.zeros_like(pos)
    for k in range(3):
        np.add.at(vn, idx[k::3], nvec)
    vn /= (np.linalg.norm(vn, axis=1, keepdims=True) + 1e-9)
    # jumlah VERTEKS badan jalan (material 0) vs trotoar (material 1)
    n_road_verts = len(parts[0][0])
    return pos, vn.astype(np.float32), uv.astype(np.float32), idx, n_road_verts


def make_road(name, pts, width, kind, curb=0.0):
    xz, _ = resample(np.array(pts, np.float64), 4.0)
    h_ter = T.height(xz[:, 0], xz[:, 1])
    ok = h_ter >= 0.9                       # tidak boleh di laut / y<0.5
    runs = []
    s = None
    for i, v in enumerate(ok):
        if v and s is None:
            s = i
        if (not v) and s is not None:
            runs.append((s, i)); s = None
    if s is not None:
        runs.append((s, len(ok)))
    max_slope = 0.10 if (kind == "tanah" and h_ter.max() > 140) else 0.12
    out = []
    for a, b in runs:
        if (b - a) * 4.0 < 120:
            continue
        x = xz[a:b]; ht = h_ter[a:b]
        hs = smooth_profile(x, ht, max_slope)
        for c, d in split_by_deviation(x, ht, hs):
            out.append(dict(name=f"{name}_{c}", xz=x[c:d], h=hs[c:d], h_ter=ht[c:d],
                            width=width, kind=kind, curb=curb))
    return out


# ------------------------------------------------------------ mask biome (dilebarkan)
def mask_of(classes, dilate_cells=3):
    m = np.isin(T.bio, classes)
    for _ in range(dilate_cells):
        m = m | np.roll(m, 1, 0) | np.roll(m, -1, 0) | np.roll(m, 1, 1) | np.roll(m, -1, 1)
    return m


M_KOTA = mask_of([2], 3)
M_DESA = mask_of([3], 3)
M_HUTAN = mask_of([4], 3)
M_GUNUNG = mask_of([4, 5], 2)
M_DARAT = T.bio > 0


def in_mask(x, z, mask):
    i = np.clip(np.rint(T.gi(np.asarray(x, float))).astype(int), 0, T.N - 1)
    j = np.clip(np.rint(T.gj(np.asarray(z, float))).astype(int), 0, T.N - 1)
    return mask[j, i]


def clip_runs(xz, mask, min_len_m=140.0, step=4.0):
    """potong polyline hanya pada bagian yang berada di dalam mask."""
    ok = in_mask(xz[:, 0], xz[:, 1], mask)
    runs, s0 = [], None
    for i, v in enumerate(ok):
        if v and s0 is None:
            s0 = i
        if (not v) and s0 is not None:
            runs.append((s0, i)); s0 = None
    if s0 is not None:
        runs.append((s0, len(ok)))
    return [(a, b) for a, b in runs if (b - a) * step >= min_len_m]


def road_networks():
    """Jaringan jalan (koordinat dunia x,z). Wilayah nyata dari peta biome:
    kota x[-2500,-280] z[-790,3410]; desa x[-1525,3400] z[-2340,3850];
    hutan barat-laut x[-2800,100] z[-2750,-200] dan hutan timur-laut x[730,2540]
    z[-2415,-1050]; dataran tinggi (gunung) di sekitar (600,-280) tinggi ~215 m."""
    R = []   # (kind, points, width, curb)

    # 1) JALAN LINGKAR: mengelilingi pulau (di dalam garis pantai), menghubungkan
    #    kota-desa-hutan-pantai
    ring = []
    for a in np.linspace(0, 2 * np.pi, 37, endpoint=False):
        r = 1780 + 190 * math.sin(3 * a + 0.7) + 90 * math.sin(5 * a)
        ring.append((r * math.cos(a), r * math.sin(a)))
    ring.append(ring[0])
    R.append(("desa", catmull(ring, 6), 7.2, 1.5))     # ring: trotoar 1.5 m

    # 2) KOTA: grid jalan (jarak 400 m), hanya di dalam wilayah kota
    for x in (-2350, -1950, -1550, -1150, -750):
        R.append(("kota", catmull([(x, -700), (x + 40, 200), (x - 30, 1100),
                                   (x + 30, 2000), (x, 2900)], 5), 13.0, 2.0))
    for z in (-600, -200, 200, 600, 1000, 1400, 1800, 2200, 2600, 3000):
        R.append(("kota", catmull([(-2500, z), (-1900, z + 25), (-1300, z - 20),
                                   (-700, z + 15), (-300, z)], 5), 13.0, 2.0))

    # 3) DESA: jalan sempit menghubungkan kelompok rumah di timur & selatan
    desa = [
        [(1200, 0), (1600, 300), (2000, 200), (2400, 600), (2200, 1100), (1700, 1300),
         (1100, 1100), (800, 700)],
        [(300, 1200), (700, 1600), (1200, 1900), (900, 2400), (400, 2600), (0, 2300),
         (-300, 1800)],
        [(2400, 600), (2700, 1200), (2500, 1900), (2000, 2300), (1400, 2200)],
        [(1000, -300), (1400, -700), (1900, -600), (2300, -1100), (2000, -1600)],
        [(-300, 2100), (-200, 2700), (300, 3100), (900, 3300)],
        [(2600, -500), (3000, -200), (3200, 400)],
    ]
    for pts in desa:
        R.append(("desa", catmull(pts + [pts[0]], 6), 6.0, 1.2))   # trotoar 1.2 m
    # penghubung lingkar -> desa
    for a, b in [((900, 2400), (600, 2100)), ((2000, 2300), (1300, 2500)),
                 ((2300, -1100), (1500, -900)), ((3200, 400), (2700, 900))]:
        R.append(("desa", catmull([a, ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2 + 60), b], 6),
                  6.0, 1.2))

    # 4) HUTAN: jalan tanah berkelok
    hutan = [
        [(-2300, -300), (-2500, -800), (-2200, -1400), (-1700, -1300), (-1400, -1800),
         (-1800, -2300), (-2300, -2100), (-2600, -1600)],
        [(-1500, -400), (-1200, -800), (-900, -600), (-600, -900), (-300, -600)],
        [(800, -2200), (1300, -2000), (1700, -1700), (2200, -1800), (2400, -1400),
         (1900, -1200), (1400, -1400)],
        [(400, -1600), (600, -2000), (200, -2400), (-200, -2200)],
    ]
    for pts in hutan:
        R.append(("tanah", catmull(pts + [pts[0]], 7), 4.8, 0.0))

    # 5) 2 KORIDOR LANDAI ke dataran tinggi (maks 10%), lewat punggung yang landai
    R.append(("tanah", catmull([(300, 1200), (350, 800), (250, 300), (400, -200),
                                (600, -400)], 6), 5.0, 0.0))
    R.append(("tanah", catmull([(1400, -700), (1150, -600), (950, -350), (800, -260),
                                (750, -280)], 6), 5.0, 0.0))
    return R


def build_all():
    roads = []
    for kind, pts, w, curb in road_networks():
        xzi, _ = resample(np.array(pts, np.float64), 4.0)
        mask = dict(kota=M_KOTA, desa=(M_DESA | M_KOTA), tanah=(M_HUTAN | M_GUNUNG))[kind]
        for a, b in clip_runs(xzi, mask):
            roads.extend(make_road(f"{kind}_{len(roads)}", xzi[a:b], w, kind, curb))
    # buang segmen yang bertumpuk terlalu dekat dengan jalan sejenis (dedupe ringan)
    for kind in ("desa", "kota", "tanah"):
        sel = [r for r in roads if r["kind"] == kind]
        print(f"  {kind}: {len(sel)} jalur, panjang total "
              f"{sum(np.linalg.norm(np.diff(r['xz'],axis=0),axis=1).sum() for r in sel)/1000:.1f} km")

    tex = dict(desa=tex_desa(), kota=tex_kota(), tanah=tex_tanah())
    tex_trotoar = tex_beton()
    stats = {}
    for kind in ("desa", "kota", "tanah"):
        sc = GLBScene()
        mi = sc.add_material(f"road_{kind}", (1, 1, 1, 1), png_bytes(tex[kind]), rough=0.9)
        mt = sc.add_material(f"trotoar_{kind}", (1, 1, 1, 1), png_bytes(tex_trotoar), rough=0.95)
        total = 0.0
        maxslope = 0.0
        minh = 1e9
        n_trotoar_tri = 0
        for r in [r for r in roads if r["kind"] == kind]:
            pos, nrm, uv, idx, n_road_verts = build_ribbon(r["xz"], r["h"], r["width"],
                                                           curb=r["curb"])
            # bagi per chunk 200 m
            step = 4.0
            per = int(200 / step) * 2
            for s in range(0, len(pos), per):
                e = min(s + per, len(pos))
                if e - s < 6:
                    continue
                vmap = {}
                P = []; N = []; U = []
                IDX = []
                for t in idx:
                    pass
                # bagi per material: badan jalan (0) vs trotoar/kerb (1)
                tri_all = idx.reshape(-1, 3)
                in_range = (tri_all >= s).all(axis=1) & (tri_all < e).all(axis=1)
                for mat_id, sel_mask in ((0, in_range & (tri_all[:, 0] < n_road_verts)),
                                         (1, in_range & (tri_all[:, 0] >= n_road_verts))):
                    sub = tri_all[sel_mask]
                    if len(sub) == 0:
                        continue
                    used = np.unique(sub)
                    remap = {int(v): i for i, v in enumerate(used)}
                    P = pos[used]; N = nrm[used]; U = uv[used]
                    IDX = np.array([remap[int(v)] for v in sub.reshape(-1)], np.uint32)
                    mat = mi if mat_id == 0 else mt
                    sc.add_mesh(Mesh(f"{r['name']}_{s}_{mat_id}", P, N, U, indices=IDX,
                                     material=mat))
                    if mat_id == 1:
                        n_trotoar_tri += len(IDX) // 3
            d = np.linalg.norm(np.diff(r["xz"], axis=0), axis=1)
            total += d.sum()
            sl = np.abs(np.diff(r["h"])) / np.maximum(d, 1e-6)
            maxslope = max(maxslope, float(sl.max()))
            minh = min(minh, float(r["h"].min()))
        for i in range(len(sc.meshes)):
            sc.add_node(sc.meshes[i].name, i)
        out = os.path.join(GEN, f"roads_{kind}.glb")
        sc.write(out)
        p = probe(out)
        print(f"  -> {os.path.basename(out)}: {p['tris']} tri, {p['size']/1e6:.2f} MB, "
              f"kemiringan maks {maxslope*100:.1f}%, tinggi min {minh:.2f} m, "
              f"trotoar {n_trotoar_tri} tri")
        stats[kind] = dict(panjang_km=round(total / 1000, 2), lebar_m=None,
                           max_slope_pct=round(maxslope * 100, 1), min_height=round(minh, 2),
                           tris=p["tris"])

    # roads.json: centerline + lebar + grip untuk fisika
    data = dict(
        meta=dict(satuan="meter", y_offset=0.05, sumber="build_roads.py"),
        grip=dict(aspal_kota=1.00, aspal_desa=0.95, tanah=0.62, rumput=0.42, pasir=0.30),
        lebar=dict(desa=6.0, kota=13.0, tanah=4.8, trotoar=2.0),
        statistik=stats,
        jalan=[],
    )
    for r in roads:
        keep = np.arange(0, len(r["xz"]), 4)      # tiap 16 m
        data["jalan"].append(dict(
            jenis=r["kind"], lebar=round(float(r["width"]), 2),
            trotoar=round(float(r.get("curb", 0.0)), 2),
            titik=[[round(float(x), 1), round(float(z), 1), round(float(h), 2)]
                   for x, z, h in zip(r["xz"][keep, 0], r["xz"][keep, 1], r["h"][keep])]))
    with open(os.path.join(GEN, "roads.json"), "w") as f:
        json.dump(data, f, separators=(",", ":"))
    print("  -> roads.json", os.path.getsize(os.path.join(GEN, "roads.json")) // 1024, "KB")

    # ---- render atas untuk validasi
    render_top(roads, stats)
    return data


def render_top(roads, stats):
    W = 900
    img = Image.new("RGB", (W, W), (10, 26, 44))
    dr = ImageDraw.Draw(img, "RGBA")

    def tx(x, z):
        return ((x - T.x0) / (T.x1 - T.x0) * W, (z - T.z0) / (T.z1 - T.z0) * W)

    # terrain biolum
    bi = np.clip((np.arange(W) * T.N // W), 0, T.N - 1)
    bio = T.bio[bi][:, bi]
    palette = {0: (10, 26, 44), 1: (238, 224, 182), 2: (128, 130, 126), 3: (178, 196, 96),
               4: (34, 80, 42), 5: (116, 110, 60), 6: (34, 98, 132)}
    arr = np.zeros((W, W, 3), np.uint8)
    hh = T.H[bi][:, bi]
    shade = 0.6 + 0.5 * (hh - hh.min()) / (hh.max() - hh.min())
    for k, c in palette.items():
        m = bio == k
        arr[m] = (np.array(c) * shade[m][:, None]).astype(np.uint8)
    img = Image.fromarray(arr)
    dr = ImageDraw.Draw(img)
    col = dict(kota=(255, 90, 60), desa=(255, 220, 90), tanah=(180, 120, 60))
    for r in roads:
        pts = [tx(x, z) for x, z in r["xz"]]
        wpx = max(1, int(r["width"] / (T.x1 - T.x0) * W))
        dr.line(pts, fill=col[r["kind"]], width=wpx)
    # tandai danau dan pusat wilayah
    dr.text((8, 8), "JALAN: kota=merah desa=kuning tanah=coklat", fill=(255, 255, 255))
    y = 24
    for k, v in stats.items():
        dr.text((8, y), f"{k}: {v['panjang_km']} km, slope maks {v['max_slope_pct']}%, "
                        f"h min {v['min_height']} m", fill=(255, 255, 255)); y += 16
    img.save(os.path.join(PREV, "roads_top.png"))
    print("  -> render atas:", os.path.join(PREV, "roads_top.png"))


if __name__ == "__main__":
    build_all()

