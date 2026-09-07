#!/bin/zsh
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <App.app>" >&2
    exit 2
fi

app="$1"
if [[ ! -d "$app/Contents" ]]; then
    echo "App bundle not found: $app" >&2
    exit 1
fi

if [[ -x "$app/Contents/Resources/bin/mpv" ]]; then
    /usr/bin/python3 - "$app" <<'PY'
import json, pathlib, sys
app = pathlib.Path(sys.argv[1]).resolve()
manifest = app / "Contents/Resources/vulkan/icd.d/MoltenVK_icd.json"
driver = app / "Contents/Frameworks/libMoltenVK.dylib"
license = app / "Contents/Resources/licenses/LICENSE.MoltenVK"
for path in (manifest, driver, license):
    if not path.is_file() or path.is_symlink() or not path.stat().st_size:
        sys.exit(f"Missing or unsafe bundled Vulkan runtime file: {path}")
data = json.loads(manifest.read_text())
if data["ICD"]["library_path"] != "../../../Frameworks/libMoltenVK.dylib":
    sys.exit("Bundled Vulkan manifest must select the enclosed MoltenVK driver")
if (manifest.parent / data["ICD"]["library_path"]).resolve() != driver:
    sys.exit("Bundled Vulkan driver escapes its expected location")
print("Verified bundled MoltenVK driver, manifest, and license")
PY
    lipo -archs "$app/Contents/Frameworks/libMoltenVK.dylib" | grep -qw arm64
fi

if find "$app" -name .DS_Store -print -quit | grep -q .; then
    echo "App bundle contains .DS_Store files" >&2
    exit 1
fi

errors=0
while IFS= read -r link; do
    if [[ ! -e "$link" ]]; then
        echo "Broken bundle symlink: $link -> $(readlink "$link")" >&2
        (( ++errors ))
    fi
done < <(find "$app" -type l -print)

mach_o_count=0
while IFS= read -r binary; do
    file -b "$binary" | grep -q 'Mach-O' || continue
    (( ++mach_o_count ))

    typeset -A install_ids
    install_ids=()
    while IFS= read -r install_id; do
        [[ -n "$install_id" ]] && install_ids[$install_id]=1
    done < <(otool -D "$binary" | awk '
        NR == 1 || /\(architecture [^)]+\):$/ { next }
        /^(\/|@)/ { print }
    ')

    while IFS= read -r dependency; do
        [[ -z "$dependency" || "$dependency" == @* || "$dependency" == /System/* || "$dependency" == /usr/lib/* ]] && continue
        [[ -n "${install_ids[$dependency]:-}" ]] && continue
        echo "External Mach-O dependency: $binary -> $dependency" >&2
        (( ++errors ))
    done < <(otool -L "$binary" | awk '/^[[:space:]]+(\/|@)/ { print $1 }')

    while IFS= read -r rpath; do
        [[ -z "$rpath" || "$rpath" == @* || "$rpath" == /System/* || "$rpath" == /usr/lib/* ]] && continue
        echo "External Mach-O rpath: $binary -> $rpath" >&2
        (( ++errors ))
    done < <(otool -l "$binary" | awk '
        $1 == "cmd" && $2 == "LC_RPATH" { in_rpath = 1; next }
        in_rpath && $1 == "path" { print $2; in_rpath = 0 }
    ')
done < <(find "$app" -type f -print)

if (( errors > 0 )); then
    echo "Bundle verification failed with $errors error(s)" >&2
    exit 1
fi

echo "Verified $mach_o_count Mach-O files with no external dependencies, external rpaths, broken symlinks, or .DS_Store files"
