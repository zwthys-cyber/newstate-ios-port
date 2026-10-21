#!/bin/bash
set -euo pipefail
if [[ "${GITHUB_ACTIONS:-}" != true || "${RUNNER_ENVIRONMENT:-}" != github-hosted || "$(uname -s)" != Darwin ]]; then
  echo 'UI build and checks require a GitHub-hosted macOS runner.' >&2
  exit 1
fi
task_preview_app="$PWD/build/ui-preview/NSPExternal.app"
mkdir -p "$task_preview_app" out/ui-preview
task_sim_sdk="$(xcrun --sdk iphonesimulator --show-sdk-path)"
task_sim_flags=(-target "$(uname -m)-apple-ios17.0-simulator" -isysroot "$task_sim_sdk" -std=c++17 -O1 -Wall -Wextra -Werror -UNDEBUG -fobjc-arc -DNSP_UI_PREVIEW=1 -Isrc -Iapps/ExternalPort)
xcrun clang++ "${task_sim_flags[@]}" src/PortCore.cpp src/RemoteImage.cpp apps/ExternalPort/RemoteReader.mm apps/ExternalPort/main.mm \
  -framework UIKit -framework Foundation -framework CoreGraphics -framework Security -o "$task_preview_app/NSPExternal"
cp apps/ExternalPort/Info.plist "$task_preview_app/Info.plist"
python3 - "$task_preview_app/Info.plist" <<'PY'
import plistlib,sys
from pathlib import Path
p=Path(sys.argv[1]);info=plistlib.loads(p.read_bytes())
info['CFBundleIdentifier']='org.newstateport.UIPreview'
info['CFBundleSupportedPlatforms']=['iPhoneSimulator']
p.write_bytes(plistlib.dumps(info))
PY
codesign --force --sign - "$task_preview_app"
xcrun simctl list devices available --json > build/ui-devices.json
task_sim_id="$(python3 - <<'PY'
import json
devices=json.load(open('build/ui-devices.json'))['devices']
phones=[d for key,values in devices.items() if 'iOS' in key for d in values if d['isAvailable'] and d['name'].startswith('iPhone')]
preferred=next((d for d in phones if d['name']=='iPhone 17 Pro'),phones[0] if phones else None)
assert preferred, 'No available iPhone simulator'
print(preferred['udid'])
PY
)"
xcrun simctl boot "$task_sim_id" || true
xcrun simctl bootstatus "$task_sim_id" -b
xcrun simctl status_bar "$task_sim_id" override --time '9:41' --dataNetwork wifi --wifiMode active --wifiBars 3 --batteryState charged --batteryLevel 100
xcrun simctl install "$task_sim_id" "$task_preview_app"
xcrun simctl launch "$task_sim_id" org.newstateport.UIPreview --nsp-smoke
task_sim_data="$(xcrun simctl get_app_container "$task_sim_id" org.newstateport.UIPreview data)"
task_smoke_path="$task_sim_data/tmp/NSPPreview/ui-smoke.json"
for task_poll in {1..25}; do
  if [[ -f "$task_smoke_path" ]]; then break; fi
  sleep 1
done
cp "$task_smoke_path" out/ui-preview/ui-smoke.json
python3 - <<'PY'
import json
result=json.load(open('out/ui-preview/ui-smoke.json'))
assert result['passed'], result
print(json.dumps(result,ensure_ascii=False,indent=2))
PY
for task_page in 0 1 2; do
  xcrun simctl terminate "$task_sim_id" org.newstateport.UIPreview || true
  xcrun simctl launch "$task_sim_id" org.newstateport.UIPreview --nsp-page "$task_page"
  sleep 2
  xcrun simctl io "$task_sim_id" screenshot "out/ui-preview/page-$task_page.png"
done
xcrun simctl terminate "$task_sim_id" org.newstateport.UIPreview
# Large text exercises wrapping and scrolling on the same real UIKit views.
xcrun simctl ui "$task_sim_id" content_size accessibility-extra-large
rm -f "$task_smoke_path"
xcrun simctl launch "$task_sim_id" org.newstateport.UIPreview --nsp-smoke
for task_poll in {1..25}; do
  if [[ -f "$task_smoke_path" ]]; then break; fi
  sleep 1
done
cp "$task_smoke_path" out/ui-preview/ui-smoke-large-text.json
python3 - <<'PY'
import json
result=json.load(open('out/ui-preview/ui-smoke-large-text.json'))
assert result['passed'], result
PY
xcrun simctl io "$task_sim_id" screenshot out/ui-preview/large-text.png
xcrun simctl terminate "$task_sim_id" org.newstateport.UIPreview
xcrun simctl shutdown "$task_sim_id"
ditto -c -k --keepParent out/ui-preview out/NSPPro-UI-preview.zip
rm -r out/ui-preview
