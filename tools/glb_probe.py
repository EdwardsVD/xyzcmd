import struct, json, sys
def load(p):
    d=open(p,'rb').read()
    jl,jt=struct.unpack('<II', d[12:20]); js=json.loads(d[20:20+jl])
    off=20+jl; bl,bt=struct.unpack('<II', d[off:off+8]); bin_=d[off+8:off+8+bl]
    return js, bin_
def accessor(js,bin_,i):
    a=js['accessors'][i]; bv=js['bufferViews'][a['bufferView']]
    ctype={5120:('b',1),5121:('B',1),5122:('h',2),5123:('H',2),5125:('I',4),5126:('f',4)}[a['componentType']]
    ncomp={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[a['type']]
    stride=bv.get('byteStride', ctype[1]*ncomp)
    base=bv.get('byteOffset',0)+a.get('byteOffset',0)
    out=[]
    for k in range(a['count']):
        o=base+k*stride
        out.append(struct.unpack_from('<'+ctype[0]*ncomp, bin_, o))
    return out
if __name__=='__main__':
    js,bin_=load(sys.argv[1])
    print('meshes',len(js['meshes']),'nodes',len(js['nodes']),'mats',len(js.get('materials',[])))
    for i,n in enumerate(js['nodes'][:5]): print(' node',i,n.get('name'),{k:v for k,v in n.items() if k in('translation','rotation','scale','mesh','children')})
    for mi,m in enumerate(js['meshes']):
        for pi,pr in enumerate(m['primitives']):
            at=pr['attributes']
            pos=accessor(js,bin_,at['POSITION'])
            xs=[p[0] for p in pos]; ys=[p[1] for p in pos]; zs=[p[2] for p in pos]
            print(f' mesh{mi} prim{pi} attrs={list(at.keys())} n={len(pos)} bbox x[{min(xs):.1f},{max(xs):.1f}] y[{min(ys):.1f},{max(ys):.1f}] z[{min(zs):.1f},{max(zs):.1f}] idx={js["accessors"][pr["indices"]]["count"] if "indices" in pr else None} mat={pr.get("material")}')
            if 'COLOR_0' in at:
                c=accessor(js,bin_,at['COLOR_0']); print('   COLOR_0 sample',[tuple(round(v,3) for v in c[k]) for k in (0,1,2)], 'type',js['accessors'][at['COLOR_0']]['componentType'])
            mat=js['materials'][pr.get('material',0)] if js.get('materials') else {}
            print('   material',json.dumps(mat)[:220])
