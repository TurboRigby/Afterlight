#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
set -euo pipefail

# Ubuntu 24.04 ships SDL2. Build the same pinned SDL3 on CI and local Linux.
source_root="$(cd "$(dirname "$0")/../.." && pwd)"
build_root="${AF_LINUX_BUILD_ROOT:-$source_root/build/linux-ci}"
configuration="${AF_BUILD_CONFIGURATION:-Debug}"
case "$configuration" in Debug|Release|Sanitizers) ;; *) echo "Unsupported build configuration: $configuration" >&2; exit 2 ;; esac
engine_build="$build_root/engine-$configuration"
jobs="${AF_BUILD_JOBS:-3}"
sdl_revision=fa2c02bb6e21974a89ea9824bc53c9932abe5f9c # release-3.4.16
mkdir -p "$build_root"
if [[ ! -d "$build_root/SDL/.git" ]]; then
  git init "$build_root/SDL"
  git -C "$build_root/SDL" remote add origin https://github.com/libsdl-org/SDL.git
fi
git -C "$build_root/SDL" fetch --depth 1 origin "$sdl_revision"
git -C "$build_root/SDL" checkout --detach "$sdl_revision"
cmake -S "$build_root/SDL" -B "$build_root/sdl-build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$build_root/sdl-install" \
  -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF \
  -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_WAYLAND=OFF
cmake --build "$build_root/sdl-build" --parallel "$jobs"
cmake --install "$build_root/sdl-build"

flags=('-DCMAKE_C_FLAGS=' '-DCMAKE_EXE_LINKER_FLAGS=' '-DCMAKE_MODULE_LINKER_FLAGS=')
if [[ "$configuration" == Sanitizers ]]; then
  configuration=Debug
  flags+=('-DCMAKE_C_COMPILER=clang' '-DCMAKE_C_FLAGS=-O1 -fsanitize=address,undefined -fno-omit-frame-pointer' '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined' '-DCMAKE_MODULE_LINKER_FLAGS=-fsanitize=address,undefined')
fi
cmake -S "$source_root" -B "$engine_build" -G Ninja \
  -DCMAKE_BUILD_TYPE="$configuration" -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH="$build_root/sdl-install" "${flags[@]}"
cmake --build "$engine_build" --parallel "$jobs"
ctest --test-dir "$engine_build" --output-on-failure -LE graphics
