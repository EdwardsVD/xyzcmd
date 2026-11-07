#!/usr/bin/env python3
"""Ekstrak heightmap, warna vertex, dan peta biome dari pulau_P2.glb.
Keluaran: assets/generated/island.bin (+ preview island_top.png, biome.png)"""
import struct, json, sys, os
import numpy as np
from glb_probe import load, accessor
from PIL import Image

GLB = sys.argv[1] if len(sys.argv) > 1 else 'assets/pulau/pulau_P2.glb'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'assets/generated'
PREV = 'output/preview'
os.makedirs(OUT, exist_ok=True); os.makedirs(PREV, exist_ok=True)

# swatch biome dari generator pulau (sRGB)
SW = {1:(238,224,182), 2:(128,130,126), 3:(178,196,96), 4:(34,80,42), 5:(116,110,60), 6:(34,98,132)}
NAMES = {0:'Laut',1:'Pantai',2:'Kota',3:'Desa',4:'Hutan',5:'Gunung',6:'Danau'}

js, bin_ = load(GLB)
prim = js['meshes'][0]['primitives'][0]
pos = np.array(accessor(js, bin_, prim['attributes']['POSITION']), dtype=np.float64)
col_lin = np.array(accessor(js, bin_, prim['attributes']['COLOR_0']), dtype=np.float64)[:, :3]
N = int(round(np.sqrt(len(pos))))
assert N*N == len(pos)
x0, x1 = pos[:,0].min(), pos[:,0].max(); z0, z1 = pos[:,2].min(), pos[:,2].max()
gi = np.rint((pos[:,0]-x0)/(x1-x0)*(N-1)).astype(int)
gj = np.rint((pos[:,2]-z0)/(z1-z0)*(N-1)).astype(int)
H = np.zeros((N,N), np.float32); C = np.zeros((N,N,3), np.float32)
H[gj,gi] = pos[:,1]; C[gj,gi] = col_lin
assert (H != 0).sum() > N*N*0.9

def lin2srgb(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c*12.92, 1.055*np.power(c, 1/2.4)-0.055)
sRGB = (lin2srgb(C)*255).astype(np.float32)

# klasifikasi biome: jarak warna terdekat ke swatch
keys = np.array([SW[i] for i in sorted(SW)], np.float32)
lab = np.zeros((N,N), np.uint8)
d = ((sRGB[...,None,:] - keys[None,None,:,:])**2).sum(-1)
lab = (np.argmin(d, axis=-1)+1).astype(np.uint8)
land_mask = H > 0.5
lab[(~land_mask)] = 0
# laut dalam / danau kebiruan -> 0 kalau bukan daratan
print('grid %d, x[%.0f,%.0f] z[%.0f,%.0f], h[%.1f,%.1f]' % (N,x0,x1,z0,z1,H.min(),H.max()))
tot = land_mask.sum()
for k in range(0,7):
    print('  %-7s %6.2f%%' % (NAMES[k], 100.0*(lab==k).sum()/(tot if k else N*N)))

# tepi warna wilayah untuk visual
vis = np.zeros((N,N,3), np.uint8)
for k in range(0,7):
    vis[lab==k] = SW.get(k,(12,48,105))
Img = Image.fromarray(vis).resize((768,768), Image.NEAREST)
Img.save(os.path.join(PREV,'biome_top.png'))

# hillshade top view
hs = np.zeros_like(H)
dy, dx = np.gradient(H, (z1-z0)/(N-1), (x1-x0)/(N-1))
nz = 1/np.sqrt(dx*dx+dy*dy+1); nx=-dx*nz; ny=-dy*nz
L = np.array([-0.4,0.75,-0.5]); L/=np.linalg.norm(L)
sh = np.clip(nx*L[0]+ny*L[1]+nz*L[2], 0, 1)
base = vis/255.0
top = np.clip(base*(0.45+0.75*sh[...,None]), 0, 1)
Image.fromarray((top*255).astype(np.uint8)).resize((900,900), Image.LANCZOS).save(os.path.join(PREV,'island_top.png'))

# --- tulis aset biner ---
# header: magic 'NISA', ver, N, x0,x1,z0,z1 (f32) | tinggi f32 N*N | warna sRGB u8 N*N*3 | biome u8 N*N
nh, nw = H.shape
hdr = struct.pack('<4sII6f', b'NISA', 1, N, x0, x1, z0, z1, 0.0, 0.0)
with open(os.path.join(OUT,'island.bin'),'wb') as f:
    f.write(hdr)
    f.write(H.astype('<f4').tobytes())
    f.write(vis.astype(np.uint8).tobytes())     # warna sRGB untuk terrain
    f.write(lab.astype(np.uint8).tobytes())     # index biome
print('island.bin', os.path.getsize(os.path.join(OUT,'island.bin')), 'byte')
np.save(os.path.join(OUT,'_h.npy'), H); np.save(os.path.join(OUT,'_b.npy'), lab)
json.dump({'N':N,'x0':x0,'x1':x1,'z0':z0,'z1':z1,'biome_pct':{NAMES[k]: round(100.0*(lab==k).sum()/(tot if k else N*N),2) for k in range(7)}},
          open(os.path.join(OUT,'island_summary.json'),'w'), indent=1)
