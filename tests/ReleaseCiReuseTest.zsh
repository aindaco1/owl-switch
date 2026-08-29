#!/bin/zsh
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <source-root>" >&2
    exit 2
fi

source_root="${1:A}"
ci_workflow="$source_root/.github/workflows/ci.yml"
release_workflow="$source_root/.github/workflows/release.yml"
restore_script="$source_root/scripts/restore_ci_app.sh"
helper_verifier="$source_root/scripts/macos_verify_bundled_helpers.sh"

for required_fragment in \
    'branches: [main]' \
    'Package exact-commit unsigned app for release' \
    'actions/attest-build-provenance@4d101475d8b20a2381f78447822ac1eab6504dd8' \
    'actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a' \
    "github.event_name == 'push' && github.ref == 'refs/heads/main'" \
    'owl-switch-verified-app-${{ github.sha }}' \
    './scripts/package_ci_app.sh'; do
    if ! grep -Fq -- "$required_fragment" "$ci_workflow"; then
        echo "CI workflow is missing exact-commit app reuse control: $required_fragment" >&2
        exit 1
    fi
done

for required_fragment in \
    'actions: read' \
    'Restore exact successful CI app' \
    'DMG="owl-switch-${{ env.RELEASE_TAG }}-macOS-arm64.dmg"' \
    '--binary-identifier "com.aindaco1.owl-switch.dmg"' \
    './scripts/restore_ci_app.sh' \
    './scripts/macos_verify_bundled_helpers.sh'; do
    if ! grep -Fq -- "$required_fragment" "$release_workflow"; then
        echo "release workflow is missing exact-commit app reuse control: $required_fragment" >&2
        exit 1
    fi
done

for required_fragment in \
    '"deno $expected_deno"|"deno $expected_deno "*' \
    'mode" == "signed-runtime"' \
    'bundled Deno version mismatch'; do
    if ! grep -Fq -- "$required_fragment" "$helper_verifier"; then
        echo "helper verifier is missing robust Deno version validation: $required_fragment" >&2
        exit 1
    fi
done

release_build_step="$(awk '
    /build-macos-arm64:/ { capture = 1 }
    capture { print }
    /package-macos-arm64:/ { exit }
' "$release_workflow")"
for forbidden_fragment in \
    'cmake -B build' \
    'cmake --build build' \
    'ctest --test-dir build' \
    'macdeployqt'; do
    if [[ "$release_build_step" == *"$forbidden_fragment"* ]]; then
        echo "release still repeats CI build work: $forbidden_fragment" >&2
        exit 1
    fi
done

for required_fragment in \
    '--workflow CI' \
    '--commit "$commit" --branch main --event push --status success' \
    '--signer-workflow "github.com/$repository/.github/workflows/ci.yml"' \
    '--source-ref refs/heads/main' \
    '--source-digest "$commit"' \
    '--deny-self-hosted-runners'; do
    if ! grep -Fq -- "$required_fragment" "$restore_script"; then
        echo "CI app restore is missing provenance policy: $required_fragment" >&2
        exit 1
    fi
done

if rg -n 'xcodebuild -version[[:space:]]*\|[[:space:]]*head' \
    "$ci_workflow" "$release_workflow" \
    "$source_root/scripts/package_ci_app.sh"; then
    echo "Xcode version detection can terminate xcodebuild through a short pipe" >&2
    exit 1
fi

echo "release CI reuse tests passed"
