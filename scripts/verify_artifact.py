"""Check the actual linked iOS Mach-O, not only the compiler exit status."""
import hashlib
import json
import pathlib
import struct
import sys

path=pathlib.Path(sys.argv[1]);data=path.read_bytes()
assert len(data)>=32 and data[:4]==b'\xcf\xfa\xed\xfe', 'not a thin Mach-O 64'
_,cpu,subtype,filetype,ncmds,sizeofcmds,flags,reserved=struct.unpack_from('<8I',data)
assert cpu==0x100000c and subtype==0 and filetype==6, 'expected arm64 MH_DYLIB'
assert sizeofcmds<=len(data)-32, 'invalid load command area'
signature_offset=None;signature_size=None
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
        signature_offset=off;signature_size=amount
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
# Require real, internally consistent 4 KiB CodeDirectory pages for TrollFools.
# Presence of LC_CODE_SIGNATURE alone cannot detect the observed 16 KiB/4 KiB mismatch.
magic,length,count=struct.unpack_from('>III',data,signature_offset)
assert magic==0xfade0cc0 and length<=signature_size
code_directories=[]
for i in range(count):
    slot,rel=struct.unpack_from('>II',data,signature_offset+12+i*8)
    start=signature_offset+rel
    assert start+8<=signature_offset+length
    magic,cd_length=struct.unpack_from('>II',data,start)
    assert start+cd_length<=signature_offset+length
    if magic!=0xfade0c02:continue
    _,_,version,cd_flags,hash_offset,ident,special,codes,limit=struct.unpack_from('>9I',data,start)
    hash_size,hash_type,_,page_log=struct.unpack_from('>4B',data,start+36)
    if version>=0x20300 and limit==0:limit=struct.unpack_from('>Q',data,start+56)[0]
    assert page_log==12, 'signature pages must be 4096 bytes for TrollFools compatibility'
    assert limit==signature_offset and codes==(limit+4095)//4096, 'invalid CodeDirectory coverage'
    assert hash_type==2 and hash_size==32, 'expected SHA-256 signature'
    assert hash_offset+codes*hash_size<=cd_length
    for j in range(codes):
        actual=hashlib.sha256(data[j*4096:min((j+1)*4096,limit)]).digest()
        expected=data[start+hash_offset+j*hash_size:start+hash_offset+(j+1)*hash_size]
        assert actual==expected, f'signature page hash mismatch: {j}'
    code_directories.append(dict(slot=slot,page_size=4096,code_slots=codes,page_hashes_verified=True))
assert code_directories, 'missing CodeDirectory'
assert identity==(sys.argv[2] if len(sys.argv)>2 else '@rpath/NewStatePort.dylib')
assert deps and all(d.startswith(('/System/Library/Frameworks/','/usr/lib/')) for d in deps), deps
if path.name=='NewStateLoadProbe.dylib':
    assert all(d.startswith('/usr/lib/') for d in deps), 'probe must not link frameworks'
print(json.dumps(dict(architecture='arm64',platform='iOS',minimum_os='17.0',
    sdk=f'{sdk>>16}.{(sdk>>8)&255}.{sdk&255}',install_name=identity,dependencies=deps,
    embedded_signature=True,code_directories=code_directories,constructor=True,on_device_tested=False),indent=2))
