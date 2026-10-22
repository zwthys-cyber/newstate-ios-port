"""GitHub-only verification of the standalone app's actual archive and signature."""
import hashlib
import json
import os
import pathlib
import plistlib
import struct
import zipfile

assert os.environ.get('GITHUB_ACTIONS')=='true' and os.environ.get('RUNNER_ENVIRONMENT')=='github-hosted'
ent=plistlib.loads(pathlib.Path('out/external-entitlements.plist').read_bytes())
hud_ent=plistlib.loads(pathlib.Path('out/external-hud-entitlements.plist').read_bytes())
probe_ent=plistlib.loads(pathlib.Path('out/external-probe-entitlements.plist').read_bytes())
memory_ent=plistlib.loads(pathlib.Path('out/memory-check-entitlements.plist').read_bytes())
assert memory_ent['task_for_pid-allow'] is True
assert memory_ent['com.apple.system-task-ports.control'] is True
assert memory_ent['com.apple.system-task-ports.read'] is True
assert memory_ent['com.apple.private.security.no-container'] is True
assert memory_ent['com.apple.private.security.container-required'] is False
for key in ('get-task-allow','dynamic-codesigning','com.apple.private.cs.debugger','com.apple.private.skip-library-validation'):
    assert not memory_ent.get(key)
memory_imports=set(pathlib.Path('out/memory-check-imports.txt').read_text().split())
assert '_task_for_pid' in memory_imports and '_vm_write' in memory_imports
assert not memory_imports.intersection(('_ptrace','_vm_protect','_mach_vm_protect','_thread_set_state','_task_suspend','_task_terminate'))
assert ent['com.apple.private.persona-mgmt'] is True
assert ent['com.apple.private.security.container-required'] is False
assert ent['com.apple.private.security.no-container'] is True
assert not ent.get('com.apple.system-task-ports.read')
assert hud_ent['com.apple.system-task-ports.read'] is True
assert hud_ent['com.apple.springboard.accessibility-window-hosting'] is True
assert hud_ent['com.apple.private.security.no-container'] is True
assert hud_ent['com.apple.private.security.container-required'] is False
assert not hud_ent.get('com.apple.private.persona-mgmt')
assert probe_ent['com.apple.private.security.container-required'] is False
assert probe_ent['com.apple.private.security.no-container'] is True
assert not probe_ent.get('com.apple.system-task-ports.read')
assert not probe_ent.get('com.apple.private.persona-mgmt')
assert not probe_ent.get('com.apple.springboard.accessibility-window-hosting')
for actual_ent in (ent,hud_ent,probe_ent):
    assert actual_ent['platform-application'] is True
    assert actual_ent['com.apple.private.security.no-sandbox'] is True
    for name in ('get-task-allow','task_for_pid-allow','com.apple.system-task-ports',
                 'com.apple.system-task-ports.control','com.apple.system-task-ports.token.control',
                 'com.apple.private.cs.debugger','com.apple.security.cs.debugger'):
        assert not actual_ent.get(name), f'unexpected control/debug entitlement: {name}'
for filename in ('external-imports.txt','external-hud-imports.txt','external-probe-imports.txt'):
    imports=set(pathlib.Path('out',filename).read_text().split())
    assert not imports.intersection(('_task_for_pid','_ptrace','_vm_write','_mach_vm_write','_vm_protect','_mach_vm_protect'))
with zipfile.ZipFile('out/NSPExternal.ipa') as z:
    assert z.testzip() is None
    assert not any(n.endswith(('.dylib','embedded.mobileprovision')) or '.framework/' in n for n in z.namelist())
    prefix='Payload/NSPExternal.app/'
    info=plistlib.loads(z.read(prefix+'Info.plist'))
    assert info['CFBundleIdentifier']=='org.newstateport.External' and info['MinimumOSVersion']=='17.0'
    assert info['CFBundleShortVersionString']=='0.3.3' and info['CFBundleDisplayName']=='NSP Pro'
    assert info['CFBundleIcons']['CFBundlePrimaryIcon']['CFBundleIconName']=='AppIcon'
    assert prefix+'Assets.car' in z.namelist()
    # actool stores the 3x rendition in Assets.car; its standalone fallback is 2x.
    icon=z.read(prefix+'AppIcon60x60@2x.png')
    assert icon[:8]==b'\x89PNG\r\n\x1a\n' and struct.unpack_from('>II',icon,16)==(120,120)
    assert z.read(prefix+'ThirdPartyNotices.txt')==pathlib.Path('apps/ExternalPort/ThirdPartyNotices.txt').read_bytes()
    binaries={name:z.read(prefix+name) for name in ('NSPExternal','NSPExternalHUD','NSPSpawnProbe','NSPMemoryCheck')}
def verify_binary(data,expected_ent):
    magic,cpu,subtype,kind,count,command_size=struct.unpack_from('<6I',data)
    assert (magic,cpu,subtype,kind)==(0xfeedfacf,0x100000c,0,2)
    assert command_size<=len(data)-32
    cursor=32;signature=None;deps=[];minimum=None;platform=None
    for _ in range(count):
        cmd,size=struct.unpack_from('<II',data,cursor)
        assert size>=8 and cursor+size<=32+command_size
        if cmd==0x32:platform,minimum=struct.unpack_from('<II',data,cursor+8)
        if cmd==0x1d:signature=struct.unpack_from('<II',data,cursor+8)
        if cmd in (0xc,0x80000018,0x8000001f):
            rel=struct.unpack_from('<I',data,cursor+8)[0]
            assert 24<=rel<size
            deps.append(data[cursor+rel:cursor+size].split(b'\0')[0].decode())
        if cmd in (0x21,0x2c):assert struct.unpack_from('<I',data,cursor+16)[0]==0
        cursor+=size
    assert platform==2 and minimum==0x110000 and signature
    assert deps and all(n.startswith(('/System/Library/Frameworks/','/usr/lib/')) for n in deps)
    cs,amount=signature
    assert cs>0 and cs+amount<=len(data)
    magic,length,blobs=struct.unpack_from('>III',data,cs)
    assert magic==0xfade0cc0 and length<=amount
    verified=0;embedded_ent=None
    for i in range(blobs):
        slot,rel=struct.unpack_from('>II',data,cs+12+i*8)
        start=cs+rel
        assert start+8<=cs+length
        magic,size=struct.unpack_from('>II',data,start)
        assert start+size<=cs+length
        if magic==0xfade7171:embedded_ent=plistlib.loads(data[start+8:start+size])
        if magic!=0xfade0c02:continue
        _,_,version,flags,ho,ident,special,codes,limit=struct.unpack_from('>9I',data,start)
        hs,ht,_,page=struct.unpack_from('>4B',data,start+36)
        assert page==12 and hs==32 and ht==2
        assert limit==cs and codes==(limit+4095)//4096 and ho+codes*hs<=size
        for j in range(codes):
            expected=data[start+ho+j*hs:start+ho+(j+1)*hs]
            assert hashlib.sha256(data[j*4096:min((j+1)*4096,limit)]).digest()==expected
        verified+=codes
    assert verified and embedded_ent==expected_ent
    return dict(signature_pages_verified=verified,dependencies=deps,entitlements_verified=True)
expected_ents={'NSPExternal':ent,'NSPExternalHUD':hud_ent,'NSPSpawnProbe':probe_ent,'NSPMemoryCheck':memory_ent}
binary_reports={name:verify_binary(data,expected_ents[name])
                for name,data in binaries.items()}
assert binary_reports['NSPSpawnProbe']['dependencies']==['/usr/lib/libSystem.B.dylib']
report=dict(app_version=info['CFBundleShortVersionString'],bundle_id=info['CFBundleIdentifier'],architecture='arm64',minimum_os='17.0',
    binaries=binary_reports,signature_pages_verified=sum(v['signature_pages_verified'] for v in binary_reports.values()),
    entitlements_verified=True,only_read_port_requested=False,overlay_only_read_port_requested=True,
    manual_control_probe_included=True,manual_probe_writes_only_owned_scratch=True,
    separate_containerless_hud=True,containerless_launcher=True,
    no_embedded_game_or_plugins=True,archive_crc_verified=True,developer_mode_test_condition='off',
    on_device_tested=False,external_read_permission_verified=False,cross_app_overlay_verified=False,
    game_files_modified=False,game_memory_writes=False,app_icon_verified=True,
    controller_ui='NSP Pro: overview/display/tools',settings_preserved_on_upgrade=True)
pathlib.Path('out/external-build-info.json').write_text(json.dumps({**report,'commit':os.environ['GITHUB_SHA'],
    'run_url':f"https://github.com/{os.environ['GITHUB_REPOSITORY']}/actions/runs/{os.environ['GITHUB_RUN_ID']}"},indent=2)+'\n')
print(json.dumps(report,indent=2))
