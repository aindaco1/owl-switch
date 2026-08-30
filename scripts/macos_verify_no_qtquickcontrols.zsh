#!/bin/zsh
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <OwlSwitch.app>" >&2
    exit 2
fi

app="${1:A}"
if [[ ! -d "$app/Contents" ]]; then
    echo "App bundle not found: $app" >&2
    exit 1
fi

forbidden=$(find "$app/Contents" \
    \( -path '*/Resources/qml/QtQuick/Controls*' -o -iname '*QuickControls*' \) \
    -print -quit)
if [[ -n "$forbidden" ]]; then
    echo "Unused Qt Quick Controls dependency remains in packaged app: $forbidden" >&2
    exit 1
fi

echo "Qt Quick Controls is absent from the packaged app"
