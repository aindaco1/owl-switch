#!/bin/zsh
set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
    echo "usage: $0 <App.app> <tool-path> [tool-destination-relative-to-app]" >&2
    exit 2
fi

app="$1"
tool="$2"
tool_rel="${3:-Contents/Resources/bin}"

if [[ ! -d "$app/Contents" ]]; then
    echo "App bundle not found: $app" >&2
    exit 1
fi

if [[ ! -f "$tool" || ! -r "$tool" || ( ! -x "$tool" && "$tool" != *.dylib ) ]]; then
    echo "Helper or dynamic library not found: $tool" >&2
    exit 1
fi

frameworks="$app/Contents/Frameworks"
tool_dir="$app/$tool_rel"
tool_name="${tool:t}"
tool_dest="$tool_dir/$tool_name"

mkdir -p "$frameworks" "$tool_dir"
cp -p "$tool" "$tool_dest"
chmod 755 "$tool_dest"

typeset -A processed
queue=("$tool_dest")

is_system_dependency() {
    local dep="$1"
    [[ "$dep" == /System/* || "$dep" == /usr/lib/* || "$dep" == @* ]]
}

dependencies_for() {
    local binary="$1"
    otool -L "$binary" | sed '1d' | awk '{print $1}'
}

rewrite_dependency() {
    local binary="$1"
    local old_path="$2"
    local copied_path="$3"
    local copied_name="${copied_path:t}"
    local new_path

    if [[ "$binary" == "$tool_dest" && "$tool_dir" != "$frameworks" ]]; then
        new_path="@loader_path/../../Frameworks/$copied_name"
    else
        new_path="@loader_path/$copied_name"
    fi

    install_name_tool -change "$old_path" "$new_path" "$binary"
}

remove_signature_if_present() {
    local binary="$1"
    if command -v codesign >/dev/null 2>&1; then
        codesign --remove-signature "$binary" >/dev/null 2>&1 || true
    fi
}

while (( ${#queue[@]} > 0 )); do
    binary="${queue[1]}"
    queue=("${queue[@]:1}")

    [[ -n "${processed[$binary]:-}" ]] && continue
    processed[$binary]=1
    remove_signature_if_present "$binary"

    if [[ "$binary" == *.dylib ]]; then
        install_name_tool -id "@rpath/${binary:t}" "$binary"
    fi

    while IFS= read -r dep; do
        [[ -z "$dep" ]] && continue
        is_system_dependency "$dep" && continue

        dep_name="${dep:t}"
        copied="$frameworks/$dep_name"
        if [[ ! -e "$copied" ]]; then
            cp -p "$dep" "$copied"
            chmod 755 "$copied"
            remove_signature_if_present "$copied"
            queue+=("$copied")
        fi

        rewrite_dependency "$binary" "$dep" "$copied"
    done < <(dependencies_for "$binary")
done

if command -v codesign >/dev/null 2>&1; then
    for dylib in "$frameworks"/*.dylib(N); do
        codesign --force --sign - "$dylib" >/dev/null 2>&1
    done
    codesign --force --sign - "$tool_dest" >/dev/null 2>&1
fi

echo "Bundled $tool_name and dynamic dependencies into $app"
