#!/usr/bin/env bash
set -euo pipefail
[[ $# == 1 && "$1" == /* && ! -e "$1" ]] || exit 64
root="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir "$work/owl-switch-source"
git -C "$root" archive HEAD | tar -x -C "$work/owl-switch-source"
mkdir -p "$work/owl-switch-source/shared/dust-wave-platform"
git -C "$root/shared/dust-wave-platform" archive HEAD | tar -x -C "$work/owl-switch-source/shared/dust-wave-platform"
git -C "$root" rev-parse HEAD > "$work/owl-switch-source/SOURCE_COMMIT"
git -C "$root/shared/dust-wave-platform" rev-parse HEAD > "$work/owl-switch-source/PLATFORM_COMMIT"
COPYFILE_DISABLE=1 tar -czf "$1" -C "$work" owl-switch-source
