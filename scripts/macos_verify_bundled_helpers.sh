#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 4 ]]; then
    echo "usage: $0 <pinned-only|signed-runtime> <absolute-helper-bin> <absolute-helper-pins.cmake> <absolute-helper-home>" >&2
    exit 64
fi

mode="$1"
bin="$2"
pins="$3"
helper_home="$4"

if [[ "$mode" != "pinned-only" && "$mode" != "signed-runtime" ]] || \
   [[ "$bin" != /* || "$pins" != /* || "$helper_home" != /* || \
      ! -d "$bin" || -L "$bin" || ! -f "$pins" || -L "$pins" ]]; then
    echo "invalid bundled helper verification input" >&2
    exit 64
fi
mkdir -p "$helper_home"

for helper in yt-dlp deno; do
    if [[ ! -x "$bin/$helper" || -L "$bin/$helper" ]]; then
        echo "bundled helper is missing, unsafe, or not executable: $helper" >&2
        exit 1
    fi
done

expected_yt_dlp="$(sed -nE 's/^set\(YT_DLP_VERSION "([^"]+)".*/\1/p' "$pins")"
expected_deno="$(sed -nE 's/^set\(DENO_VERSION "([^"]+)".*/\1/p' "$pins")"
if [[ -z "$expected_yt_dlp" || -z "$expected_deno" ]]; then
    echo "bundled helper pins are missing" >&2
    exit 1
fi

actual_yt_dlp="$(env -i HOME="$helper_home" PATH=/usr/bin:/bin "$bin/yt-dlp" --version)"
deno_version_output="$(env -i HOME="$helper_home" PATH=/usr/bin:/bin "$bin/deno" --version)"
deno_version_line="${deno_version_output%%$'\n'*}"
if [[ "$actual_yt_dlp" != "$expected_yt_dlp" ]]; then
    echo "bundled yt-dlp version mismatch: expected $expected_yt_dlp, got $actual_yt_dlp" >&2
    exit 1
fi
case "$deno_version_line" in
    "deno $expected_deno"|"deno $expected_deno "*) ;;
    *)
        echo "bundled Deno version mismatch: expected $expected_deno, got $deno_version_line" >&2
        exit 1
        ;;
esac

if [[ "$mode" == "signed-runtime" ]]; then
    for helper in mpv ffmpeg ffprobe; do
        if [[ ! -x "$bin/$helper" || -L "$bin/$helper" ]]; then
            echo "bundled helper is missing, unsafe, or not executable: $helper" >&2
            exit 1
        fi
    done
    env -i HOME="$helper_home" PATH=/usr/bin:/bin "$bin/mpv" --version >/dev/null
    env -i HOME="$helper_home" PATH=/usr/bin:/bin "$bin/ffmpeg" -version >/dev/null
    env -i HOME="$helper_home" PATH=/usr/bin:/bin "$bin/ffprobe" -version >/dev/null
fi

echo "Verified bundled helper versions ($mode): yt-dlp $actual_yt_dlp; $deno_version_line"
