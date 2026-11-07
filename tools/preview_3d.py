#!/usr/bin/env python3
"""preview_3d.py - pratinjau 3D tanpa GPU (software rasterizer + buffer z).

Menghasilkan:
  output/preview/pandangan_spawn.png  - pemandangan dari kamera chase di titik spawn
  output/preview/pandangan_udara.png  - pemandangan udara di atas kota

Tujuan: memastikan warna/tekstur/penempatan masuk akal walau tidak ada HP/GPU di
sandbox. Ini bukan emulator GL; hanya cek visual kasar.
"""
import json
import math
import os
import struct
import sys

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from glb_probe import load  # noqa: E402

GEN = os.path.join(ROOT, "assets", "generated")
APP = os.path.join(ROOT, "app", "assets")
OUT = os.path.join(ROOT, "output", "preview")
os.makedirs(OUT, exist_ok=True)

W, H = 900, 506
NEAR, FAR = 0.35, 1400.0

# ------------------------------------------------------------------ terrain
_d = open(os.path.join(GEN, "island.bin"), "rb").read()
magic, ver, N, x0, x1, z0, z1, _m1, _m2 = struct.unpack_from("<4sII6f", _d, 0)
HH = np.frombuffer(_d, dtype="<f4", count=N * N, offset=36).reshape(N, N).astype(np.float64)
COL = np.frombuffer(_d, dtype=np.uint8, count=N * N * 3, offset=36 + N * N * 4).reshape(N, N, 3)
BIO = np.frombuffer(_d, dtype=np.uint8, count=N * N, offset=36 + N * N * 4 + N * N * 3).reshape(N, N)


def h_at(x, z):
    fx = np.clip((np.asarray(x, np.float64) - x0) / (x1 - x0) * (N - 1), 0, N - 1.001)
    fz = np.clip((np.asarray(z, np.float64) - z0) / (z1 - z0) * (N - 1), 0, N - 1.001)
    i = np.floor(fx).astype(int)
    j = np.floor(fz).astype(int)
    tx, tz = fx - i, fz - j
    return (HH[j, i] * (1 - tx) * (1 - tz) + HH[j, i + 1] * tx * (1 - tz)
            + HH[j + 1, i] * (1 - tx) * tz + HH[j + 1, i + 1] * tx * tz)


FOV = math.radians(62)
ASPECT = W / H
FY = 1.0 / math.tan(FOV / 2)
FX = FY / ASPECT


def look_at(eye, target, up=(0, 1, 0)):
    f = np.asarray(target, float) - np.asarray(eye, float)
    f /= np.linalg.norm(f)
    u = np.asarray(up, float)
    s = np.cross(f, u); s /= np.linalg.norm(s)
    uu = np.cross(s, f)
    M = np.eye(4)
    M[0, :3], M[1, :3], M[2, :3] = s, uu, -f
    M[:3, 3] = -M[:3, :3] @ np.asarray(eye, float)
    return M


def project(pts, M):
    """pts (n,3) dunia -> layar (n,2) + kedalaman kamera (n,) + flag valid."""
    P = np.concatenate([pts, np.ones((len(pts), 1))], 1) @ M.T
    z = -P[:, 2]
    ok = z > 0.02
    zz = np.maximum(z, NEAR)   # verteks di belakang kamera: dijepit (pratinjau kasar)
    sx = (P[:, 0] * FX / zz) * 0.5 * W + W * 0.5
    sy = (-P[:, 1] * FY / zz) * 0.5 * H + H * 0.5
    return np.stack([sx, sy], 1), z, ok


class Canvas:
    def __init__(self):
        self.col = np.zeros((H, W, 3), np.float32)
        self.z = np.full((H, W), np.inf)

    def sky(self):
        t = np.linspace(0, 1, H)[:, None]
        top = np.array([0.33, 0.56, 0.86]) ** 2.2
        bot = np.array([0.74, 0.84, 0.93]) ** 2.2
        self.col[:] = (top * (1 - t) + bot * t)[:, None, :]

    def tri(self, p, z, rgb):
        """rasterisasi segitiga 2D dengan uji z-buffer (looping barycentric)."""
        (x0_, y0_), (x1_, y1_), (x2_, y2_) = p
        minx = max(int(math.floor(min(x0_, x1_, x2_))), 0)
        maxx = min(int(math.ceil(max(x0_, x1_, x2_))), W - 1)
        miny = max(int(math.floor(min(y0_, y1_, y2_))), 0)
        maxy = min(int(math.ceil(max(y0_, y1_, y2_))), H - 1)
        if minx > maxx or miny > maxy:
            return
        xs = np.arange(minx, maxx + 1) + 0.5
        ys = np.arange(miny, maxy + 1) + 0.5
        XX, YY = np.meshgrid(xs, ys)
        d = (y1_ - y2_) * (x0_ - x2_) + (x2_ - x1_) * (y0_ - y2_)
        if abs(d) < 1e-9:
            return
        a = ((y1_ - y2_) * (XX - x2_) + (x2_ - x1_) * (YY - y2_)) / d
        b = ((y2_ - y0_) * (XX - x2_) + (x0_ - x2_) * (YY - y2_)) / d
        c = 1 - a - b
        inside = (a >= 0) & (b >= 0) & (c >= 0)
        if not inside.any():
            return
        zz = a * z[0] + b * z[1] + c * z[2]
        sub = self.z[miny:maxy + 1, minx:maxx + 1]
        upd = inside & (zz < sub)
        if not upd.any():
            return
        sub[upd] = zz[upd]
        self.col[miny:maxy + 1, minx:maxx + 1][upd] = rgb

    def fog(self, d):
        f = np.clip((d - 210.0) / (430.0 - 210.0), 0, 1)[..., None]
        target = np.array([0.70, 0.80, 0.90]) ** 2.2
        return self.col * (1 - f) + target * f

    def save(self, path):
        img = np.clip(self.fog(np.zeros((H, W)) + 400) ** (1 / 2.2) * 255, 0, 255).astype(np.uint8)
        Image.fromarray(img).save(path)


LIGHT = np.array([0.42, 0.78, 0.32]); LIGHT /= np.linalg.norm(LIGHT)


def draw_mesh(cv, V, idx, M, camM, tex_colors=None, tint=np.array([1, 1, 1]), dbl=False):
    if len(idx) == 0:
        return
    scr, zc, ok = project(V, camM)
    tris = idx.reshape(-1, 3)
    zt = zc[tris]
    vis = ok[tris].all(1) & (zt < 1400).all(1)
    tris, zt = tris[vis], zt[vis]
    if len(tris) == 0:
        return
    # normal dunia
    A, B, C = V[tris[:, 0]], V[tris[:, 1]], V[tris[:, 2]]
    n = np.cross(B - A, C - A)
    ln = np.linalg.norm(n, axis=1)
    ln[ln < 1e-12] = 1
    n = n / ln[:, None]
    shade = np.clip(n @ LIGHT, 0, 1) * 0.58 + 0.52
    for k in range(len(tris)):
        t = tris[k]
        col = tint * shade[k]
        if tex_colors is not None:
            col = col * tex_colors[k]
        cv.tri(scr[t], zt[k], np.clip(col, 0, 1))


def model_mesh(path):
    js, bin_ = load(path)
    Vs, Is, cols = [], [], []
    for m in js["meshes"]:
        for pr in m["primitives"]:
            a = js["accessors"][pr["attributes"]["POSITION"]]
            bv = js["bufferViews"][a["bufferView"]]
            stride = bv.get("byteStride") or max(12, bv["byteLength"] // max(1, a["count"]))
            off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
            V = np.frombuffer(bin_, dtype="<f4", count=a["count"] * (stride // 4),
                              offset=off).reshape(-1, stride // 4)[:, :3].astype(np.float64)
            base = len(Vs)
            Vs.append(V)
            ia = js["accessors"][pr["indices"]]
            ib = js["bufferViews"][ia["bufferView"]]
            ioff = ib.get("byteOffset", 0) + ia.get("byteOffset", 0)
            I = np.frombuffer(bin_, dtype="<I", count=ia["count"], offset=ioff).astype(np.int64) + base
            Is.append(I)
            cols.append(vertex_color(js, bin_, m, pr, a["count"]))
    return np.vstack(Vs), np.concatenate(Is), np.vstack(cols) if cols[0] is not None else None


def vertex_color(js, bin_, mesh, pr, n):
    """warna rata-rata material (perkiraan) supaya gambar tidak hitam."""
    if "material" in pr:
        m = js["materials"][pr["material"]]
        pbr = m.get("pbrMetallicRoughness", {})
        f = pbr.get("baseColorFactor", [1, 1, 1, 1])
        base = np.array(f[:3], float)
        if "baseColorTexture" in pbr:
            base = base * texture_mean(js, bin_, pbr["baseColorTexture"]["index"])
        return np.tile(base, (n, 1))
    if "COLOR_0" in pr["attributes"]:
        a = js["accessors"][pr["attributes"]["COLOR_0"]]
        bv = js["bufferViews"][a["bufferView"]]
        off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
        if a["componentType"] == 5126:
            C = np.frombuffer(bin_, dtype="<f4", count=a["count"] * 3, offset=off).reshape(-1, 3)
        else:
            C = np.frombuffer(bin_, dtype=np.uint8, count=a["count"] * 4,
                              offset=off).reshape(-1, 4)[:, :3].astype(float) / 255
        return np.clip(np.asarray(C, float) ** 2.2, 0, 1)
    return np.ones((n, 3))


_texcache = {}


def texture_mean(js, bin_, ti):
    if ti in _texcache:
        return _texcache[ti]
    import io
    img = js["images"][js["textures"][ti].get("source", 0)]
    try:
        if "bufferView" in img:
            bv = js["bufferViews"][img["bufferView"]]
            off = bv.get("byteOffset", 0)
            data = bin_[off:off + bv["byteLength"]]
            im = Image.open(io.BytesIO(data)).convert("RGB")
            a = np.asarray(im.resize((16, 16)), float) / 255.0
            c = (a.mean((0, 1)) ** 2.2)
        else:
            c = np.array([0.6, 0.6, 0.6])
    except Exception:
        c = np.array([0.6, 0.6, 0.6])
    _texcache[ti] = c
    return c


def rotY(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def boxes(cv, P, camM, col, size=1.0):
    """gambar kubus untuk objek (pohon/rumah/gedung) - cepat."""
    if len(P) == 0:
        return
    scr, z, ok = project(P, camM)
    keep = ok & (z < 420)
    scr, z, P = scr[keep], z[keep], P[keep]
    if len(P) == 0:
        return
    r = np.maximum(1.0, 260.0 / z) * size
    for k in range(len(scr)):
        x, y = scr[k]
        h = r[k] * 2.2
        cv.tri(np.array([[x - r[k], y], [x + r[k], y], [x, y - h]]), np.array([z[k]] * 3), col)


_bb = {}


def bbox_h(t):
    """tinggi model untuk tipe props (rumah desa 4..15, gedung kota 16..27)."""
    if t in _bb:
        return _bb[t][0]
    if t < 4:
        h, w = 8.0, 4.0
    elif t < 16:
        n = t - 4
        fam, idx = n // 3, n % 3
        path = os.path.join(APP, "models", "desa", f"desa_{'ABCD'[fam]}_{idx + 1}_lod1.glb")
        h, w = _measure(path, 7.0, 9.0)
    else:
        n = t - 16
        fam, idx = n // 3, n % 3
        path = os.path.join(APP, "models", "kota", f"kota_{'ABCD'[fam]}_{idx + 1}_lod1.glb")
        h, w = _measure(path, 30.0, 18.0)
    _bb[t] = (h, w)
    return h


def bbox_w(t):
    bbox_h(t)
    return _bb[t][1]


def _measure(path, dh, dw):
    try:
        js, b = load(path)
        a = js["accessors"][0]
        mn, mx = a["min"], a["max"]
        return float(mx[1] - mn[1]), float(mx[0] - mn[0])
    except Exception:
        return dh, dw


def render(campos, target, out, radius=340.0):
    cv = Canvas()
    cv.sky()
    camM = look_at(campos, target)

    # ---- terrain (grid sampel, kubus kecil per sel) -> pakai segitiga permukaan
    # makin dekat kamera makin rapat (grid geometris): 3 m di dekat, 25 m di jauh
    def grid(c, r, near=40.0, nfast=34):
        # rapat 2 m di sekitar kamera (anti lubang), lalu makin renggang ke jauh
        inner = np.linspace(-near, near, int(2 * near / 2.0) + 1)
        far_r = np.geomspace(near + 2.0, r, nfast)
        return np.concatenate([c - far_r[::-1], c + inner, c + far_r])

    gx = grid(campos[0], radius)
    gz = grid(campos[2], radius)
    n = len(gx)
    GX, GZ = np.meshgrid(gx, gz)
    GY = h_at(GX, GZ)
    V = np.stack([GX, GY, GZ], -1).reshape(-1, 3)
    idx = []
    for j in range(n - 1):
        for i in range(n - 1):
            a = j * n + i
            b, c, d = a + 1, a + n, a + n + 1
            idx += [a, b, c, b, d, c]
    idx = np.array(idx)
    fi = np.clip(((GX - x0) / (x1 - x0) * (N - 1)).astype(int), 0, N - 1)
    fj = np.clip(((GZ - z0) / (z1 - z0) * (N - 1)).astype(int), 0, N - 1)
    tcol = (COL[fj, fi].reshape(-1, 3).astype(np.float64) / 255.0) ** 2.2
    tris = idx.reshape(-1, 3)
    scr, zc, ok = project(V, camM)
    zt = zc[tris]
    vis = ok[tris].all(1) & (zt < 1400).all(1) & (zt > 0.5).all(1)
    tris2, zt2 = tris[vis], zt[vis]
    scr2 = scr[tris]
    A, B, C = V[tris2[:, 0]], V[tris2[:, 1]], V[tris2[:, 2]]
    nrm = np.cross(B - A, C - A)
    ln = np.linalg.norm(nrm, axis=1); ln[ln < 1e-12] = 1
    sh = np.clip((nrm / ln[:, None]) @ LIGHT, 0, 1) * 0.58 + 0.52
    tc = tcol[tris2[:, 0]]
    for k in range(len(tris2)):
        cv.tri(scr2[k], zt2[k], np.clip(tc[k] * sh[k], 0, 1))

    # ---- jalan
    for kind in ("desa", "kota", "tanah"):
        p = os.path.join(APP, "models", f"roads_{kind}.glb")
        if not os.path.exists(p):
            continue
        V2, I2, _c = model_mesh(p)
        col = np.array([0.16, 0.16, 0.17]) if kind != "tanah" else np.array([0.34, 0.25, 0.15])
        draw_mesh(cv, V2, I2, None, camM, tint=col)
        # trotoar: model terpisah, warna beton terang
        pt = os.path.join(APP, "models", f"roads_{kind}_trotoar.glb")
        if os.path.exists(pt):
            V3, I3, _c = model_mesh(pt)
            draw_mesh(cv, V3, I3, None, camM, tint=np.array([0.62, 0.61, 0.58]))

    # ---- objek dari props.bin
    pb = open(os.path.join(APP, "generated", "props.bin"), "rb").read()
    _m, _v, cnt, _nc = struct.unpack_from("<4sIII", pb, 0)
    arr = np.frombuffer(pb, dtype="<f4", count=cnt * 5, offset=16).reshape(cnt, 5)
    tps = np.frombuffer(pb, dtype="<u2", count=cnt, offset=16 + cnt * 20)
    d2 = (arr[:, 0] - campos[0]) ** 2 + (arr[:, 2] - campos[2]) ** 2
    sel = d2 < radius * radius
    arr, tps = arr[sel], tps[sel]
    if len(arr):
        P = arr[:, [0, 1, 2]].astype(np.float64)
        P[:, 1] += 1.0
        tree = tps < 4
        if tree.any():
            boxes(cv, P[tree], camM, np.array([0.06, 0.22, 0.07]), 1.0)
        bld = ~tree
        if bld.any():
            Pb = P[bld]
            tb = tps[bld]
            cols = np.where((tb < 16)[:, None], np.array([[0.78, 0.70, 0.56]]),
                            np.array([[0.60, 0.64, 0.68]]))
            # tinggi & lebar nyata per tipe (dari glb paket)
            scr, z, ok = project(Pb, camM)
            top = Pb.copy()
            top[:, 1] += np.array([bbox_h(int(t)) for t in tb])
            scr2, z2, ok2 = project(top, camM)
            wid = np.array([bbox_w(int(t)) for t in tb])
            for k in np.nonzero(ok & (z > 6.0) & (z < 520))[0]:
                x0s, y0s = scr[k]
                x1s, y1s = scr2[k]
                zz = z[k]
                ww = max(1.2, wid[k] * 0.5 * FY / zz * 0.5 * W)
                c = cols[k] * (0.55 + 0.45 * k / max(1, len(scr)))
                q = np.array([[x0s - ww, y0s], [x0s + ww, y0s], [x1s + ww, y1s], [x1s - ww, y1s]])
                cv.tri(q[[0, 1, 2]], np.array([zz] * 3), np.clip(c * 0.9, 0, 1))
                cv.tri(q[[0, 2, 3]], np.array([zz] * 3), np.clip(c * 0.7, 0, 1))

    # ---- mobil
    pm = os.path.join(APP, "models", "mobil.glb")
    if os.path.exists(pm):
        V3, I3, C3 = model_mesh(pm)
        Mt = np.eye(4)
        Mt[:3, :3] = rotY(1.577)
        Mt[:3, 3] = [campos[0] + 6.4 * math.sin(1.577), h_at(campos[0], campos[2]) + 0.7,
                     campos[2] + 6.4 * math.cos(1.577)]
        P4 = np.concatenate([V3, np.ones((len(V3), 1))], 1) @ Mt.T
        draw_mesh(cv, P4[:, :3], I3, Mt, camM, tint=np.array([0.85, 0.20, 0.15]))

    img = np.clip(cv.fog(0.0) ** (1 / 2.2), 0, 1)
    Image.fromarray((img * 255).astype(np.uint8)).save(out)
    print("  render:", out)


if __name__ == "__main__":
    sp = json.load(open(os.path.join(APP, "generated", "car_spawn.json")))
    yaw = sp["yaw"]
    # kamera chase: 6.4 m di belakang mobil
    eye = (sp["x"] - 6.4 * math.sin(yaw), sp["y"] + 2.4, sp["z"] - 6.4 * math.cos(yaw))
    tgt = (sp["x"] + 4 * math.sin(yaw), sp["y"] + 1.1, sp["z"] + 4 * math.cos(yaw))
    render(eye, tgt, os.path.join(OUT, "pandangan_spawn.png"), radius=300.0)
    eye = (sp["x"] + 90, sp["y"] + 45, sp["z"] + 95)
    tgt = (sp["x"] - 150, sp["y"] - 5, sp["z"] - 260)
    render(eye, tgt, os.path.join(OUT, "pandangan_udara.png"), radius=420.0)
