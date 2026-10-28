#!/usr/bin/env python3
import struct,sys,hashlib
from pathlib import Path
b=Path(sys.argv[1]).read_bytes()
assert b[:4]==b'\0PBP' and len(b)>1000
v=struct.unpack_from('<I',b,4)[0];o=struct.unpack_from('<8I',b,8)
assert v==0x10000 and o[0]>=40 and list(o)==sorted(o) and o[-1]<=len(b)
assert b[o[0]:o[0]+4]==b'\0PSF'
assert b[o[1]:o[1]+8]==b'\x89PNG\r\n\x1a\n'
e=b[o[6]:o[7]]
assert e[:4]==b'~PSP' and len(e)>336, 'Release must contain encrypted PSP PRX'
assert 'ДАЛЬНИЙ РЕЙС'.encode() in b[o[0]:o[1]]
print('PASS: PBP + PARAM.SFO + PNG icon + encrypted PSP PRX')
print('SHA256:',hashlib.sha256(b).hexdigest(),'bytes:',len(b))
