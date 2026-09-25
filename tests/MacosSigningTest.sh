#!/usr/bin/env bash
set -euo pipefail
[[ $# == 1 ]] || exit 64
verifier="$1"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
app="$work/Fixture.app"
mkdir -p "$app/Contents/PlugIns/platforms"
printf 'int fixture(void) { return 0; }\n' > "$work/fixture.c"
plugin="$app/Contents/PlugIns/platforms/libfixture.dylib"
clang -dynamiclib "$work/fixture.c" -o "$plugin"
codesign --force --sign - "$plugin"
# Strict signature verification accepts ad hoc code; release preflight must not.
codesign --verify --strict "$plugin"
if "$verifier" "$app" PWT3Q52LZ2 > "$work/result" 2>&1; then
  echo 'Release preflight accepted an ad hoc Qt plug-in' >&2; exit 1
fi
grep -Fq 'Missing expected Developer ID signature or secure timestamp:' "$work/result"
grep -Fq 'Contents/PlugIns/platforms/libfixture.dylib' "$work/result"
rm "$plugin"
if "$verifier" "$app" PWT3Q52LZ2 > "$work/result" 2>&1; then
  echo 'Release preflight accepted an empty app' >&2; exit 1
fi
grep -Fq 'App contains no Mach-O code' "$work/result"
echo 'Unsigned nested plug-in and empty-app rejection passed.'
