import os
# Match functions between the Steam and Epic KH1 executables and derive address maps.
import sys, re, struct, hashlib, difflib, collections, pickle
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe import PE
SC = os.environ.get('EPICMAP_WORK', 'work') + '/'
BASE = 0x140000000
ADDR = re.compile(r'0x14[0-9a-f]{7}\b')
RIP = re.compile(r'rip[+-]0x[0-9a-f]+')
DISP = re.compile(r'\+0x([0-9a-f]+)\]')

def load_asm(path):
    ins = []
    for line in open(path):
        if not line.startswith('   14'):
            continue
        a, _, t = line.partition(':')
        t = t.strip()
        if not t: continue
        ins.append((int(a, 16) - BASE, t))
    return ins

def funcs(p):
    n, va, vs, ro, rs = [s for s in p.secs if s[0] == '.pdata'][0]
    out = []
    for i in range(0, vs, 12):
        b, e, u = struct.unpack_from('<III', p.d, ro + i)
        if b == 0: break
        out.append((b, e))
    return out

def split(ins, fl):
    import bisect
    addrs = [a for a, _ in ins]
    res = []
    for b, e in fl:
        i = bisect.bisect_left(addrs, b); j = bisect.bisect_left(addrs, e)
        body = ins[i:j]
        norm = []; refs = []
        for a, t in body:
            r = [int(x, 16) - BASE for x in ADDR.findall(t)]
            t2 = ADDR.sub('@', t)
            t2 = RIP.sub('rip+@', t2)
            for m in DISP.finditer(t2):
                v = int(m.group(1), 16)
                if 0x1000 <= v < 0x2f90000 and 'rsp' not in t2 and 'rbp' not in t2: r.append(v)
            t2 = DISP.sub(lambda m: '+@]' if 0x1000 <= int(m.group(1), 16) < 0x2f90000 and 'rsp' not in t2 and 'rbp' not in t2 else m.group(0), t2)
            norm.append(t2); refs.append(r)
        h = hashlib.md5('\n'.join(norm).encode()).hexdigest()
        offs = [a - b for a, _ in body]
        res.append(dict(b=b, e=e, h=h, offs=offs, refs=refs, ok=(len(body) > 0 and body[0][0] == b)))
    return res

S, E = PE(SC + 'steam.exe'), PE(SC + 'epic.exe')
fs = split(load_asm(SC + 'steam.asm'), funcs(S))
fe = split(load_asm(SC + 'epic.asm'), funcs(E))
# 1) sequence alignment on hashes, 2) unique-hash matching for moved functions
sm = difflib.SequenceMatcher(None, [f['h'] for f in fs], [f['h'] for f in fe], autojunk=False)
pairs = {}
for tag, i1, i2, j1, j2 in sm.get_opcodes():
    if tag == 'equal':
        for k in range(i2 - i1): pairs[i1 + k] = j1 + k
cs = collections.Counter(f['h'] for f in fs); ce = collections.Counter(f['h'] for f in fe)
hidx = {f['h']: j for j, f in enumerate(fe)}
for i, f in enumerate(fs):
    if i not in pairs and cs[f['h']] == 1 and ce[f['h']] == 1:
        pairs[i] = hidx[f['h']]
print('functions steam', len(fs), 'epic', len(fe), 'matched identical', len(pairs))
# code map: per matched function
code = []
datapairs = collections.Counter()
for i, j in pairs.items():
    a, b = fs[i], fe[j]
    if a['offs'] != b['offs']: continue
    code.append((a['b'], a['e'], b['b'] - a['b']))
    for ra, rb in zip(a['refs'], b['refs']):
        for x, y in zip(ra, rb):
            datapairs[(x, y)] += 1
code.sort()
pickle.dump(dict(code=code, datapairs=datapairs, fs=fs, fe=fe, pairs=pairs), open(SC + 'map.pkl', 'wb'))
print('code ranges', len(code), 'ref pairs', len(datapairs))
