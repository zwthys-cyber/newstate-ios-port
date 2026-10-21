"""The diagnostic input must preserve the actual compiled probe instructions."""
import hashlib
import pathlib
import struct
import sys


def text_section(path):
    data=pathlib.Path(path).read_bytes()
    cur=32
    for _ in range(struct.unpack_from('<I',data,16)[0]):
        cmd,size=struct.unpack_from('<II',data,cur)
        if cmd==0x19:
            count=struct.unpack_from('<I',data,cur+64)[0]
            for i in range(count):
                start=cur+72+i*80
                name=data[start:start+16].split(b'\0')[0]
                if name==b'__text':
                    amount,off=struct.unpack_from('<QI',data,start+40)
                    return data[off:off+amount]
        cur+=size
    raise AssertionError('no __text')

original=text_section(sys.argv[1])
variant=text_section(sys.argv[2])
assert original and original==variant,'removing signature changed compiled probe code'
print('PASS: unsigned injector input preserves __text SHA-256 '+hashlib.sha256(original).hexdigest())
