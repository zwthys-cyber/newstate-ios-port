#!/bin/bash
set -euo pipefail
if [[ "${GITHUB_ACTIONS:-}" != "true" || "${RUNNER_ENVIRONMENT:-}" != "github-hosted" || "$(uname -s)" != "Darwin" ]]; then
  echo 'Use a GitHub-hosted macOS runner for compilation and native tests.' >&2
  exit 1
fi
mkdir -p build out
task_sdk_path="$(xcrun --sdk iphoneos --show-sdk-path)"
xcrun clang++ -std=c++17 -Wall -Wextra -Werror -UNDEBUG -fsanitize=address,undefined \
  -Isrc src/PortCore.cpp tests/core_tests.cpp -o build/core-tests
build/core-tests | tee out/tests.txt
task_common=(-arch arm64 -isysroot "$task_sdk_path" -miphoneos-version-min=17.0 -std=c++17 -O2 -Wall -Wextra -Werror -fvisibility=hidden -Isrc)
xcrun --sdk iphoneos clang++ "${task_common[@]}" -c src/PortCore.cpp -o build/PortCore.o
xcrun --sdk iphoneos clang++ "${task_common[@]}" -fobjc-arc -c src/Plugin.mm -o build/Plugin.o
xcrun --sdk iphoneos clang++ "${task_common[@]}" -dynamiclib build/PortCore.o build/Plugin.o \
  -framework Foundation -framework UIKit -framework CoreGraphics \
  -Wl,-install_name,@rpath/NewStatePort.dylib -Wl,-dead_strip -o out/NewStatePort.dylib
codesign --force --sign - --timestamp=none --pagesize 4096 out/NewStatePort.dylib
codesign --verify --strict --verbose=2 out/NewStatePort.dylib 2>&1 | tee out/signature.txt
xcrun lipo out/NewStatePort.dylib -verify_arch arm64
xcrun lipo -info out/NewStatePort.dylib | tee out/architecture.txt
xcrun otool -L out/NewStatePort.dylib | tee out/dependencies.txt
python3 scripts/verify_artifact.py out/NewStatePort.dylib | tee out/macho-verification.json
# A separate injection-control library: no Objective-C, UIKit, UI or memory reads.
xcrun --sdk iphoneos clang "${task_common[@]}" -x c++ -dynamiclib src/LoadProbe.cpp \
  -Wl,-install_name,@rpath/NewStateLoadProbe.dylib -Wl,-dead_strip -o out/NewStateLoadProbe.dylib
codesign --force --sign - --timestamp=none --pagesize 4096 out/NewStateLoadProbe.dylib
codesign --verify --strict --verbose=2 out/NewStateLoadProbe.dylib
xcrun lipo out/NewStateLoadProbe.dylib -verify_arch arm64
python3 scripts/verify_artifact.py out/NewStateLoadProbe.dylib '@rpath/NewStateLoadProbe.dylib' | tee out/probe-verification.json
python3 tests/signature_test.py out/NewStateLoadProbe.dylib | tee out/signature-regression.txt
python3 - <<'PY'
import json,os,pathlib,subprocess
data={
  'plugin_version':'0.1.2','commit':os.environ['GITHUB_SHA'],
  'run_url':f"{os.environ['GITHUB_SERVER_URL']}/{os.environ['GITHUB_REPOSITORY']}/actions/runs/{os.environ['GITHUB_RUN_ID']}",
  'runner_environment':os.environ['RUNNER_ENVIRONMENT'],
  'compiler':subprocess.check_output(['xcrun','clang++','--version'],text=True).splitlines()[0],
  'sdk':subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-version'],text=True).strip(),
  'deployment_target':'17.0','user_reported_device_os':'17.0','installation':'TrollFools 4.2 (227) injection',
  'signature_page_size':4096,
  'target_game_version':'0.9.98','target_game_build':'804',
  'on_device_tested':False,'gameplay_features_verified':False,
  'mode':'deferred-startup-read-only-overlay','engine_function_calls':False,'game_memory_writes':False
}
pathlib.Path('out/build-info.json').write_text(json.dumps(data,indent=2)+'\n')
PY
cp README.zh-CN.md out/
git archive --format=zip --output=out/newstate-ios-port-source.zip HEAD
bash scripts/build-devmode-check.sh
(cd out && shasum -a 256 ./* > SHA256SUMS)
