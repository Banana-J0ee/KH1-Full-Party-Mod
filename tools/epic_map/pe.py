import struct,sys
class PE:
    def __init__(s,path):
        s.d=open(path,'rb').read()
        e=struct.unpack_from('<I',s.d,0x3c)[0]
        n=struct.unpack_from('<H',s.d,e+6)[0]
        osz=struct.unpack_from('<H',s.d,e+20)[0]
        s.base=struct.unpack_from('<Q',s.d,e+24+24)[0]
        s.secs=[]
        o=e+24+osz
        for i in range(n):
            name=s.d[o:o+8].rstrip(b'\0').decode()
            vs,va,rs,ro=struct.unpack_from('<IIII',s.d,o+8)
            s.secs.append((name,va,vs,ro,rs)); o+=40
    def off(s,rva):
        for n,va,vs,ro,rs in s.secs:
            if va<=rva<va+max(vs,rs):
                if rva-va>=rs: return None
                return ro+rva-va
        return None
    def rd(s,rva,n):
        o=s.off(rva)
        if o is None: return None
        return s.d[o:o+n]
    def u32(s,rva): return struct.unpack('<I',s.rd(rva,4))[0]
    def u64(s,rva): return struct.unpack('<Q',s.rd(rva,8))[0]
    def cstr(s,rva):
        o=s.off(rva); e=s.d.index(b'\0',o); return s.d[o:e].decode('latin1')
