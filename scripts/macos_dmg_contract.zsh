#!/bin/zsh
set -eu
setopt pipe_fail null_glob

readonly APP_NAME="OwlSwitch.app"
readonly LEGACY_APP_ALIAS="240-mp-jellyfin.app"
readonly APPLICATIONS_LINK_NAME="Applications"
readonly APPLICATIONS_LINK_TARGET="/Applications"

fail() {
    print -u2 -r -- "$1"
    return 1
}

require_specific_absolute_path() {
    local path_value="$1"
    local description="$2"
    [[ "$path_value" == /* && "$path_value" != "/" ]] ||
        fail "$description must be a specific absolute path"
}

validate_layout() {
    local layout_root="$1"
    require_specific_absolute_path "$layout_root" "DMG layout root"
    [[ -d "$layout_root" && ! -L "$layout_root" ]] ||
        fail "DMG layout root is missing or unsafe: $layout_root"

    local -a entries
    entries=("$layout_root"/*(DN))
    (( ${#entries} == 3 )) ||
        fail "DMG layout must contain the app, updater alias, and Applications shortcut"

    local saw_app=false
    local saw_legacy_alias=false
    local saw_applications=false
    local entry
    for entry in "${entries[@]}"; do
        case "${entry:t}" in
            "$APP_NAME") saw_app=true ;;
            "$LEGACY_APP_ALIAS") saw_legacy_alias=true ;;
            "$APPLICATIONS_LINK_NAME") saw_applications=true ;;
            *) fail "unexpected top-level DMG entry: ${entry:t}" ;;
        esac
    done
    [[ "$saw_app" == true && "$saw_legacy_alias" == true &&
       "$saw_applications" == true ]] ||
        fail "DMG layout is missing a required entry"

    local app_path="$layout_root/$APP_NAME"
    [[ -d "$app_path" && ! -L "$app_path" ]] ||
        fail "DMG app must be a real directory"

    local legacy_alias="$layout_root/$LEGACY_APP_ALIAS"
    [[ -L "$legacy_alias" ]] ||
        fail "DMG updater compatibility entry must be a symbolic link"
    [[ "$(/usr/bin/readlink "$legacy_alias")" == "$APP_NAME" ]] ||
        fail "DMG updater compatibility entry must point exactly to $APP_NAME"

    local applications_link="$layout_root/$APPLICATIONS_LINK_NAME"
    [[ -L "$applications_link" ]] ||
        fail "DMG Applications entry must be a symbolic link"
    [[ "$(/usr/bin/readlink "$applications_link")" == "$APPLICATIONS_LINK_TARGET" ]] ||
        fail "DMG Applications shortcut must point exactly to $APPLICATIONS_LINK_TARGET"
}

stage_layout() {
    local source_app="$1"
    local staging_root="$2"
    require_specific_absolute_path "$source_app" "source app"
    require_specific_absolute_path "$staging_root" "DMG staging root"
    [[ -d "$source_app" && ! -L "$source_app" ]] ||
        fail "source app is missing or unsafe: $source_app"
    [[ ! -e "$staging_root" && ! -L "$staging_root" ]] ||
        fail "refusing to replace existing DMG staging output: $staging_root"

    /bin/mkdir -p "$staging_root"
    /usr/bin/ditto --norsrc --noextattr "$source_app" "$staging_root/$APP_NAME"
    /bin/ln -s "$APP_NAME" "$staging_root/$LEGACY_APP_ALIAS"
    /usr/bin/chflags -h hidden "$staging_root/$LEGACY_APP_ALIAS"
    /bin/ln -s "$APPLICATIONS_LINK_TARGET" "$staging_root/$APPLICATIONS_LINK_NAME"
    validate_layout "$staging_root"
}

verify_dmg() {
    local verification_mode="$1"
    local dmg_path="$2"
    local expected_version="$3"
    local expected_team="$4"
    local expected_bundle="$5"

    [[ "$verification_mode" == "signed" || "$verification_mode" == "notarized" ]] ||
        fail "DMG verification mode must be signed or notarized"
    require_specific_absolute_path "$dmg_path" "DMG path"
    [[ -f "$dmg_path" && ! -L "$dmg_path" ]] ||
        fail "DMG artifact is missing or unsafe: $dmg_path"
    [[ -n "$expected_version" && -n "$expected_team" && -n "$expected_bundle" ]] ||
        fail "expected version, team, and bundle identifier are required"

    /usr/bin/hdiutil verify "$dmg_path"

    if [[ "$verification_mode" == "signed" ]]; then
        /usr/bin/codesign --verify --verbose=4 "$dmg_path"
        local dmg_signature
        dmg_signature="$(/usr/bin/codesign -dvvv "$dmg_path" 2>&1)"
        local actual_dmg_team
        actual_dmg_team="$(print -r -- "$dmg_signature" | /usr/bin/sed -n 's/^TeamIdentifier=//p')"
        [[ "$actual_dmg_team" == "$expected_team" ]] ||
            fail "DMG was signed by an unexpected developer"
        print -r -- "$dmg_signature" | /usr/bin/grep -q '^Timestamp=' ||
            fail "DMG signature has no secure timestamp"
    else
        /usr/bin/xcrun stapler validate "$dmg_path"
        /usr/sbin/spctl -a -t open --context context:primary-signature -vv "$dmg_path"
    fi

    local temporary_base="${TMPDIR:-/tmp}"
    require_specific_absolute_path "$temporary_base" "temporary directory"
    local work_root
    work_root="$(/usr/bin/mktemp -d "$temporary_base/owl-switch-dmg-verify.XXXXXX")"
    local mount_point="$work_root/mount"
    local mounted=false

    cleanup_verification() {
        if [[ "$mounted" == true ]]; then
            /usr/bin/hdiutil detach "$mount_point" >/dev/null 2>&1 ||
                /usr/bin/hdiutil detach -force "$mount_point" >/dev/null 2>&1 || true
        fi
        if [[ -n "$work_root" && "$work_root" == "$temporary_base"/owl-switch-dmg-verify.* ]]; then
            /bin/rm -rf -- "$work_root"
        fi
    }
    trap cleanup_verification EXIT INT TERM

    /bin/mkdir "$mount_point"
    /usr/bin/hdiutil attach -readonly -nobrowse -noautoopen \
        -mountpoint "$mount_point" "$dmg_path" >/dev/null
    mounted=true

    validate_layout "$mount_point"
    local app_path="$mount_point/$APP_NAME"
    /usr/bin/codesign --verify --deep --strict --verbose=4 "$app_path"
    /usr/bin/xcrun stapler validate "$app_path"
    /usr/sbin/spctl -a -t exec -vv "$app_path"

    local app_signature
    app_signature="$(/usr/bin/codesign -dvvv "$app_path" 2>&1)"
    local actual_app_team
    actual_app_team="$(print -r -- "$app_signature" | /usr/bin/sed -n 's/^TeamIdentifier=//p')"
    [[ "$actual_app_team" == "$expected_team" ]] ||
        fail "app was signed by an unexpected developer"

    local info_plist="$app_path/Contents/Info.plist"
    local actual_bundle
    actual_bundle="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$info_plist")"
    [[ "$actual_bundle" == "$expected_bundle" ]] ||
        fail "app bundle identifier does not match the release contract"

    local actual_version
    actual_version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$info_plist")"
    [[ "$actual_version" == "$expected_version" ]] ||
        fail "app version does not match the release contract"

    local executable
    executable="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$info_plist")"
    [[ -n "$executable" && -f "$app_path/Contents/MacOS/$executable" ]] ||
        fail "app executable is missing"
    /usr/bin/lipo -archs "$app_path/Contents/MacOS/$executable" | /usr/bin/grep -qw arm64 ||
        fail "app executable does not contain arm64"

    cleanup_verification
    trap - EXIT INT TERM
    print -r -- "verified $verification_mode DMG contract: $dmg_path"
}

usage() {
    print -u2 -r -- "usage: $0 stage <source-app> <staging-root>"
    print -u2 -r -- "       $0 validate-layout <layout-root>"
    print -u2 -r -- "       $0 verify-signed <dmg> <version> <team> <bundle-id>"
    print -u2 -r -- "       $0 verify-notarized <dmg> <version> <team> <bundle-id>"
    return 2
}

(( $# >= 1 )) || usage
command_name="$1"
shift
case "$command_name" in
    stage)
        (( $# == 2 )) || usage
        stage_layout "$1" "$2"
        ;;
    validate-layout)
        (( $# == 1 )) || usage
        validate_layout "$1"
        ;;
    verify-signed)
        (( $# == 4 )) || usage
        verify_dmg signed "$1" "$2" "$3" "$4"
        ;;
    verify-notarized)
        (( $# == 4 )) || usage
        verify_dmg notarized "$1" "$2" "$3" "$4"
        ;;
    *) usage ;;
esac
