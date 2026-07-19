#!/usr/bin/env bash
# WaveTree II plugin build — pins Apple clang + Xcode SDK, scrubs conda/miniforge
# env vars that would pollute the JUCE build. Pass extra CMake args through.
#   ./build.sh              # arm64 dev build
#   ./build.sh --universal  # arm64 + x86_64 (final artifacts)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${ROOT}/build"
SDK="$(xcrun --show-sdk-path)"
ARCHS="arm64"
if [[ "${1:-}" == "--universal" ]]; then ARCHS="arm64;x86_64"; shift; fi
if command -v ninja >/dev/null 2>&1; then GEN=(-G Ninja); else GEN=(-G "Unix Makefiles"); fi
unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS LDFLAGS_LD DEBUG_CFLAGS \
      C_INCLUDE_PATH CPLUS_INCLUDE_PATH CPATH OBJC_INCLUDE_PATH \
      LIBRARY_PATH DYLD_LIBRARY_PATH PKG_CONFIG_PATH 2>/dev/null || true
cmake -S "${ROOT}" -B "${BUILD}" "${GEN[@]}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
    -DCMAKE_OSX_ARCHITECTURES="${ARCHS}" \
    -DCMAKE_OSX_SYSROOT="${SDK}" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 "$@"
cmake --build "${BUILD}" --config Release -j4
echo "=== done ==="
echo "VST3:       ${BUILD}/WaveTree_artefacts/Release/VST3/WaveTree II.vst3"
echo "Standalone: ${BUILD}/WaveTree_artefacts/Release/Standalone/WaveTree II.app"
