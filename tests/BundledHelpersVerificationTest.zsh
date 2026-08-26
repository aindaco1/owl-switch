#!/bin/zsh
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <helper-verifier>" >&2
    exit 2
fi

verifier="${1:A}"
test_root="$(mktemp -d "${TMPDIR:-/tmp}/240-mp-helper-verification-test.XXXXXX")"
cleanup() {
    /bin/rm -rf -- "$test_root"
}
trap cleanup EXIT

bin="$test_root/bin"
pins="$test_root/BundledHelpers.cmake"
helper_home="$test_root/home"
mkdir -p "$bin"
print -r -- 'set(YT_DLP_VERSION "2026.08.19" CACHE STRING "Pinned yt-dlp release")' > "$pins"
print -r -- 'set(DENO_VERSION "2.9.3" CACHE STRING "Pinned Deno release")' >> "$pins"

write_helper() {
    local name="$1"
    local output="$2"
    print -r -- '#!/bin/sh' > "$bin/$name"
    print -r -- "printf '%s\\n' '$output'" >> "$bin/$name"
    chmod 0755 "$bin/$name"
}

write_helper yt-dlp '2026.08.19'
write_helper deno 'deno 2.9.3 (stable, release, aarch64-apple-darwin)'
write_helper mpv 'mpv 0.40.0'
write_helper ffmpeg 'ffmpeg version 8.0'
write_helper ffprobe 'ffprobe version 8.0'

"$verifier" signed-runtime "$bin" "$pins" "$helper_home" >/dev/null

write_helper deno 'deno 2.9.30 (stable, release, aarch64-apple-darwin)'
if "$verifier" pinned-only "$bin" "$pins" "$helper_home" >/dev/null 2>&1; then
    echo "helper verifier accepted the wrong Deno semantic version" >&2
    exit 1
fi

write_helper deno 'deno 2.9.3'
"$verifier" pinned-only "$bin" "$pins" "$helper_home" >/dev/null

write_helper yt-dlp '2026.08.20'
if "$verifier" pinned-only "$bin" "$pins" "$helper_home" >/dev/null 2>&1; then
    echo "helper verifier accepted the wrong yt-dlp version" >&2
    exit 1
fi

write_helper yt-dlp '2026.08.19'
/bin/rm "$bin/mpv"
"$verifier" pinned-only "$bin" "$pins" "$helper_home" >/dev/null
if "$verifier" signed-runtime "$bin" "$pins" "$helper_home" >/dev/null 2>&1; then
    echo "signed-runtime verification accepted a missing runtime helper" >&2
    exit 1
fi

echo "bundled helper verification tests passed"
