#!/bin/zsh
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <helper-verifier>" >&2
    exit 2
fi

verifier="${1:A}"
test_root="$(mktemp -d "${TMPDIR:-/tmp}/owl-switch-helper-verification-test.XXXXXX")"
cleanup() {
    /bin/rm -rf -- "$test_root"
}
trap cleanup EXIT

bin="$test_root/bin"
manifest="$test_root/helper-manifest.json"
helper_home="$test_root/home"
mkdir -p "$bin/_internal"
cat > "$manifest" <<'JSON'
{
  "schemaVersion": 1,
  "ytDlp": {
    "version": "2026.08.19",
    "layout": "onedir",
    "archiveSha256": "07e54b0865303c864006925913bce2604f8ee8cc6f18699bac9c309f9328a6d8",
    "executable": "bin/yt-dlp",
    "runtimeDirectory": "bin/_internal"
  },
  "deno": {
    "version": "2.9.3",
    "archiveSha256": "1b2972f7ceb6df28d9600eab18d423bebb9aa18db02f01d7eb37a5b501482203",
    "executable": "bin/deno"
  },
  "runtimeHelpers": ["bin/mpv", "bin/ffmpeg", "bin/ffprobe"]
}
JSON

write_helper() {
    local name="$1"
    local output="$2"
    print -r -- '#!/bin/sh' > "$bin/$name"
    if [[ "$name" == "deno" ]]; then
        print -r -- 'if [ "$1" = "eval" ]; then echo "Deno JavaScript execution passed"; exit 0; fi' >> "$bin/$name"
    fi
    print -r -- "printf '%s\\n' '$output'" >> "$bin/$name"
    chmod 0755 "$bin/$name"
}

write_helper yt-dlp '2026.08.19'
write_helper deno 'deno 2.9.3 (stable, release, aarch64-apple-darwin)'
write_helper mpv 'mpv 0.40.0'
write_helper ffmpeg 'ffmpeg version 8.0'
write_helper ffprobe 'ffprobe version 8.0'
write_helper _internal/Python 'Python 3.13'

mkdir -p "$bin/_internal/certifi"
ca_bundle="$bin/_internal/certifi/cacert.pem"
/usr/bin/openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
    -subj /CN=owl-switch-test-fixture -keyout "$test_root/test-key.pem" \
    -out "$ca_bundle" >/dev/null 2>&1
cp "$ca_bundle" "$test_root/saved-ca.pem"
/usr/bin/python3 - "$manifest" "$ca_bundle" <<'PY'
import hashlib, json, pathlib, sys
manifest = pathlib.Path(sys.argv[1])
data = json.loads(manifest.read_text())
data["tlsCaBundle"] = {
    "provider": "certifi", "version": "2026.10.19",
    "path": "bin/_internal/certifi/cacert.pem",
    "sha256": hashlib.sha256(pathlib.Path(sys.argv[2]).read_bytes()).hexdigest()
}
manifest.write_text(json.dumps(data))
PY

"$verifier" signed-runtime "$bin" "$manifest" "$helper_home" >/dev/null

cp "$bin/deno" "$test_root/working-deno"
cat > "$bin/deno" <<'SH'
#!/bin/sh
if [ "$1" = "eval" ]; then exit 133; fi
echo 'deno 2.9.3'
SH
if "$verifier" signed-runtime "$bin" "$manifest" "$helper_home" >/dev/null 2>&1; then
    echo "helper verifier accepted Deno that reports its version but cannot execute JavaScript" >&2
    exit 1
fi
cp "$test_root/working-deno" "$bin/deno"

write_helper deno 'deno 2.9.30 (stable, release, aarch64-apple-darwin)'
if "$verifier" pinned-only "$bin" "$manifest" "$helper_home" >/dev/null 2>&1; then
    echo "helper verifier accepted the wrong Deno semantic version" >&2
    exit 1
fi

write_helper deno 'deno 2.9.3'
"$verifier" pinned-only "$bin" "$manifest" "$helper_home" >/dev/null

write_helper yt-dlp '2026.08.20'
if "$verifier" pinned-only "$bin" "$manifest" "$helper_home" >/dev/null 2>&1; then
    echo "helper verifier accepted the wrong yt-dlp version" >&2
    exit 1
fi

write_helper yt-dlp '2026.08.19'
/bin/rm "$bin/mpv"
"$verifier" pinned-only "$bin" "$manifest" "$helper_home" >/dev/null
if "$verifier" signed-runtime "$bin" "$manifest" "$helper_home" >/dev/null 2>&1; then
    echo "signed-runtime verification accepted a missing runtime helper" >&2
    exit 1
fi

/bin/rm "$ca_bundle"
if "$verifier" pinned-only "$bin" "$manifest" "$helper_home" >/dev/null 2>&1; then
    echo "helper verifier accepted a missing TLS certificate store" >&2
    exit 1
fi
print -r -- 'not a certificate' > "$ca_bundle"
if "$verifier" pinned-only "$bin" "$manifest" "$helper_home" >/dev/null 2>&1; then
    echo "helper verifier accepted a malformed TLS certificate store" >&2
    exit 1
fi
cp "$test_root/saved-ca.pem" "$ca_bundle"
"$verifier" pinned-only "$bin" "$manifest" "$helper_home" >/dev/null
print -r -- "" >> "$ca_bundle"
if "$verifier" pinned-only "$bin" "$manifest" "$helper_home" >/dev/null 2>&1; then
    echo "helper verifier accepted a changed but parseable TLS certificate store" >&2
    exit 1
fi

echo "bundled helper verification tests passed"
