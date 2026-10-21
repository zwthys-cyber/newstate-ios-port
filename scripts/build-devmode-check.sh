#!/bin/bash
set -euo pipefail
if [[ "${GITHUB_ACTIONS:-}" != "true" || "${RUNNER_ENVIRONMENT:-}" != "github-hosted" || "$(uname -s)" != "Darwin" ]]; then
  echo 'Use a GitHub-hosted macOS runner for compilation.' >&2
  exit 1
fi
task_ipa_output="$PWD/out/DeveloperModeCheck.ipa"
task_app_root=build/devmode-check
task_app_path="$task_app_root/Payload/DeveloperModeCheck.app"
mkdir -p "$task_app_path" out
cp apps/DeveloperModeCheck/Info.plist "$task_app_path/Info.plist"
plutil -lint "$task_app_path/Info.plist"
task_sdk_path="$(xcrun --sdk iphoneos --show-sdk-path)"
xcrun --sdk iphoneos clang -arch arm64 -isysroot "$task_sdk_path" -miphoneos-version-min=17.0 \
  -fobjc-arc -O2 -Wall -Wextra -Werror apps/DeveloperModeCheck/main.m \
  -framework UIKit -framework Foundation -o "$task_app_path/DeveloperModeCheck"
codesign --force --sign - --timestamp=none --pagesize 4096 "$task_app_path"
codesign --verify --strict --verbose=2 "$task_app_path"
xcrun lipo "$task_app_path/DeveloperModeCheck" -verify_arch arm64
(cd "$task_app_root" && /usr/bin/zip -qr "$task_ipa_output" Payload)
cp apps/DeveloperModeCheck/README.zh-CN.md out/DeveloperModeCheck-README.zh-CN.md
python3 - <<'PY'
import json,pathlib,plistlib,struct,zipfile
with zipfile.ZipFile('out/DeveloperModeCheck.ipa') as z:
    assert z.testzip() is None
    prefix='Payload/DeveloperModeCheck.app/'
    p=plistlib.loads(z.read(prefix+'Info.plist'))
    assert p['CFBundleIdentifier']=='org.newstateport.DeveloperModeCheck'
    data=z.read(prefix+p['CFBundleExecutable'])
    magic,cpu,subtype,kind=struct.unpack_from('<4I',data)
    assert (magic,cpu,subtype,kind)==(0xfeedfacf,0x100000c,0,2)
    assert not any(n.endswith('embedded.mobileprovision') for n in z.namelist())
    assert not any(n.endswith('.dylib') for n in z.namelist())
pathlib.Path('out/DeveloperModeCheck-verification.json').write_text(json.dumps(dict(
    bundle_id=p['CFBundleIdentifier'],architecture='arm64',minimum_os='17.0',
    archive_crc_verified=True,contains_game=False,requires_user_resigning=True,
    on_device_tested=False,direct_developer_mode_activation=False),indent=2)+'\n')
PY
