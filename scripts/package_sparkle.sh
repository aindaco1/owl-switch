#!/usr/bin/env bash
set -euo pipefail
[[ $# == 3 ]] || { echo 'usage: package_sparkle.sh APP TAG OUTPUT_DIRECTORY' >&2; exit 64; }
app="$1"; tag="$2"; output="$3"
[[ "$tag" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] || exit 64
[[ ! -e "$output" && -n "${SPARKLE_PRIVATE_KEY:-}" ]] || exit 64
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
umask 077
printf '%s' "$SPARKLE_PRIVATE_KEY" > "$work/key"
unset SPARKLE_PRIVATE_KEY
curl --fail --location --retry 3 --output "$work/sparkle.tar.xz" \
  https://github.com/sparkle-project/Sparkle/releases/download/2.10.0/Sparkle-2.10.0.tar.xz
printf '%s  %s\n' c2bf58aa8387266ac179357b1415d6f2635f044da8be41042af32425dae6da0c "$work/sparkle.tar.xz" | shasum -a 256 -c -
mkdir "$work/tools" "$output"
tar -xJf "$work/sparkle.tar.xz" -C "$work/tools"
codesign --verify --deep --strict "$app"
xcrun stapler validate "$app"
ditto -c -k --keepParent "$app" "$output/OwlSwitch.zip"
"$work/tools/bin/generate_appcast" --ed-key-file "$work/key" \
  --download-url-prefix "https://github.com/aindaco1/owl-switch/releases/download/$tag/" \
  --link https://github.com/aindaco1/owl-switch --maximum-deltas 0 "$output"
test -s "$output/appcast.xml"
grep -q 'sparkle:edSignature=' "$output/appcast.xml"
grep -q 'sparkle:version' "$output/appcast.xml"
(cd "$output" && shasum -a 256 OwlSwitch.zip appcast.xml > SHA256SUMS)
