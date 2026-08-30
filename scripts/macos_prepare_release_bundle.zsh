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
app="$install_root/OwlSwitch.app"
macdeployqt_log="${TMPDIR:-/tmp}/owl-switch-macdeployqt.$$.log"
helper_home=""
helper_stage_root=""
qml_scan_root=""
cleanup() {
    rm -f "$macdeployqt_log"
    if [[ -n "$helper_stage_root" && -d "$helper_stage_root/bin" &&
          ! -e "$app/Contents/Resources/bin" ]]; then
        /bin/mkdir -p "$app/Contents/Resources"
        /bin/mv "$helper_stage_root/bin" "$app/Contents/Resources/bin"
    fi
    if [[ -n "$helper_stage_root" && -d "$helper_stage_root" &&
          "$helper_stage_root" == "${TMPDIR:-/tmp}"/owl-switch-helper-stage.* ]]; then
        /bin/rm -rf -- "$helper_stage_root"
    fi
    if [[ -n "$helper_home" && -d "$helper_home" &&
          "$helper_home" == "${TMPDIR:-/tmp}"/owl-switch-helper-home.* ]]; then
        /bin/rm -rf -- "$helper_home"
    fi
    if [[ -n "$qml_scan_root" && -d "$qml_scan_root" &&
          "$qml_scan_root" == "${TMPDIR:-/tmp}"/owl-switch-qml-scan.* ]]; then
        /bin/rm -rf -- "$qml_scan_root"
    fi
}
trap cleanup EXIT

# macdeployqt recursively scans -qmldir. Give it only source QML instead of the
# checkout root, which can contain large build trees and unrelated tooling.
qml_scan_root="$(mktemp -d "${TMPDIR:-/tmp}/owl-switch-qml-scan.XXXXXX")"
/bin/cp -p "$source_root/Main.qml" "$qml_scan_root/Main.qml"
for qml_source_dir in qml views modules; do
    [[ -d "$source_root/$qml_source_dir" ]] || continue
    /usr/bin/rsync -a --prune-empty-dirs \
        --include='*/' --include='*.qml' --include='*.js' --include='qmldir' \
        --exclude='*' \
        "$source_root/$qml_source_dir/" "$qml_scan_root/$qml_source_dir/"
done

# The helper bundle is already dependency-normalized by CMake. Keep it outside
# the app while macdeployqt performs its Qt-only pass so the multi-file yt-dlp
# runtime is not redundantly inspected as application code.
helper_stage_root="$(mktemp -d "${TMPDIR:-/tmp}/owl-switch-helper-stage.XXXXXX")"
/bin/mv "$app/Contents/Resources/bin" "$helper_stage_root/bin"

if ! "$qt_root/bin/macdeployqt" "$app" \
        -qmldir="$qml_scan_root" \
        -libpath="$qt_root/lib" \
        -libpath="$(brew --prefix brotli)/lib" \
        -libpath="$(brew --prefix webp)/lib" \
        -no-codesign \
        -verbose=1 >"$macdeployqt_log" 2>&1; then
    cat "$macdeployqt_log"
    exit 1
fi
/bin/mv "$helper_stage_root/bin" "$app/Contents/Resources/bin"
/bin/rmdir "$helper_stage_root"
helper_stage_root=""

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
    "$app" "$qml_scan_root" "$qml_import_scanner" "$qml_import_path"
"$source_root/scripts/macos_verify_no_qtquickcontrols.zsh" "$app"
"$source_root/scripts/macos_verify_bundle.zsh" "$app"
helper_home="$(mktemp -d "${TMPDIR:-/tmp}/owl-switch-helper-home.XXXXXX")"
"$source_root/scripts/macos_verify_bundled_helpers.sh" \
    pinned-only "$app/Contents/Resources/bin" \
    "$app/Contents/Resources/helper-manifest.json" "$helper_home"
