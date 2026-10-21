#!/bin/zsh
set -eu
setopt pipe_fail null_glob

(( $# == 1 )) || {
    print -u2 -r -- "usage: $0 <macos-dmg-contract-script>"
    exit 2
}

readonly contract_script="$1"
[[ "$contract_script" == /* && -f "$contract_script" ]] || {
    print -u2 -r -- "DMG contract script must be an absolute file path"
    exit 2
}

temporary_base="${TMPDIR:-/tmp}"
test_root="$(/usr/bin/mktemp -d "$temporary_base/owl-switch-dmg-contract-test.XXXXXX")"
cleanup() {
    if [[ -n "$test_root" && "$test_root" == "$temporary_base"/owl-switch-dmg-contract-test.* ]]; then
        /bin/rm -rf -- "$test_root"
    fi
}
trap cleanup EXIT INT TERM

fail() {
    print -u2 -r -- "$1"
    exit 1
}

expect_failure() {
    local label="$1"
    shift
    if "$@" >/dev/null 2>&1; then
        fail "expected failure: $label"
    fi
}

make_valid_layout() {
    local root="$1"
    /bin/mkdir -p "$root/OwlSwitch.app"
    /bin/ln -s OwlSwitch.app "$root/240-mp-jellyfin.app"
    /bin/ln -s /Applications "$root/Applications"
}

source_app="$test_root/source/OwlSwitch.app"
/bin/mkdir -p "$source_app/Contents"
stage_root="$test_root/staged"
/bin/zsh "$contract_script" stage "$source_app" "$stage_root"
/bin/zsh "$contract_script" validate-layout "$stage_root"

staged_entries=("$stage_root"/*(DN))
(( ${#staged_entries} == 3 )) || fail "staged layout did not contain exactly three entries"
[[ "$(/usr/bin/readlink "$stage_root/Applications")" == "/Applications" ]] ||
    fail "staged Applications shortcut was redirected"
[[ "$(/usr/bin/readlink "$stage_root/240-mp-jellyfin.app")" == "OwlSwitch.app" ]] ||
    fail "staged legacy updater alias was redirected"

existing_stage="$test_root/existing-stage"
/bin/mkdir "$existing_stage"
expect_failure "existing staging output" \
    /bin/zsh "$contract_script" stage "$source_app" "$existing_stage"

missing_link="$test_root/missing-link"
make_valid_layout "$missing_link"
/bin/rm "$missing_link/Applications"
expect_failure "missing Applications shortcut" \
    /bin/zsh "$contract_script" validate-layout "$missing_link"

redirected_link="$test_root/redirected-link"
make_valid_layout "$redirected_link"
/bin/rm "$redirected_link/Applications"
/bin/ln -s /tmp "$redirected_link/Applications"
expect_failure "redirected Applications shortcut" \
    /bin/zsh "$contract_script" validate-layout "$redirected_link"

directory_link="$test_root/directory-link"
make_valid_layout "$directory_link"
/bin/rm "$directory_link/Applications"
/bin/mkdir "$directory_link/Applications"
expect_failure "Applications directory instead of shortcut" \
    /bin/zsh "$contract_script" validate-layout "$directory_link"

extra_entry="$test_root/extra-entry"
make_valid_layout "$extra_entry"
/usr/bin/touch "$extra_entry/.DS_Store"
expect_failure "extra top-level entry" \
    /bin/zsh "$contract_script" validate-layout "$extra_entry"

linked_app="$test_root/linked-app"
/bin/mkdir "$linked_app"
/bin/ln -s /tmp "$linked_app/OwlSwitch.app"
/bin/ln -s OwlSwitch.app "$linked_app/240-mp-jellyfin.app"
/bin/ln -s /Applications "$linked_app/Applications"
expect_failure "symlinked app bundle" \
    /bin/zsh "$contract_script" validate-layout "$linked_app"

redirected_legacy_alias="$test_root/redirected-legacy-alias"
make_valid_layout "$redirected_legacy_alias"
/bin/rm "$redirected_legacy_alias/240-mp-jellyfin.app"
/bin/ln -s /tmp "$redirected_legacy_alias/240-mp-jellyfin.app"
expect_failure "redirected legacy updater alias" \
    /bin/zsh "$contract_script" validate-layout "$redirected_legacy_alias"

expect_failure "relative layout root" \
    /bin/zsh "$contract_script" validate-layout relative-layout
expect_failure "filesystem root" \
    /bin/zsh "$contract_script" validate-layout /

# Exercise the external verifier boundary without requiring a private signing
# identity or network fixture. A matching text header alone must never pass.
source "$contract_script"
fixture_signature_valid=true
fixture_timestamp=true
fixture_team_field="not set"
function /usr/bin/codesign() {
    if [[ "$1" == "--verify" ]]; then
        [[ "$*" == *"anchor apple generic"* &&
           "$*" == *'certificate leaf[subject.OU] = "PWT3Q52LZ2"'* &&
           "$*" == *"certificate leaf[field.1.2.840.113635.100.6.1.13] exists"* ]] ||
            return 1
        [[ "$fixture_signature_valid" == true ]] || return 1
        return 0
    fi
    [[ "$1" == "-dvvv" ]] || return 1
    print -r -- "Authority=Developer ID Application: Fixture (PWT3Q52LZ2)"
    print -r -- "TeamIdentifier=$fixture_team_field"
    if [[ "$fixture_timestamp" == true ]]; then
        print -r -- "Timestamp=Oct 21, 2026"
    fi
    return 0
}

verify_dmg_signature "$test_root/fixture.dmg" PWT3Q52LZ2 ||
    fail "valid certificate without CodeDirectory TeamIdentifier was rejected"
fixture_team_field=PWT3Q52LZ2
verify_dmg_signature "$test_root/fixture.dmg" PWT3Q52LZ2 ||
    fail "valid certificate with CodeDirectory TeamIdentifier was rejected"
expect_failure "different certificate team" \
    verify_dmg_signature "$test_root/fixture.dmg" AAAAAAAAAA
fixture_signature_valid=false
expect_failure "invalid signature despite matching displayed team" \
    verify_dmg_signature "$test_root/fixture.dmg" PWT3Q52LZ2
fixture_signature_valid=true
fixture_timestamp=false
expect_failure "missing secure timestamp" \
    verify_dmg_signature "$test_root/fixture.dmg" PWT3Q52LZ2
expect_failure "requirement injection in team" \
    verify_dmg_signature "$test_root/fixture.dmg" 'PWT3Q52LZ2" or true'
unfunction /usr/bin/codesign

print -r -- "macOS DMG contract fixtures passed"
