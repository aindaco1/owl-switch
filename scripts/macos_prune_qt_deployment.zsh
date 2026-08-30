#!/bin/zsh
set -euo pipefail

if [[ $# -ne 4 ]]; then
    echo "usage: $0 <App.app> <source-root> <qmlimportscanner> <Qt-QML-import-path>" >&2
    exit 2
fi

app="$1"
source_root="$2"
qmlimportscanner="$3"
qt_qml_path="$4"
qml_root="$app/Contents/Resources/qml"
quick_plugins="$app/Contents/PlugIns/quick"

if [[ ! -d "$app/Contents" || ! -d "$source_root" ]]; then
    echo "App bundle or source root not found" >&2
    exit 1
fi
if [[ ! -x "$qmlimportscanner" || ! -d "$qt_qml_path" ]]; then
    echo "Qt QML scanner or import path not found" >&2
    exit 1
fi
if ! command -v jq >/dev/null 2>&1; then
    echo "jq is required to read qmlimportscanner output" >&2
    exit 1
fi

typeset -A imported_plugins
while IFS= read -r plugin; do
    [[ -n "$plugin" ]] && imported_plugins[$plugin]=1
done < <("$qmlimportscanner" -rootPath "$source_root" -importPath "$qt_qml_path" \
    | jq -r '.[] | select(.plugin != null and .plugin != "") | .plugin')

removed_modules=0
if [[ -d "$qml_root" ]]; then
    module_dirs=()
    while IFS= read -r qmldir; do
        plugins=("${(@f)$(awk '
            $1 == "plugin" { print $2 }
            $1 == "optional" && $2 == "plugin" { print $3 }
        ' "$qmldir")}")
        (( ${#plugins[@]} == 0 )) && continue

        keep=false
        for plugin in "${plugins[@]}"; do
            if [[ -n "${imported_plugins[$plugin]:-}" ]]; then
                keep=true
                break
            fi
        done
        if [[ "$keep" == false ]]; then
            module_dirs+=("${qmldir:h}")
        fi
    done < <(find "$qml_root" -name qmldir -type f -print)

    for module_dir in "${module_dirs[@]}"; do
        [[ -d "$module_dir" ]] || continue
        rm -rf "$module_dir"
        (( ++removed_modules ))
    done
fi

typeset -A referenced_quick_plugins
if [[ -d "$qml_root" ]]; then
    while IFS= read -r link; do
        target=$(readlink "$link")
        [[ -n "$target" ]] && referenced_quick_plugins[${target:t}]=1
    done < <(find "$qml_root" -type l -name '*.dylib' -print)
fi

removed_plugins=0
if [[ -d "$quick_plugins" ]]; then
    for plugin in "$quick_plugins"/*.dylib(N); do
        plugin_name="${plugin:t}"
        if [[ -z "${referenced_quick_plugins[$plugin_name]:-}" ]]; then
            rm -f "$plugin"
            (( ++removed_plugins ))
        fi
    done
fi

# macdeployqt copies the framework closure before the unused QML modules and
# quick plugins above are removed. Recompute the Qt framework graph from the
# Mach-O files that remain, then remove only unreachable Qt frameworks. This
# keeps framework pruning derived from the same post-scan bundle rather than a
# hand-maintained list of style or controls dependencies.
framework_root="$app/Contents/Frameworks"
typeset -A available_frameworks
typeset -A required_frameworks
framework_queue=()
for framework in "$framework_root"/Qt*.framework(N/); do
    available_frameworks[${framework:t}]="$framework"
done

mark_framework_dependencies() {
    local binary="$1"
    local dependency framework_name
    file -b "$binary" | grep -q 'Mach-O' || return 0
    while IFS= read -r dependency; do
        if [[ "$dependency" =~ '([^/]+\.framework)(/|$)' ]]; then
            framework_name="${match[1]}"
            if [[ -n "${available_frameworks[$framework_name]:-}" &&
                  -z "${required_frameworks[$framework_name]:-}" ]]; then
                required_frameworks[$framework_name]=1
                framework_queue+=("$framework_name")
            fi
        fi
    done < <(otool -L "$binary" | tail -n +2 | awk '{ print $1 }')
}

while IFS= read -r binary; do
    mark_framework_dependencies "$binary"
done < <(find "$app" -path "$framework_root" -prune -o -type f -print)

queue_index=1
while (( queue_index <= ${#framework_queue[@]} )); do
    framework_name="${framework_queue[$queue_index]}"
    framework="${available_frameworks[$framework_name]}"
    while IFS= read -r binary; do
        if file -b "$binary" | grep -q 'Mach-O'; then
            mark_framework_dependencies "$binary"
            break
        fi
    done < <(find "$framework" -type f -print)
    (( ++queue_index ))
done

removed_frameworks=0
for framework_name framework in "${(@kv)available_frameworks}"; do
    if [[ -z "${required_frameworks[$framework_name]:-}" ]]; then
        rm -rf "$framework"
        (( ++removed_frameworks ))
    fi
done

updated_rpaths=0
while IFS= read -r binary; do
    file -b "$binary" | grep -q 'Mach-O' || continue
    removed_signature=false
    rpaths=("${(@f)$(otool -l "$binary" | awk '
        $1 == "cmd" && $2 == "LC_RPATH" { in_rpath = 1; next }
        in_rpath && $1 == "path" { print $2; in_rpath = 0 }
    ')}")
    for rpath in "${rpaths[@]}"; do
        [[ "$rpath" == /* && "$rpath" != /System/* && "$rpath" != /usr/lib/* ]] || continue
        if [[ "$removed_signature" == false ]]; then
            codesign --remove-signature "$binary" >/dev/null 2>&1 || true
            removed_signature=true
        fi
        install_name_tool -delete_rpath "$rpath" "$binary"
        (( ++updated_rpaths ))
    done
done < <(find "$app" -type f -print)

echo "Pruned $removed_modules unused QML modules, $removed_plugins quick plugins, and $removed_frameworks unreachable Qt frameworks; removed $updated_rpaths external rpaths"
