#!/usr/bin/env python3
"""pack_assets.py - siapkan aset runtime Nusa Rush ke app/assets/.

Kenapa: loader cgltf di HP lambat untuk 28 model bertekstur (butuh ratusan ms + RAM besar).
Skrip ini "memanggang" setiap .glb menjadi GLB sederhana: 1 buffer, vertex unik
(POSITION/NORMAL/TEXCOORD_0) + indices u32, tanpa node transform, tekstur PNG 512 px
tertanam lewat bufferView. Runtime cukup membaca buffer langsung ke VBO (tanpa alokasi besar).

Keluaran app/assets/:
  generated/island.bin, roads.json, props.bin, car_spawn.json
  models/roads_{desa,kota,tanah}.glb            (mesh jalan + tekstur 512)
  models/trees/{oak,spruce,sakura,palm}[_lod1].glb
  models/desa/desa_X_n[_lod1].glb               (12)
  models/kota/kota_X_n[_lod1].glb               (12)
  models/mobil.glb
  assets_manifest.json
"""
import json
import os
import struct
import sys

import numpy as np
from PIL import Image


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from glb_probe import load  # noqa: E402

GEN = os.path.join(ROOT, "assets", "generated")
OUT = os.path.join(ROOT, "app", "assets")


def read_glb(path):
    js, bin_ = load(path)
    return js, bin_


def acc_array(js, bin_, idx):
    a = js["accessors"][idx]
    bv = js["bufferViews"][a["bufferView"]]
    ct = {5120: "<b", 5121: "<B", 5122: "<h", 5123: "<H", 5125: "<I", 5126: "<f"}[a["componentType"]]
    ncomp = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
    count = a["count"]
    stride = bv.get("byteStride", 0) or (struct.calcsize(ct) * ncomp)
    base = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    out = np.empty((count, ncomp), dtype=np.float32 if a["componentType"] == 5126 else np.int64)
    raw = bin_
    for k in range(count):
        o = base + k * stride
        vals = struct.unpack_from("<" + ct[1] * ncomp, raw, o)
        out[k] = vals
    return out


def png_embed(js, bin_, img_index, maxpx=1024, quality=92):
    """ambil PNG dari images[i] (bufferView atau data URI), simpan sampai maxpx.

    Sesi 3 (revisi): tekstur 512 px terlihat burem di layar 1080p, jadi dinaikkan
    ke 1024 px. Ukuran APK tetap < 60 MB karena JPEG quality 92.
    """
    import io
    import base64
    img = js["images"][img_index]
    if "bufferView" in img:
        bv = js["bufferViews"][img["bufferView"]]
        o = bv.get("byteOffset", 0)
        data = bin_[o:o + bv["byteLength"]]
    elif "uri" in img and img["uri"].startswith("data:"):
        data = base64.b64decode(img["uri"].split(",", 1)[1])
    else:
        return None
    im = Image.open(io.BytesIO(data)).convert("RGB")
    if max(im.size) > maxpx:
        im.thumbnail((maxpx, maxpx), Image.LANCZOS)
    b = io.BytesIO()
    im.save(b, "JPEG", quality=quality, optimize=True)
    return b.getvalue()


# tekstur pohon cukup 512 px (banyak instance, ukuran file penting);
# bangunan/gedung & mobil 1024 px karena dilihat dari dekat
TEX_POHON = 512
TEX_LOD = 256


def bake_glb(src, dst, maxpx=1024, lod=None, keep_tex=True, node_xform=False,
             wheel_pivots=None, mat_filter=None):
    js, bin_ = read_glb(src)
    verts = []          # (pos3, nrm3, uv2)
    indices = []
    prims = []          # (indexOffset, indexCount, nama mesh/node)
    tex_data = None
    tex2_data = None          # tekstur kedua (mis. trotoar jalan)
    mat_tex_seen = {}
    for mi, m in enumerate(js["meshes"]):
        for p in m["primitives"]:
            # mat_filter: hanya ambil primitif dengan material tertentu (mis. trotoar)
            if mat_filter is not None and p.get("material", 0) != mat_filter:
                continue
            at = p["attributes"]
            pos = acc_array(js, bin_, at["POSITION"])
            nrm = acc_array(js, bin_, at["NORMAL"]) if "NORMAL" in at else np.zeros_like(pos)
            uv = acc_array(js, bin_, at["TEXCOORD_0"]) if "TEXCOORD_0" in at else np.zeros((len(pos), 2))
            if "indices" in p:
                ix = acc_array(js, bin_, p["indices"]).reshape(-1).astype(np.int64)
            else:
                ix = np.arange(len(pos))
            base = len(verts)
            ioff = sum(len(a) for a in indices)
            name = m.get("name", "mesh%d" % mi)
            pivot = None
            if wheel_pivots:
                for wn, wp in wheel_pivots.items():
                    if wn.lower() in name.lower():
                        pivot = np.array(wp, np.float32)
            for k in range(len(pos)):
                p3 = pos[k] - pivot if pivot is not None else pos[k]
                verts.append((p3, nrm[k], uv[k]))
            indices.append(ix + base)
            # material 0 -> tekstur utama, material 1 -> tekstur kedua (trotoar)
            which = 0
            if keep_tex and "material" in p:
                # kalau memang hanya mengambil material kedua, jadikan tekstur utama
                if mat_filter == 1:
                    mat = js["materials"][p["material"]]
                    pbr = mat.get("pbrMetallicRoughness", {})
                    if "baseColorTexture" in pbr:
                        si = js["textures"][pbr["baseColorTexture"]["index"]].get("source")
                        if si is not None and tex_data is None:
                            tex_data = png_embed(js, bin_, si, maxpx)
                    which = 0
                    prims.append(dict(off=ioff, count=int(len(ix)), name=name, mat=0,
                                      pivot=None))
                    continue
                which = 1 if p["material"] >= 1 else 0
                mat = js["materials"][p["material"]]
                pbr = mat.get("pbrMetallicRoughness", {})
                if "baseColorTexture" in pbr:
                    ti = pbr["baseColorTexture"]["index"]
                    src_img = js["textures"][ti].get("source")
                    if src_img is not None:
                        if which == 0 and tex_data is None:
                            tex_data = png_embed(js, bin_, src_img, maxpx)
                        elif which == 1 and tex2_data is None:
                            tex2_data = png_embed(js, bin_, src_img, maxpx)
            prims.append(dict(off=ioff, count=int(len(ix)), name=name, mat=which,
                              pivot=(pivot.tolist() if pivot is not None else None)))
    nv = len(verts)
    idx = np.concatenate(indices).astype(np.uint32) if indices else np.zeros(0, np.uint32)
    if nv == 0 or len(idx) == 0:
        print("  kosong:", src)
        return None
    V = np.zeros((nv, 8), np.float32)
    for k, (p, n, u) in enumerate(verts):
        V[k, 0:3] = p
        V[k, 3:6] = n
        V[k, 6:8] = u
    vbytes = V.tobytes()
    ibytes = idx.tobytes()
    tex = tex_data or b""
    tex2 = tex2_data or b""
    # buffer: [verts][idx][tex0][tex1]
    off_idx = len(vbytes)
    off_tex = off_idx + len(ibytes)
    off_tex2 = off_tex + len(tex)
    buf = vbytes + ibytes + tex + tex2
    views = [
        dict(buffer=0, byteOffset=0, byteLength=len(vbytes), target=34962),
        dict(buffer=0, byteOffset=off_idx, byteLength=len(ibytes), target=34963),
    ]
    accessors = [
        dict(bufferView=0, componentType=5126, count=nv, type="VEC3",
             min=[float(x) for x in V[:, 0:3].min(0)], max=[float(x) for x in V[:, 0:3].max(0)]),
        dict(bufferView=0, componentType=5126, count=nv, type="VEC3", byteOffset=12),
        dict(bufferView=0, componentType=5126, count=nv, type="VEC2", byteOffset=24),
        dict(bufferView=1, componentType=5125, count=len(idx), type="SCALAR"),
    ]
    # accessor dengan byteOffset di dalam view bertarget: glTF butuh byteOffset % 4 == 0 -> oke (12,24)
    attrs = dict(POSITION=0, NORMAL=1, TEXCOORD_0=2)
    prim = dict(attributes=attrs, indices=3, mode=4, material=0)
    out = dict(asset=dict(version="2.0", generator="nusa pack_assets"),
               scene=0, scenes=[dict(nodes=[0])], nodes=[dict(name="root", mesh=0)],
               meshes=[dict(name="m", primitives=[prim])],
               materials=[dict(name="m", pbrMetallicRoughness=dict(
                   baseColorFactor=[1, 1, 1, 1], metallicFactor=0.0, roughnessFactor=0.9))],
               accessors=accessors, bufferViews=views, buffers=[dict(byteLength=len(buf))])
    # metadata khusus loader Nusa Rush (sub-mesh + pivot roda)
    out["nusa"] = dict(prims=[[p["off"], p["count"], p["name"], p["pivot"], p["mat"]]
                              for p in prims])
    samples = [dict(magFilter=9729, minFilter=9987, wrapS=10497, wrapT=10497)]
    if tex:
        views.append(dict(buffer=0, byteOffset=off_tex, byteLength=len(tex)))
        out["images"] = [dict(bufferView=2, mimeType="image/jpeg")]
        out["textures"] = [dict(source=0, sampler=0)]
        out["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"] = dict(index=0)
    if tex2:
        views.append(dict(buffer=0, byteOffset=off_tex2, byteLength=len(tex2)))
        out["images"] = out.get("images", []) + [dict(bufferView=len(views) - 1,
                                                      mimeType="image/jpeg")]
        out["textures"] = out.get("textures", []) + [dict(source=len(out["images"]) - 1,
                                                          sampler=0)]
        out["materials"].append(dict(name="m1", pbrMetallicRoughness=dict(
            baseColorFactor=[1, 1, 1, 1], metallicFactor=0.0, roughnessFactor=0.95,
            baseColorTexture=dict(index=len(out["textures"]) - 1))))
    out["samplers"] = samples
    # tulis glb
    jsb = json.dumps(out, separators=(",", ":")).encode()
    while len(jsb) % 4:
        jsb += b" "
    while len(buf) % 4:
        buf += b"\x00"
    total = 12 + 8 + len(jsb) + 8 + len(buf)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, total))
        f.write(struct.pack("<II", len(jsb), 0x4E4F534A))
        f.write(jsb)
        f.write(struct.pack("<II", len(buf), 0x004E4942))
        f.write(buf)
    return dict(tris=len(idx) // 3, verts=nv, size=os.path.getsize(dst),
                tex=len(tex), height=float(V[:, 1].max() - V[:, 1].min()))


def main():
    os.makedirs(OUT, exist_ok=True)
    report = {}

    def do(src, rel, **kw):
        dst = os.path.join(OUT, rel)
        r = bake_glb(src, dst, **kw)
        report[rel] = r
        if r:
            print(f"  {rel:42s} tri={r['tris']:6d} v={r['verts']:6d} "
                  f"tex={r['tex']//1024:4d}KB tot={r['size']/1024:7.1f}KB h={r['height']:.1f}m")
        return r

    print("== jalan (badan jalan + trotoar dipisah supaya 1 draw call per bagian)")
    for k in ("desa", "kota", "tanah"):
        src = os.path.join(GEN, f"roads_{k}.glb")
        do(src, f"models/roads_{k}.glb", maxpx=1024, mat_filter=0)
        # trotoar hanya ada di desa & kota
        if k != "tanah":
            try:
                js_t, _ = read_glb(src)
                has_t = len(js_t.get("materials", [])) > 1
            except Exception:
                has_t = False
            if has_t:
                do(src, f"models/roads_{k}_trotoar.glb", maxpx=1024, mat_filter=1)
    print("== pohon")
    for n in ("oak", "spruce", "sakura", "palm"):
        do(os.path.join(ROOT, f"assets/gemini1-pohon-fixed/{n}.glb"), f"models/trees/{n}.glb",
           maxpx=TEX_POHON)
        do(os.path.join(ROOT, f"assets/gemini1-pohon-fixed/{n}_lod1.glb"),
           f"models/trees/{n}_lod1.glb", maxpx=TEX_LOD)
    print("== rumah desa")
    for s in "ABCD":
        for i in (1, 2, 3):
            do(os.path.join(ROOT, f"assets/desa-{s}-fixed/desa_{s}_{i}.glb"),
               f"models/desa/desa_{s}_{i}.glb")
            do(os.path.join(ROOT, f"assets/desa-{s}-fixed/desa_{s}_{i}_lod1.glb"),
               f"models/desa/desa_{s}_{i}_lod1.glb", maxpx=TEX_LOD)
    print("== gedung kota")
    for s in "ABCD":
        for i in (1, 2, 3):
            do(os.path.join(ROOT, f"assets/kota-{s}-fixed/kota_{s}_{i}.glb"),
               f"models/kota/kota_{s}_{i}.glb")
            do(os.path.join(ROOT, f"assets/kota-{s}-fixed/kota_{s}_{i}_lod1.glb"),
               f"models/kota/kota_{s}_{i}_lod1.glb", maxpx=TEX_LOD)
    print("== mobil (pivot roda dipertahankan)")
    js_car, _ = read_glb(os.path.join(ROOT, "assets/lamborghini.glb"))
    wheels = {}
    for n in js_car["nodes"]:
        nm = (n.get("name") or "").lower()
        if "wheel" in nm:
            wheels[n["name"]] = n.get("translation", [0, 0, 0])
    r = bake_glb(os.path.join(ROOT, "assets/lamborghini.glb"), os.path.join(OUT, "models/mobil.glb"),
                 wheel_pivots=wheels)
    report["models/mobil.glb"] = r
    print("  models/mobil.glb tri=%d size=%.1fKB pivot roda=%s" %
          (r["tris"], r["size"] / 1024, list(wheels.keys())))

    print("== data dunia")
    for f in ("island.bin", "roads.json", "props.bin"):
        src = os.path.join(GEN, f)
        dst = os.path.join(OUT, "generated", f)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(src, "rb") as a, open(dst, "wb") as b:
            b.write(a.read())
        print(f"  generated/{f:16s} {os.path.getsize(dst)/1024:8.1f} KB")

    # spawn mobil di jalan kota terdekat dari pusat kota
    roads = json.load(open(os.path.join(GEN, "roads.json")))
    best = None
    for j in roads["jalan"]:
        if j["jenis"] != "kota":
            continue
        for k in range(0, len(j["titik"]) - 1, 3):
            p, q = j["titik"][k], j["titik"][k + 1]
            d = (p[0] + 1350) ** 2 + (p[1] - 1300) ** 2
            if best is None or d < best[0]:
                best = (d, p, q)
    spawn_path = os.path.join(OUT, "generated", "car_spawn.json")
    keep_existing = os.path.exists(spawn_path)
    if best:
        _, p, q = best
        import math
        yaw = math.atan2(q[0] - p[0], q[1] - p[1])
        spawn = dict(x=p[0], y=p[2], z=p[1], yaw=yaw)
    else:
        spawn = dict(x=0, y=0, z=0, yaw=0)
    if keep_existing:
        print("  spawn: dipakai hasil tools/fix_scene.py")
    else:
        json.dump(spawn, open(spawn_path, "w"))
        print("  spawn:", spawn)
    print("  (pakai tools/fix_scene.py bila ingin memilih titik spawn lain)")

    json.dump(report, open(os.path.join(OUT, "assets_manifest.json"), "w"), indent=1)
    total = sum(f["size"] for f in report.values() if f)
    print(f"TOTAL model: {total/1e6:.2f} MB, berkas: {len(report)}")


if __name__ == "__main__":
    main()
