#!/bin/zsh
set -euo pipefail

if [[ $# -ne 4 ]]; then
    echo "usage: $0 <build-dir> <install-root> <source-root> <Qt-root>" >&2
    exit 2
fi

build_dir="${1:A}"
install_root="${2:A}"
source_root="${3:A}"
qt_root="${4:A}"

if [[ ! -d "$build_dir" || ! -f "$build_dir/CMakeCache.txt" || \
      ! -d "$source_root" || ! -x "$qt_root/bin/macdeployqt" || \
      "$install_root" == / || -L "$install_root" ]]; then
    echo "invalid release-bundle preparation input" >&2
    exit 1
fi
if [[ -e "$install_root" ]]; then
    echo "release install root must not already exist: $install_root" >&2
    exit 1
fi

cmake --install "$build_dir" --prefix "$install_root"
app="$install_root/240-mp-jellyfin.app"
macdeployqt_log="${TMPDIR:-/tmp}/240-mp-jellyfin-macdeployqt.$$.log"
cleanup() {
    rm -f "$macdeployqt_log"
}
trap cleanup EXIT

if ! "$qt_root/bin/macdeployqt" "$app" \
        -qmldir="$source_root" \
        -libpath="$qt_root/lib" \
        -libpath="$(brew --prefix brotli)/lib" \
        -libpath="$(brew --prefix webp)/lib" \
        -no-codesign \
        -verbose=1 >"$macdeployqt_log" 2>&1; then
    cat "$macdeployqt_log"
    exit 1
fi

# macdeployqt can leave ad-hoc signatures on binaries whose load commands must
# still be normalized. Remove only those signatures before editing the rpaths.
while IFS= read -r binary; do
    file -b "$binary" | grep -q 'Mach-O' || continue
    external_rpath=$(otool -l "$binary" | awk '
      $1 == "cmd" && $2 == "LC_RPATH" { in_rpath = 1; next }
      in_rpath && $1 == "path" {
        if ($2 ~ /^\// && $2 !~ /^\/System\// && $2 !~ /^\/usr\/lib\//) {
          print $2
          exit
        }
        in_rpath = 0
      }
    ')
    if [[ -n "$external_rpath" ]]; then
        codesign --remove-signature "$binary" >/dev/null 2>&1 || true
    fi
done < <(find "$app" -type f -print)

qml_import_scanner="$("$qt_root/bin/qtpaths" --query QT_INSTALL_LIBEXECS)/qmlimportscanner"
qml_import_path="$("$qt_root/bin/qtpaths" --query QT_INSTALL_QML)"
"$source_root/scripts/macos_prune_qt_deployment.zsh" \
    "$app" "$source_root" "$qml_import_scanner" "$qml_import_path"
"$source_root/scripts/macos_verify_bundle.zsh" "$app"
