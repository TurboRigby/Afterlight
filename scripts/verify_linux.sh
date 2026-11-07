#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
set -euo pipefail
if [[ $(uname -s) != Linux ]]; then printf 'Run this verifier on the native Linux desktop.\n' >&2; exit 1; fi
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
configuration=${AF_NATIVE_CONFIGURATION:-Debug}
build_dir=${AF_NATIVE_BUILD_DIR:-"$root/build/native-${configuration,,}"}
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-"/run/user/$(id -u)"}
if [[ -S "$XDG_RUNTIME_DIR/${WAYLAND_DISPLAY:-wayland-0}" ]]; then
    export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0}
    export SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-wayland}
fi
if [[ ${SDL_VIDEODRIVER:-} == wayland ]]; then unset DISPLAY; fi
export SDL_AUDIO_DRIVER=${SDL_AUDIO_DRIVER:-dummy}
export AFTERLIGHT_REQUIRE_VALIDATION=1
export UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}
flags=()
if [[ $configuration == Sanitizers ]]; then
    configuration=Debug
    flags+=('-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined' '-DCMAKE_MODULE_LINKER_FLAGS=-fsanitize=address,undefined')
    export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0}
fi
mkdir -p "$build_dir/native-report"
{ uname -a; cmake --version; pkg-config --modversion sdl3 vulkan; vulkaninfo --summary; } > "$build_dir/native-report/hardware.txt" 2>&1
cmake -S "$root" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE="$configuration" -DBUILD_TESTING=ON "${flags[@]}" > "$build_dir/native-report/configure.log" 2>&1
cmake --build "$build_dir" --parallel "${AF_BUILD_JOBS:-3}" > "$build_dir/native-report/build.log" 2>&1
sha256sum "$build_dir/source/afterlight-source.zip" > "$build_dir/native-report/source.sha256"
if [[ -n ${AF_NATIVE_TEST_REGEX:-} ]]; then
    ctest --test-dir "$build_dir" --no-tests=error --output-on-failure -R "$AF_NATIVE_TEST_REGEX" | tee "$build_dir/native-report/tests.log"
else
    ctest --test-dir "$build_dir" --no-tests=error --output-on-failure | tee "$build_dir/native-report/tests.log"
fi
printf 'Native verification report: %s/native-report\n' "$build_dir"
