#!/usr/bin/env bash
set -euo pipefail
[[ $# == 2 && -d "$1" && ! -L "$1" && "$2" =~ ^[A-Z0-9]{10}$ ]] || {
  echo 'usage: verify_macos_signatures.sh APP EXPECTED_TEAM' >&2; exit 64;
}
count=0
while IFS= read -r -d '' binary; do
  file -b "$binary" | grep -q 'Mach-O' || continue
  codesign --verify --strict "$binary"
  details=$(codesign -dv --verbose=4 "$binary" 2>&1)
  if ! grep -Fqx "TeamIdentifier=$2" <<< "$details" ||
     ! grep -q '^Authority=Developer ID Application:' <<< "$details" ||
     ! grep -q '^Timestamp=' <<< "$details"; then
    echo "Missing expected Developer ID signature or secure timestamp: $binary" >&2
    exit 1
  fi
  count=$((count + 1))
done < <(find "$1" -type f -print0)
[[ $count -gt 0 ]] || { echo 'App contains no Mach-O code' >&2; exit 1; }
printf 'Verified Developer ID and timestamp on %s Mach-O files.\n' "$count"
