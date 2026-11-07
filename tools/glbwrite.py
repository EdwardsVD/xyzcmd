#!/usr/bin/env python3
"""glbwrite.py - penulis GLB (glTF 2.0 binary) minimal, dipakai pipeline aset Nusa Rush.

Fitur: primitif POSITION/NORMAL/TEXCOORD_0/COLOR_0, material PBR baseColorTexture,
PNG tertanam lewat bufferView (bukan data URI), node & scene.
"""
import json
import struct
import numpy as np
from PIL import Image
import io


class Mesh:
    def __init__(self, name, pos, normals=None, uvs=None, colors=None, indices=None,
                 material=0):
        self.name = name
        self.pos = np.asarray(pos, dtype=np.float32)
        self.normals = None if normals is None else np.asarray(normals, dtype=np.float32)
        self.uvs = None if uvs is None else np.asarray(uvs, dtype=np.float32)
        self.colors = None if colors is None else np.asarray(colors, dtype=np.float32)
        self.indices = None if indices is None else np.asarray(indices, dtype=np.uint32)
        self.material = material


class GLBScene:
    def __init__(self):
        self.meshes = []
        self.materials = []      # dict: name, base_color, texture(png bytes) or None, rough, metal
        self.nodes = []          # dict: name, translation/rotation/scale, mesh index

    def add_material(self, name, base_color=(1, 1, 1, 1), texture=None, rough=0.8, metal=0.0,
                     double_sided=False, emissive=None):
        self.materials.append(dict(name=name, base_color=base_color, texture=texture,
                                   rough=rough, metal=metal, double_sided=double_sided,
                                   emissive=emissive))
        return len(self.materials) - 1

    def add_mesh(self, m):
        self.meshes.append(m)
        return len(self.meshes) - 1

    def add_node(self, name, mesh=None, t=(0, 0, 0), s=(1, 1, 1), r=None):
        n = dict(name=name, mesh=mesh, t=t, s=s, r=r)
        self.nodes.append(n)
        return len(self.nodes) - 1

    def write(self, path):
        bin_ = bytearray()
        views = []
        accessors = []

        def add_view(data, target=None):
            while len(bin_) % 4:
                bin_.append(0)
            off = len(bin_)
            bin_.extend(data)
            v = dict(buffer=0, byteOffset=off, byteLength=len(data))
            if target:
                v["target"] = target
            views.append(v)
            return len(views) - 1

        images = []
        textures = []
        for mat in self.materials:
            if mat["texture"] is not None:
                vi = add_view(mat["texture"])
                images.append(dict(bufferView=vi, mimeType="image/png"))
                textures.append(dict(source=len(images) - 1, sampler=0))

        gl_meshes = []
        for m in self.meshes:
            attrs = {}
            pos = m.pos.astype(np.float32)
            vi = add_view(pos.tobytes(), 34962)
            accessors.append(dict(bufferView=vi, componentType=5126, count=len(pos), type="VEC3",
                                  min=[float(x) for x in pos.min(0)], max=[float(x) for x in pos.max(0)]))
            attrs["POSITION"] = len(accessors) - 1
            if m.normals is not None:
                vi = add_view(m.normals.astype(np.float32).tobytes(), 34962)
                accessors.append(dict(bufferView=vi, componentType=5126, count=len(m.normals), type="VEC3"))
                attrs["NORMAL"] = len(accessors) - 1
            if m.uvs is not None:
                vi = add_view(m.uvs.astype(np.float32).tobytes(), 34962)
                accessors.append(dict(bufferView=vi, componentType=5126, count=len(m.uvs), type="VEC2"))
                attrs["TEXCOORD_0"] = len(accessors) - 1
            if m.colors is not None:
                vi = add_view(m.colors.astype(np.float32).tobytes(), 34962)
                accessors.append(dict(bufferView=vi, componentType=5126, count=len(m.colors), type="VEC4"))
                attrs["COLOR_0"] = len(accessors) - 1
            prim = dict(attributes=attrs, mode=4, material=m.material)
            if m.indices is not None:
                idx = m.indices.astype(np.uint32)
                vi = add_view(idx.tobytes(), 34963)
                accessors.append(dict(bufferView=vi, componentType=5125, count=len(idx), type="SCALAR"))
                prim["indices"] = len(accessors) - 1
            gl_meshes.append(dict(name=m.name, primitives=[prim]))

        gl_materials = []
        ti = 0
        for mat in self.materials:
            pbr = dict(baseColorFactor=list(mat["base_color"]),
                       metallicFactor=mat["metal"], roughnessFactor=mat["rough"])
            if mat["texture"] is not None:
                pbr["baseColorTexture"] = dict(index=ti)
                ti += 1
            mm = dict(name=mat["name"], pbrMetallicRoughness=pbr,
                      doubleSided=bool(mat["double_sided"]))
            if mat["emissive"]:
                mm["emissiveFactor"] = list(mat["emissive"])
            gl_materials.append(mm)

        gl_nodes = []
        for n in self.nodes:
            d = dict(name=n["name"])
            if n["mesh"] is not None:
                d["mesh"] = n["mesh"]
            if n["t" ] != (0, 0, 0):
                d["translation"] = list(n["t"])
            if n["s"] != (1, 1, 1):
                d["scale"] = list(n["s"])
            if n["r"]:
                d["rotation"] = list(n["r"])
            gl_nodes.append(d)

        while len(bin_) % 4:
            bin_.append(0)
        js = dict(
            asset=dict(version="2.0", generator="nusa-rush glbwrite.py"),
            scene=0,
            scenes=[dict(nodes=list(range(len(gl_nodes))))] if gl_nodes else [dict(nodes=[])],
            nodes=gl_nodes,
            meshes=gl_meshes,
            materials=gl_materials,
            accessors=accessors,
            bufferViews=views,
            buffers=[dict(byteLength=len(bin_))],
        )
        if images:
            js["images"] = images
            js["textures"] = textures
            js["samplers"] = [dict(magFilter=9729, minFilter=9987, wrapS=10497, wrapT=10497)]
        jsb = json.dumps(js, separators=(",", ":")).encode()
        while len(jsb) % 4:
            jsb += b" "
        total = 12 + 8 + len(jsb) + 8 + len(bin_)
        with open(path, "wb") as f:
            f.write(struct.pack("<4sII", b"glTF", 2, total))
            f.write(struct.pack("<II", len(jsb), 0x4E4F534A))
            f.write(jsb)
            f.write(struct.pack("<II", len(bin_), 0x004E4942))
            f.write(bytes(bin_))
        return path


def png_bytes(img):
    b = io.BytesIO()
    img.save(b, format="PNG", optimize=True)
    return b.getvalue()


def srgb_to_linear(c):
    c = np.asarray(c, dtype=np.float32)
    return np.where(c <= 0.04045, c / 12.92, np.power((c + 0.055) / 1.055, 2.4))


def probe(path):
    """Ringkasan glb: verifikasi magic, jumlah segitiga, tinggi."""
    d = open(path, "rb").read()
    jl, jt = struct.unpack("<II", d[12:20])
    js = json.loads(d[20:20 + jl])
    tris = 0
    for m in js.get("meshes", []):
        for pr in m["primitives"]:
            if "indices" in pr:
                tris += js["accessors"][pr["indices"]]["count"] // 3
            else:
                tris += js["accessors"][pr["attributes"]["POSITION"]]["count"] // 3
    ys = [(a["min"][1], a["max"][1]) for a in js["accessors"]
          if a.get("type") == "VEC3" and "min" in a]
    return dict(magic=d[:4], size=len(d), tris=tris, imgs=len(js.get("images", [])),
                meshes=len(js.get("meshes", [])), ymin=min(y[0] for y in ys) if ys else 0,
                ymax=max(y[1] for y in ys) if ys else 0)
