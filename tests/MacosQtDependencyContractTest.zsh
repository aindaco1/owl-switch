#!/bin/zsh
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <verification-script>" >&2
    exit 2
fi

verification_script="${1:A}"
temporary_base="${TMPDIR:-/tmp}"
test_root="$(/usr/bin/mktemp -d "$temporary_base/owl-switch-qt-contract-test.XXXXXX")"
cleanup() {
    if [[ -n "$test_root" && "$test_root" == "$temporary_base"/owl-switch-qt-contract-test.* ]]; then
        /bin/rm -rf -- "$test_root"
    fi
}
trap cleanup EXIT

allowed="$test_root/Allowed.app"
/bin/mkdir -p "$allowed/Contents/Frameworks" "$allowed/Contents/Resources/qml/QtQuick"
"$verification_script" "$allowed" >/dev/null

forbidden_qml="$test_root/ForbiddenQml.app"
/bin/mkdir -p "$forbidden_qml/Contents/Resources/qml/QtQuick/Controls"
if "$verification_script" "$forbidden_qml" >/dev/null 2>&1; then
    echo "Qt Quick Controls QML directory was not rejected" >&2
    exit 1
fi

forbidden_framework="$test_root/ForbiddenFramework.app"
/bin/mkdir -p "$forbidden_framework/Contents/Frameworks/QtQuickControls2.framework"
if "$verification_script" "$forbidden_framework" >/dev/null 2>&1; then
    echo "Qt Quick Controls framework was not rejected" >&2
    exit 1
fi

echo "Qt dependency contract checks passed"
