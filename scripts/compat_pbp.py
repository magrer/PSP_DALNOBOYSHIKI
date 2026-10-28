#!/usr/bin/env python3
"""Retain the PSP-tested metadata while updating the icon and encrypted game."""
from pathlib import Path
import struct,sys
src=Path(sys.argv[1]).read_bytes();sfo=Path(sys.argv[2]).read_bytes();icon=Path(sys.argv[3]).read_bytes();out=Path(sys.argv[4])
assert src[:4]==b'\0PBP' and sfo[:4]==b'\0PSF' and icon[:8]==b'\x89PNG\r\n\x1a\n'
o=struct.unpack_from('<8I',src,8);sections=[sfo,icon]+[src[o[i]:o[i+1]] for i in range(2,7)]+[src[o[7]:]]
assert sections[6].startswith(b'~PSP') and len(sections)==8
header=bytearray(src[:40]);pos=40
for i,section in enumerate(sections):struct.pack_into('<I',header,8+4*i,pos);pos+=len(section)
out.write_bytes(header+b''.join(sections))
