#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 OpenTS contributors

set -euo pipefail

fail() {
	printf '%s\n' "$*" >&2
	exit 1
}

if [[ $# -lt 3 || $# -gt 4 || ( $# -eq 4 && "$4" != "--distribution" ) ]]; then
	fail "Usage: $0 ZIP ARCH MIN_MACOS [--distribution]"
fi

scratch="$(mktemp -d "${TMPDIR:-/tmp}/opents-smoke.XXXXXX")"
trap 'rm -rf "$scratch"' EXIT
component="$(printf '%0180d' 0)"
long_root="$scratch/$component/$component/$component"
mkdir -p "$long_root/Extracted App"
ditto -x -k "$1" "$long_root/Extracted App"

app="$long_root/Extracted App/OpenTS.app"
plist="$app/Contents/Info.plist"
binary="$app/Contents/MacOS/OpenTS"
ui="$app/Contents/Resources/ui"

plutil -lint "$plist"
codesign --verify --strict --verbose=2 "$app"
for asset in Arimo.ttf mainopt.rml mainopt.rcss; do
	[[ -s "$ui/$asset" ]] || fail "Missing or empty UI asset: $asset"
done

importer="$app/Contents/Resources/import-iso.sh"
[[ -s "$importer" ]] || fail "Missing or empty ISO importer."
bash -n "$importer"

arch="$(lipo -archs "$binary")"
[[ "$arch" == "$2" ]] || fail "Expected architecture $2, found $arch."
bundle_min="$(plutil -extract LSMinimumSystemVersion raw -o - "$plist")"
binary_min="$(xcrun vtool -show-build "$binary" | awk '$1 == "minos" { print $2 }')"
[[ "$bundle_min" == "$3" && "$binary_min" == "$3" ]] ||
	fail "Expected minimum macOS $3, found $bundle_min in Info.plist and $binary_min in the executable."

if [[ "${4:-}" == "--distribution" ]]; then
	xcrun stapler validate "$app"
	spctl --assess --type execute --verbose=2 "$app"
fi

status=0
"$binary" -h "-USERDIR=$long_root/player" >"$scratch/help.log" 2>&1 || status=$?
if [[ "$status" -ne 1 ]] || ! grep -q -- '-DESTNET' "$scratch/help.log"; then
	cat "$scratch/help.log" >&2
	fail "OpenTS did not print its command-line help (exit $status)."
fi

mkdir "$long_root/empty data"
status=0
"$binary" "-DATADIR=$long_root/empty data" "-USERDIR=$long_root/player" \
	"-CAPTUREMAP=$scratch/unused.map" "-CAPTUREOUT=$scratch/unused.png" \
	>"$scratch/missing-data.log" 2>&1 || status=$?
if [[ "$status" -ne 1 ]] ||
	! grep -Fq -- "$long_root/empty data/" "$scratch/missing-data.log" ||
	! grep -Fq -- 'including TIBSUN.MIX' "$scratch/missing-data.log" ||
	! grep -Fq -- 'Debug log:' "$scratch/missing-data.log"; then
	cat "$scratch/missing-data.log" >&2
	fail "OpenTS did not report the missing game data (exit $status)."
fi

printf 'Verified OpenTS ZIP for %s with macOS minimum %s.\n' "$2" "$3"
