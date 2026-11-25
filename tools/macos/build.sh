#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 OpenTS contributors
#
# Builds OpenTS.app for the Mac this runs on.
#
# usage: tools/macos/build.sh [Release|Debug] [--zip]
#
# Needs the Xcode command line tools (xcode-select --install) and CMake 3.23 or newer.
# Ninja is used when installed (brew install cmake ninja), otherwise Unix Makefiles.
# The app lands in build-macos/bin/OpenTS.app; --zip also writes build-macos/OpenTS-macOS-<arch>.zip.

set -euo pipefail

config="${1:-Release}"
zip=0
for argument in "$@"; do
	[[ "$argument" == "--zip" ]] && zip=1
done
[[ "$config" == "--zip" ]] && config=Release

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$root/build-macos"
arch="$(uname -m)"

if [[ "$(uname -s)" != "Darwin" ]]; then
	echo "This script builds the macOS app and has to run on macOS." >&2
	exit 1
fi

if ! xcode-select -p >/dev/null 2>&1; then
	echo "The Xcode command line tools are missing. Install them with: xcode-select --install" >&2
	exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
	echo "CMake was not found. Install it with: brew install cmake" >&2
	exit 1
fi

if [[ -d "$root/.git" ]] && [[ ! -f "$root/thirdparty/SDL/CMakeLists.txt" ]]; then
	echo "Fetching the submodules..."
	git -C "$root" submodule update --init --recursive
fi

generator="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then
	generator="Ninja"
fi

cmake -S "$root" -B "$build" -G "$generator" \
	-DCMAKE_BUILD_TYPE="$config" \
	-DCMAKE_OSX_ARCHITECTURES="$arch"

cmake --build "$build" --config "$config" --parallel "$(sysctl -n hw.ncpu)"

app="$build/bin/OpenTS.app"
if [[ ! -d "$app" ]]; then
	echo "The build finished but $app is missing." >&2
	exit 1
fi

# An ad hoc signature is enough to run a locally built app on Apple silicon.
codesign --force --deep --sign - "$app"

if [[ "$zip" == 1 ]]; then
	archive="$build/OpenTS-macOS-$arch.zip"
	rm -f "$archive"
	ditto -c -k --sequesterRsrc --keepParent "$app" "$archive"
	echo "Wrote $archive"
fi

echo
echo "Built $app"
echo "Copy OpenTS.app into your Tiberian Sun folder (the one holding TIBSUN.MIX), or put the"
echo "game files in ~/Library/Application Support/OpenTS/TiberianSun, then open the app."
