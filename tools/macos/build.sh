#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 OpenTS contributors
#
# Builds OpenTS.app for the Mac this runs on, signs it, and optionally notarizes it.
#
# usage: tools/macos/build.sh [Release|Debug] [options]
#
#   --sign IDENTITY      Sign with this certificate, for example
#                        "Developer ID Application: Jane Doe (ABCDE12345)", with the hardened
#                        runtime and a secure timestamp. Without it the app gets an ad hoc
#                        signature, which runs on the Mac that built it.
#   --notarize PROFILE   Submit the signed app to Apple's notary service with the notarytool
#                        keychain profile PROFILE, wait for the verdict, and staple the ticket.
#                        Needs --sign with a Developer ID Application certificate.
#   --zip                Write build-macos/OpenTS-macOS-<arch>.zip.
#   --dmg                Write build-macos/OpenTS-macOS-<arch>.dmg (signed, and notarized with
#                        --notarize).
#
# The identity and profile can also come from OPENTS_SIGN_IDENTITY and OPENTS_NOTARY_PROFILE.
#
# Needs the Xcode command line tools (xcode-select --install) and CMake 3.23 or newer.
# Ninja is used when installed (brew install cmake ninja), otherwise Unix Makefiles.

set -euo pipefail

config="Release"
identity="${OPENTS_SIGN_IDENTITY:-}"
profile="${OPENTS_NOTARY_PROFILE:-}"
zip=0
dmg=0

while [[ $# -gt 0 ]]; do
	case "$1" in
		Release|Debug|RelWithDebInfo)	config="$1" ;;
		--sign)							identity="${2:?--sign needs an identity}"; shift ;;
		--notarize)						profile="${2:?--notarize needs a keychain profile}"; shift ;;
		--zip)							zip=1 ;;
		--dmg)							dmg=1 ;;
		-h|--help)						sed -n '5,23p' "$0"; exit 0 ;;
		*)								echo "Unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$root/build-macos"
arch="$(uname -m)"
app="$build/bin/OpenTS.app"

fail() {
	echo "$*" >&2
	exit 1
}

[[ "$(uname -s)" == "Darwin" ]] || fail "This script builds the macOS app and has to run on macOS."
xcode-select -p >/dev/null 2>&1 || fail "The Xcode command line tools are missing. Install them with: xcode-select --install"
command -v cmake >/dev/null 2>&1 || fail "CMake was not found. Install it with: brew install cmake"

if [[ -n "$profile" && -z "$identity" ]]; then
	fail "Notarizing needs a Developer ID signature: pass --sign as well."
fi

if [[ -n "$identity" ]]; then
	security find-identity -v -p codesigning | grep -F -- "$identity" >/dev/null \
		|| fail "No valid code signing identity matching \"$identity\" is in the keychain. List them with: security find-identity -v -p codesigning"
fi

if [[ ! -f "$root/thirdparty/SDL/CMakeLists.txt" ]]; then
	if [[ -d "$root/.git" ]]; then
		echo "Fetching the submodules..."
		git -C "$root" submodule update --init --recursive
	else
		fail "thirdparty/ is incomplete and this is not a git checkout to fetch it into."
	fi
fi

generator="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then
	generator="Ninja"
fi

echo "==> Configuring ($config, $arch, $generator)"
cmake -S "$root" -B "$build" -G "$generator" \
	-DCMAKE_BUILD_TYPE="$config" \
	-DCMAKE_OSX_ARCHITECTURES="$arch"

echo "==> Building"
cmake --build "$build" --config "$config" --parallel "$(sysctl -n hw.ncpu)"

[[ -d "$app" ]] || fail "The build finished but $app is missing."

# Extended attributes left by copies or downloads make codesign refuse the bundle.
xattr -cr "$app"

echo "==> Signing"
if [[ -n "$identity" ]]; then
	codesign --force --timestamp --options runtime \
		--entitlements "$root/tools/macos/OpenTS.entitlements" \
		--sign "$identity" "$app"
else
	codesign --force --sign - "$app"
fi
codesign --verify --strict --verbose=2 "$app"

if [[ -n "$profile" ]]; then
	echo "==> Notarizing"
	submission="$build/OpenTS-notarize.zip"
	rm -f "$submission"
	ditto -c -k --keepParent "$app" "$submission"
	xcrun notarytool submit "$submission" --keychain-profile "$profile" --wait
	rm -f "$submission"
	xcrun stapler staple "$app"
	xcrun stapler validate "$app"
	spctl --assess --type execute --verbose=2 "$app"
fi

if [[ "$zip" == 1 ]]; then
	archive="$build/OpenTS-macOS-$arch.zip"
	rm -f "$archive"
	ditto -c -k --sequesterRsrc --keepParent "$app" "$archive"
	echo "Wrote $archive"
fi

if [[ "$dmg" == 1 ]]; then
	image="$build/OpenTS-macOS-$arch.dmg"
	staging="$build/dmg"
	rm -rf "$staging" "$image"
	mkdir -p "$staging"
	ditto "$app" "$staging/OpenTS.app"
	ln -s /Applications "$staging/Applications"
	hdiutil create -volname "OpenTS" -srcfolder "$staging" -ov -format UDZO "$image" >/dev/null
	rm -rf "$staging"
	if [[ -n "$identity" ]]; then
		codesign --force --timestamp --sign "$identity" "$image"
	fi
	if [[ -n "$profile" ]]; then
		xcrun notarytool submit "$image" --keychain-profile "$profile" --wait
		xcrun stapler staple "$image"
	fi
	echo "Wrote $image"
fi

echo
echo "Built $app"
if [[ -z "$identity" ]]; then
	echo "It has an ad hoc signature: it runs on this Mac, and on others only after"
	echo "right-click > Open or: xattr -dr com.apple.quarantine OpenTS.app"
elif [[ -z "$profile" ]]; then
	echo "It is signed but not notarized: Gatekeeper on other Macs will still warn about it."
fi
echo "Copy OpenTS.app into your Tiberian Sun folder (the one holding TIBSUN.MIX), or put the"
echo "game files in ~/Library/Application Support/OpenTS/TiberianSun, then open the app."
