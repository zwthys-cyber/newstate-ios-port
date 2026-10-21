"""Check the actual linked iOS Mach-O, not only the compiler exit status."""
import json
import pathlib
import struct
import sys

path=pathlib.Path(sys.argv[1]);data=path.read_bytes()
assert len(data)>=32 and data[:4]==b'\xcf\xfa\xed\xfe', 'not a thin Mach-O 64'
_,cpu,subtype,filetype,ncmds,sizeofcmds,flags,reserved=struct.unpack_from('<8I',data)
assert cpu==0x100000c and subtype==0 and filetype==6, 'expected arm64 MH_DYLIB'
assert sizeofcmds<=len(data)-32, 'invalid load command area'
cursor=32;platform=None;minimum=None;sdk=None;signature=False;identity=None;deps=[];constructor=False
for _ in range(ncmds):
    cmd,size=struct.unpack_from('<II',data,cursor)
    assert size>=8 and cursor+size<=32+sizeofcmds
    if cmd==0x32:
        platform,minimum,sdk=struct.unpack_from('<III',data,cursor+8)
    if cmd in (0xc,0xd,0x80000018,0x8000001f):
        rel=struct.unpack_from('<I',data,cursor+8)[0];assert 24<=rel<size
        end=data.index(b'\0',cursor+rel,cursor+size);name=data[cursor+rel:end].decode()
        if cmd==0xd:identity=name
        else:deps.append(name)
    if cmd==0x1d:
        off,amount=struct.unpack_from('<II',data,cursor+8)
        signature=off>0 and amount>0 and off+amount<=len(data)
    if cmd==0x19:
        count=struct.unpack_from('<I',data,cursor+64)[0]
        assert 72+80*count<=size
        for i in range(count):
            so=cursor+72+80*i
            name=data[so:so+16].split(b'\0')[0]
            if name in (b'__mod_init_func',b'__init_offsets') and struct.unpack_from('<Q',data,so+40)[0]>0:constructor=True
    cursor+=size
assert platform==2, 'must target iOS device, not simulator or macOS'
assert minimum==0x110000, 'deployment target must be iOS 17.0'
assert sdk and sdk>=minimum
assert signature and constructor
assert identity==(sys.argv[2] if len(sys.argv)>2 else '@rpath/NewStatePort.dylib')
assert deps and all(d.startswith(('/System/Library/Frameworks/','/usr/lib/')) for d in deps), deps
if path.name=='NewStateLoadProbe.dylib':
    assert all(d.startswith('/usr/lib/') for d in deps), 'probe must not link frameworks'
print(json.dumps(dict(architecture='arm64',platform='iOS',minimum_os='17.0',
    sdk=f'{sdk>>16}.{(sdk>>8)&255}.{sdk&255}',install_name=identity,dependencies=deps,
    embedded_signature=True,constructor=True,on_device_tested=False),indent=2))
