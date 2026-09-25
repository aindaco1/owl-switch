#!/usr/bin/env bash
set -euo pipefail
[[ $# == 3 ]] || { echo 'usage: sign_sparkle.sh APP IDENTITY KEYCHAIN' >&2; exit 64; }
framework="$1/Contents/Frameworks/Sparkle.framework"
version="$framework/Versions/B"
common=(--force --options runtime --timestamp --sign "$2" --keychain "$3")
codesign "${common[@]}" "$version/XPCServices/Installer.xpc"
codesign "${common[@]}" --preserve-metadata=entitlements "$version/XPCServices/Downloader.xpc"
codesign "${common[@]}" "$version/Autoupdate"
codesign "${common[@]}" "$version/Updater.app"
codesign "${common[@]}" "$framework"
codesign --verify --deep --strict "$framework"
