#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 4 ]]; then
    echo "usage: $0 <pinned-only|signed-runtime> <absolute-helper-bin> <absolute-helper-manifest.json> <absolute-helper-home>" >&2
    exit 64
fi

mode="$1"
bin="$2"
manifest="$3"
helper_home="$4"

if [[ "$mode" != "pinned-only" && "$mode" != "signed-runtime" ]] || \
   [[ "$bin" != /* || "$manifest" != /* || "$helper_home" != /* || \
      ! -d "$bin" || -L "$bin" || ! -f "$manifest" || -L "$manifest" ]]; then
    echo "invalid bundled helper verification input" >&2
    exit 64
fi
mkdir -p "$helper_home"

manifest_values="$(/usr/bin/python3 - "$manifest" <<'PY'
import json
import re
import sys

with open(sys.argv[1], "r", encoding="utf-8") as handle:
    data = json.load(handle)

if data.get("schemaVersion") != 1:
    raise SystemExit("unsupported helper manifest schema")
yt = data.get("ytDlp")
deno = data.get("deno")
runtime = data.get("runtimeHelpers")
if not isinstance(yt, dict) or not isinstance(deno, dict) or not isinstance(runtime, list):
    raise SystemExit("invalid helper manifest structure")

values = (
    yt.get("version"), yt.get("layout"), yt.get("archiveSha256"),
    yt.get("executable"), yt.get("runtimeDirectory"),
    deno.get("version"), deno.get("archiveSha256"), deno.get("executable"),
)
if not all(isinstance(value, str) for value in values):
    raise SystemExit("invalid helper manifest values")
if not re.fullmatch(r"[0-9]{4}\.[0-9]{2}\.[0-9]{2}", values[0]):
    raise SystemExit("invalid yt-dlp manifest version")
if values[1] != "onedir" or values[3] != "bin/yt-dlp" or values[4] != "bin/_internal":
    raise SystemExit("release helper manifest does not require yt-dlp onedir")
if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", values[5]):
    raise SystemExit("invalid Deno manifest version")
if values[7] != "bin/deno" or not all(re.fullmatch(r"[a-f0-9]{64}", value) for value in (values[2], values[6])):
    raise SystemExit("invalid helper manifest identity")
if runtime != ["bin/mpv", "bin/ffmpeg", "bin/ffprobe"]:
    raise SystemExit("invalid runtime helper manifest")
print("\t".join(values))
PY
)" || {
    echo "bundled helper manifest is invalid" >&2
    exit 1
}

IFS=$'\t' read -r expected_yt_dlp yt_dlp_layout yt_dlp_sha \
    yt_dlp_executable yt_dlp_runtime expected_deno deno_sha deno_executable \
    <<<"$manifest_values"

resources="$(cd "$bin/.." && pwd -P)"
manifest_canonical="$(cd "$(dirname "$manifest")" && pwd -P)/$(basename "$manifest")"
if [[ "$manifest_canonical" != "$resources/helper-manifest.json" ]]; then
    echo "helper manifest is outside the app Resources directory" >&2
    exit 1
fi

for helper in yt-dlp deno; do
    if [[ ! -x "$bin/$helper" || -L "$bin/$helper" ]]; then
        echo "bundled helper is missing, unsafe, or not executable: $helper" >&2
        exit 1
    fi
done
if [[ ! -d "$resources/$yt_dlp_runtime" || -L "$resources/$yt_dlp_runtime" || \
      ! -x "$resources/$yt_dlp_runtime/Python" || -L "$resources/$yt_dlp_runtime/Python" ]]; then
    echo "bundled yt-dlp onedir runtime is missing or unsafe" >&2
    exit 1
fi

actual_yt_dlp="$(env -i HOME="$helper_home" PATH=/usr/bin:/bin "$resources/$yt_dlp_executable" --version)"
deno_version_output="$(env -i HOME="$helper_home" PATH=/usr/bin:/bin "$resources/$deno_executable" --version)"
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

echo "Verified bundled helper versions ($mode): yt-dlp $actual_yt_dlp ($yt_dlp_layout); $deno_version_line"
