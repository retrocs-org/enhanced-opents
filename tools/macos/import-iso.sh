#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 OpenTS contributors

set -euo pipefail

fail() {
	printf '%s\n' "$*" >&2
	exit 1
}

[[ $# -ge 2 ]] || fail "Usage: $0 NEW_DATA_DIRECTORY ISO [ISO ...] or NEW_DATA_DIRECTORY --directory FOLDER"
[[ "$(uname -s)" == Darwin ]] || fail "This importer requires macOS."

destination="$1"
shift
parent="$(cd -- "$(dirname -- "$destination")" && pwd -P)" ||
	fail "Create the destination's parent directory first."
destination="$parent/$(basename -- "$destination")"
[[ ! -e "$destination" && ! -L "$destination" ]] ||
	fail "The destination already exists: $destination"

if [[ "$1" == --directory ]]; then
	[[ $# == 2 ]] || fail "Choose one installed game folder."
	source_directory="$(cd -- "$2" && pwd -P)" || fail "Could not open the selected game folder."
	case "$destination/" in
		"$source_directory/"*) fail "Choose a destination outside the selected game folder." ;;
	esac
	set -- --directory "$source_directory"
fi

scratch="$(mktemp -d "$parent/.opents-iso.XXXXXX")"
mount="$scratch/disc"
staging="$scratch/data"
attached=0
publishing=0

progress() {
	local name="${2//$'\n'/ }"
	printf 'PROGRESS\t%s\t%s\n' "$1" "$name"
}

disk_image() {
	if ! hdiutil "$@" >/dev/null 2>"$scratch/disk-error"; then
		sed '/^hdiutil: WARNING:/d' "$scratch/disk-error" >&2
		return 1
	fi
}

cleanup() {
	if [[ "$attached" == 1 ]]; then
		disk_image detach "$mount" || return
	fi
	if [[ "$publishing" == 1 ]]; then
		rm -rf -- "$destination"
	fi
	rm -rf -- "$scratch"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
mkdir "$mount" "$staging"

if [[ "$1" == --directory ]]; then
	[[ $# == 2 && -d "$2" ]] || fail "Choose one installed game folder."
	shopt -s dotglob nullglob nocasematch
	entries=("$2"/*)
	[[ ${#entries[@]} -gt 0 ]] || fail "The selected game folder is empty."
	index=0
	for source in "${entries[@]}"; do
		progress "$((5 + 90 * index / ${#entries[@]}))" "Copying ${source##*/} ($((index + 1)) of ${#entries[@]})"
		/bin/cp -a -- "$source" "$staging/${source##*/}"
		index=$((index + 1))
	done
else
	disc=0
	total=$#
	for iso in "$@"; do
		case "$iso" in
			/*) ;;
			*) iso="$PWD/$iso" ;;
		esac
		[[ -f "$iso" ]] || fail "Image file not found: $iso"
		progress "$((5 + 90 * disc / total))" "Opening ${iso##*/} (disc $((disc + 1)) of $total)"
		disk_image attach -readonly -nobrowse -mountpoint "$mount" "$iso" ||
			fail "Could not open disc image: ${iso##*/}"
		attached=1
		shopt -s nullglob nocaseglob
		sources=()
		expansion=0
		for folder in "$mount" "$mount"/[Ii][Nn][Ss][Tt][Aa][Ll][Ll]; do
			[[ -d "$folder" && ! -L "$folder" ]] || continue
			for source in "$folder"/*.mix; do
				[[ -f "$source" && ! -L "$source" ]] || continue
				sources+=("$source")
				[[ "${source##*/}" == [Ee][Xx][Pp][Aa][Nn][Dd]01.[Mm][Ii][Xx] ]] && expansion=1
			done
		done
		[[ ${#sources[@]} -gt 0 ]] || fail "No MIX archives in the disc root or INSTALL folder: $iso"
		index=0
		for source in "${sources[@]}"; do
			name="$(printf '%s' "${source##*/}" | LC_ALL=C tr '[:lower:]' '[:upper:]')"
			progress "$((5 + 90 * (disc * ${#sources[@]} + index) / (total * ${#sources[@]})))" \
				"Importing $name ($((index + 1)) of ${#sources[@]}, disc $((disc + 1)) of $total)"
			index=$((index + 1))
			target="$staging/$name"
			case "$expansion:$name" in
				1:SCORES.MIX|1:MULTI.MIX|1:SIDECD01.MIX|1:SIDECD02.MIX)
					target="$scratch/$name"
					;;
			esac
			if [[ -e "$target" ]]; then
				cmp -s "$source" "$target" || fail "Conflicting copies of $name in the supplied images."
			else
				cp -- "$source" "$target"
			fi
		done
		disk_image detach "$mount" || fail "Could not eject disc image: ${iso##*/}"
		attached=0
		disc=$((disc + 1))
	done
	for name in SCORES.MIX MULTI.MIX SIDECD01.MIX SIDECD02.MIX; do
		if [[ -f "$scratch/$name" && ! -e "$staging/$name" ]]; then
			mv -- "$scratch/$name" "$staging/$name"
		fi
	done
fi

progress 96 "Checking game archives"
has_archive() {
	local archive
	for archive in "$staging"/*; do
		if [[ -f "$archive" && -s "$archive" && "${archive##*/}" == $1 ]]; then
			return 0
		fi
	done
	return 1
}
if ! has_archive TIBSUN.MIX && { ! has_archive CACHE.MIX || ! has_archive CONQUER.MIX; }; then
	fail "The selected data does not contain TIBSUN.MIX or both CACHE.MIX and CONQUER.MIX."
fi

progress 99 "Saving imported game data"
mkdir -- "$destination"
publishing=1
entries=("$staging"/*)
mv -- "${entries[@]}" "$destination/"
publishing=0
printf 'Imported game archives into %s\n' "$destination"
