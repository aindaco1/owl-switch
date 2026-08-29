#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 6 ]]; then
    echo "usage: $0 <absolute-app> <absolute-output.tar.gz> <owner/repository> <commit> <run-id> <run-attempt>" >&2
    exit 64
fi

app="$1"
archive="$2"
repository="$3"
commit="$4"
run_id="$5"
run_attempt="$6"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ "$app" != /* || "$archive" != /* || \
      ! "$repository" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ || \
      ! "$commit" =~ ^[a-f0-9]{40}$ || \
      ! "$run_id" =~ ^[1-9][0-9]*$ || ! "$run_attempt" =~ ^[1-9][0-9]*$ ]]; then
    echo "invalid CI app artifact input" >&2
    exit 64
fi
if [[ ! -d "$app/Contents" || -L "$app" || -e "$archive" || -L "$archive" || \
      ! -d "$(dirname "$archive")" || -L "$(dirname "$archive")" ]]; then
    echo "CI app artifact input or output is missing or unsafe" >&2
    exit 1
fi
if [[ "$(git -C "$repo_root" rev-parse HEAD)" != "$commit" ]] || \
    ! git -C "$repo_root" diff --quiet -- . || \
    ! git -C "$repo_root" diff --cached --quiet -- .; then
    echo "CI app artifact checkout is not the exact clean source commit" >&2
    exit 1
fi
if [[ -z "${QT_ROOT_DIR:-}" || ! -x "$QT_ROOT_DIR/bin/qtpaths" ]]; then
    echo "QT_ROOT_DIR does not identify the CI Qt installation" >&2
    exit 1
fi

"$repo_root/scripts/macos_verify_bundle.zsh" "$app"

work_root="$(mktemp -d "${TMPDIR:-/tmp}/240-mp-jellyfin-ci-app-package.XXXXXX")"
cleanup() {
    /bin/rm -rf -- "$work_root"
}
trap cleanup EXIT
bundle_root="$work_root/240-mp-jellyfin-ci-app"
mkdir -p "$bundle_root/app"
ditto --norsrc --noextattr "$app" "$bundle_root/app/OwlSwitch.app"

app_manifest="$({
    python3 "$repo_root/scripts/ci_app_artifact.py" manifest \
        "$bundle_root/app/OwlSwitch.app"
})"
source_tree="$(git -C "$repo_root" rev-parse 'HEAD^{tree}')"
packaging_contract_sha256="$({
    cd "$repo_root"
    shasum -a 256 \
        CMakeLists.txt \
        cmake/BundledHelpers.cmake \
        cmake/helper-manifest.json.in \
        scripts/ci_app_artifact.py \
        scripts/macos_bundle_tool_deps.zsh \
        scripts/macos_prepare_release_bundle.zsh \
        scripts/macos_prune_qt_deployment.zsh \
        scripts/macos_verify_bundled_helpers.sh \
        scripts/macos_youtube_canary.sh \
        scripts/macos_verify_bundle.zsh \
        scripts/package_ci_app.sh
} | shasum -a 256 | awk '{print $1}')"
xcode_version_output="$(xcodebuild -version)"
xcode_version="${xcode_version_output%%$'\n'*}"
cmake_version_output="$(cmake --version)"
cmake_version="${cmake_version_output%%$'\n'*}"
qt_version="$("$QT_ROOT_DIR/bin/qtpaths" --query QT_VERSION)"
macos_version="$(sw_vers -productVersion)"

jq -n \
    --arg schema "240-mp-jellyfin-ci-app-v1" \
    --arg repository "$repository" \
    --arg commit "$commit" \
    --arg workflow ".github/workflows/ci.yml" \
    --argjson runID "$run_id" \
    --argjson runAttempt "$run_attempt" \
    --arg runner "github-hosted" \
    --arg xcodeVersion "$xcode_version" \
    --arg macOSVersion "$macos_version" \
    --arg cmakeVersion "$cmake_version" \
    --arg qtVersion "$qt_version" \
    --arg sourceTree "$source_tree" \
    --arg packagingContractSHA256 "$packaging_contract_sha256" \
    --arg appTreeSHA256 "$(jq -r .sha256 <<<"$app_manifest")" \
    --argjson appEntryCount "$(jq -r .entries <<<"$app_manifest")" \
    --argjson appUncompressedBytes "$(jq -r .bytes <<<"$app_manifest")" \
    '{
      schema: $schema,
      repository: $repository,
      commit: $commit,
      workflow: $workflow,
      runID: $runID,
      runAttempt: $runAttempt,
      runner: $runner,
      xcodeVersion: $xcodeVersion,
      macOSVersion: $macOSVersion,
      cmakeVersion: $cmakeVersion,
      qtVersion: $qtVersion,
      sourceTree: $sourceTree,
      packagingContractSHA256: $packagingContractSHA256,
      appTreeSHA256: $appTreeSHA256,
      appEntryCount: $appEntryCount,
      appUncompressedBytes: $appUncompressedBytes
    }' > "$bundle_root/metadata.json"
chmod 0644 "$bundle_root/metadata.json"

COPYFILE_DISABLE=1 tar -czf "$archive" -C "$work_root" 240-mp-jellyfin-ci-app
chmod 0644 "$archive"
echo "$archive"
