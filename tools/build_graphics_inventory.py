"""Build SC2's local startup inventory from assets/XBE, without running gameplay.

Run with --disc <Xbox ISO> --assets <guest root.olk> --output <graphics-cache>.
The output contains game-derived shader code and must stay with the user's files.
"""
import argparse
import hashlib
import itertools
import json
import mmap
from pathlib import Path
import struct
import sys

SUPPORTED_XBE = 'd7c2e1fc993350821e3f7e13cfd1c9d4ba6a7bf91011005bdd1f843899efb730'
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/xboxrecomp/tools'))

RECIPE_SIZE, PIXEL_OFFSET = 3052, 2196

def checksum(data):
    h = 1469598103934665603
    for b in data:
        h = ((h ^ b) * 1099511628211) & ((1 << 64) - 1)
    return h

def write_catalog(path, records):
    temp = path.with_suffix('.tmp')
    with temp.open('wb') as f:
        for r in sorted(records):
            f.write(struct.pack('<IIQ', 0x32574353, len(r), checksum(r)))
            f.write(r)
    temp.replace(path)

class Xbe:
    def __init__(self, data):
        if data[:4] != b'XBEH':
            raise ValueError('not an Xbox executable')
        self.data = data
        base = struct.unpack_from('<I', data, 0x104)[0]
        count, table = struct.unpack_from('<II', data, 0x11c)
        self.sections = [struct.unpack_from('<6I', data, table-base+56*i) for i in range(count)]
    def read(self, address, size):
        for flags, va, vs, raw, rs, name in self.sections:
            if va <= address and address+size <= va+rs:
                return self.data[raw+address-va:raw+address-va+size]
        raise ValueError('unmapped XBE range %#x+%d' % (address,size))
    def words(self, address, count):
        return struct.unpack('<%dI' % count, self.read(address,count*4))

def pixel_combiner(words):
    p = bytearray(856)
    struct.pack_into('<I',p,0,2)
    comb = [words[53], *words[34:42], *words[45:53], *words[:8], *words[26:34],
            words[8], words[9], words[54], words[56], words[55], words[42]]
    struct.pack_into('<39I',p,184,*comb)
    return p

def recipe(fvf, pixel, visual=0, program=b''):
    if len(program)%16 or len(program)>136*16:
        raise ValueError('invalid vertex program')
    r=bytearray(RECIPE_SIZE)
    struct.pack_into('<II',r,0,fvf,len(program)//16)
    r[16]=visual
    r[20:20+len(program)]=program
    r[PIXEL_OFFSET:]=pixel
    return bytes(r)

def scan_models(data, label):
    at=0; models=materials=0; blends=set(); unsupported=[]
    while True:
        at=data.find(b'VMX.',at)
        if at<0: break
        base=at; at+=4
        if data[base+4:base+7]!=bytes((4,16,13)): continue
        def words(offset,n=1):
            return struct.unpack_from('<%dI'%n,data,base+offset)
        count=struct.unpack_from('<H',data,base+20)[0]
        ptr=words(28)[0]
        if not count or ptr<0x4c or base+ptr+80*count>len(data):
            raise ValueError('invalid VMX material table in %s at %#x'%(label,base))
        models+=1; materials+=count
        for i in range(count):
            m=words(ptr+i*80,8); blends.add(m[1]&15)
            if m[2] and m[3] and ((m[1]>>4)&15) not in (1,2,3,4):
                unsupported.append({'asset':label,'offset':base,'material':i,'flags':m[1]})
    return dict(asset=label,models=models,materials=materials,blend_modes=sorted(blends),unsupported=unsupported)

def vertex_programs(xbe):
    # Four records used by the effect renderer's shader initialization table.
    # Validate every declaration and program header rather than accepting a
    # coincidental microcode signature elsewhere in the executable.
    result=[]
    for index in range(4):
        code,decl=xbe.words(0x19567c+index*20,2)
        tag,count=struct.unpack('<HH',xbe.read(code,4))
        if tag!=0x2078 or not 0<count<=136: raise ValueError('unsupported effect shader table')
        mapping={i:i for i in range(16)}; fvf=0; slots=[(0,0x32,2),(2,0x32,16),(3,0x40,64),(9,0x22,256)]
        used=set()
        for token in xbe.words(decl,16):
            if token==0xffffffff: break
            if token==0x20000000: continue
            if token>>29!=2: raise ValueError('unsupported effect vertex declaration')
            reg=token&15; typ=(token>>16)&255
            match=next((s for s in slots if s[0] not in used and s[1]==typ),None)
            if not match: raise ValueError('unsupported effect vertex input')
            used.add(match[0]); mapping[reg]=match[0]; fvf|=match[2]
        else: raise ValueError('unterminated vertex declaration')
        w=list(xbe.words(code+4,count*4))
        for i in range(count):
            a=w[i*4+1];w[i*4+1]=(a&~(15<<9))|(mapping[(a>>9)&15]<<9)
        if not w[-1]&1: raise ValueError('unterminated vertex program')
        result.append((fvf,struct.pack('<%dI'%len(w),*w)))
    return result

def build(xbe):
    recipes=set()
    # Five definitions selected by the material routine at 0x240c0. Prepare
    # the finite UV-transform and alpha-kill variants, including settings
    # changed by the stage/effect passes rather than the material itself.
    for a in range(0x193f60,0x194410,0xf0):
        w=xbe.words(a,60)
        if w[53] not in (0x11101,0x11102) or w[54] not in (0,1,0x21):
            raise ValueError('unsupported model shader definitions')
        active=[i for i in range(4) if (w[54]>>(5*i))&31==1]
        for bits in itertools.product((0,1),repeat=2*len(active)):
            p=pixel_combiner(w)
            for j,stage in enumerate(active):
                p[8+44*stage+40:8+44*stage+43]=bytes((1,bits[j],bits[j+len(active)]))
            for visual in (0,1,2):recipes.add(recipe(0x152,p,visual))
    # UI/effect fixed-function state families established by 0x314d0,
    # 0x31570, 0x31670, 0x33840 and 0x8ffc0, plus SDK inherited alpha state.
    layouts=[(v,b'') for v in (2,0x42,0x44,0x104,0x112,0x142,0x144,0x152)] + vertex_programs(xbe)
    for fvf,code in layouts:
        # The fourth legacy effect program reads undeclared v1. The current
        # renderer cannot translate it; report this exclusion explicitly.
        if code and fvf==2: continue
        visuals=(0,) if code or (fvf&0xe)==4 else (0,1)
        for spec in (0,1):
            p=bytearray(856);struct.pack_into('<I',p,0,1);p[340]=spec
            for visual in visuals:recipes.add(recipe(fvf,p,visual,code))
            # SC2 disables the fixed-function stage for untextured layouts.
            if not fvf&0xf00: continue
            for op,alpha2,kill,transform in itertools.product((4,5),(0,1),(0,1),(0,1)):
                if transform and (code or (fvf&0xe)==4 or not fvf&0xf00): continue
                q=bytearray(p);struct.pack_into('<I',q,4,1)
                struct.pack_into('<10I',q,8,op,0,2,0,op,0,2,alpha2,1,0)
                q[48:51]=bytes((bool(fvf&0xf00),kill,transform))
                for visual in visuals: recipes.add(recipe(fvf,q,visual,code))
    return recipes

def main():
    from xiso.xdvdfs import Xiso
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--disc',required=True,type=Path)
    ap.add_argument('--assets',action='append',default=[],type=Path)
    ap.add_argument('--output',required=True,type=Path)
    args=ap.parse_args(); args.output.mkdir(parents=True,exist_ok=True)
    with Xiso(str(args.disc)) as iso:
        entry=iso.find('default.xbe'); root=iso.find('root.olk')
        if not entry or not root: raise ValueError('disc is missing default.xbe/root.olk')
        executable=iso.read(entry)
        if hashlib.sha256(executable).hexdigest()!=SUPPORTED_XBE:
            raise ValueError("This inventory extractor supports the verified US Xbox executable only")
        data=iso.read(root)
        reports=[scan_models(data,'disc/root.olk')]; del data
    for path in args.assets:
        with path.open('rb') as f:
            with mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as data:
                reports.append(scan_models(data,str(path)))
    if not all(r['models'] for r in reports):raise ValueError('no model assets found')
    if any(r['unsupported'] for r in reports):raise ValueError('unsupported material selection; inventory not installed')
    recipes=build(Xbe(executable))
    report={'schema':1,'discovery':'static executable and asset inspection; no gameplay capture',
            'xbe_sha256':hashlib.sha256(executable).hexdigest(),'recipes':len(recipes),'assets':reports,
            'scope':'five model pixel definitions, three supported effect vertex programs (four audited), fixed-function UI/effect families',
            'limitations':['Dynamic render states and unrecognized render paths are not proven exhaustive.',
            'Legacy effect program 0x1955a8 reads undeclared v1 and is unsupported by the current renderer; excluded.']}
    write_catalog(args.output/'offline-shaders-v1.bin',recipes)
    (args.output/'offline-inventory.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()
