#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "usage: $0 <owner/repository> <commit> <absolute-app-destination>" >&2
    exit 64
fi

repository="$1"
commit="$2"
app_destination="$3"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ ! "$repository" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ || \
      ! "$commit" =~ ^[a-f0-9]{40}$ || "$app_destination" != /* || \
      "$app_destination" != *.app || -e "$app_destination" || \
      -L "$app_destination" ]]; then
    echo "invalid verified CI app destination" >&2
    exit 64
fi
for required_command in gh jq python3 shasum; do
    if ! command -v "$required_command" >/dev/null 2>&1; then
        echo "missing required CI app restore command: $required_command" >&2
        exit 1
    fi
done
if [[ "$(git -C "$repo_root" rev-parse HEAD)" != "$commit" ]] || \
    ! git -C "$repo_root" diff --quiet -- . || \
    ! git -C "$repo_root" diff --cached --quiet -- .; then
    echo "release checkout is not the exact clean source commit" >&2
    exit 1
fi

runs="$(
    gh run list --repo "$repository" --workflow CI \
        --commit "$commit" --branch main --event push --status success \
        --limit 10 \
        --json databaseId,attempt,conclusion,event,headBranch,headSha,status
)"
ci_run="$(
    jq -er --arg commit "$commit" '
      [ .[] | select(
          .headSha == $commit and .headBranch == "main" and
          .event == "push" and .status == "completed" and
          .conclusion == "success" and
          (.databaseId | type == "number") and
          (.attempt | type == "number")
      ) ]
      | if length == 0 then error("missing exact successful CI run")
        else sort_by(.databaseId) | last | [.databaseId, .attempt] | @tsv
        end
    ' <<<"$runs"
)"
ci_run_id="${ci_run%%$'\t'*}"
ci_run_attempt="${ci_run#*$'\t'}"

work_root="$(mktemp -d "${TMPDIR:-/tmp}/owl-switch-ci-app-restore.XXXXXX")"
cleanup() {
    /bin/rm -rf -- "$work_root"
}
trap cleanup EXIT
artifact_name="owl-switch-verified-app-$commit"
gh run download "$ci_run_id" --repo "$repository" \
    --name "$artifact_name" --dir "$work_root/download"
archive="$work_root/download/owl-switch-ci-app-$commit.tar.gz"
if [[ ! -f "$archive" || -L "$archive" ]]; then
    echo "exact CI app artifact is missing or unsafe" >&2
    exit 1
fi

gh attestation verify "$archive" \
    --repo "$repository" \
    --signer-workflow "github.com/$repository/.github/workflows/ci.yml" \
    --source-ref refs/heads/main \
    --source-digest "$commit" \
    --deny-self-hosted-runners >/dev/null

extract_root="$work_root/extracted"
python3 "$repo_root/scripts/ci_app_artifact.py" extract "$archive" "$extract_root"
bundle_root="$extract_root/owl-switch-ci-app"
metadata="$bundle_root/metadata.json"
app="$bundle_root/app/OwlSwitch.app"
source_tree="$(git -C "$repo_root" rev-parse 'HEAD^{tree}')"
if [[ ! -f "$metadata" || -L "$metadata" ]] || ! jq -e \
    --arg repository "$repository" \
    --arg commit "$commit" \
    --argjson runID "$ci_run_id" \
    --argjson runAttempt "$ci_run_attempt" \
    --arg sourceTree "$source_tree" '
      (keys | sort) == [
        "appEntryCount", "appTreeSHA256", "appUncompressedBytes",
        "cmakeVersion", "commit", "macOSVersion", "packagingContractSHA256",
        "qtVersion", "repository", "runAttempt", "runID", "runner", "schema",
        "sourceTree", "workflow", "xcodeVersion"
      ] and
      .schema == "owl-switch-ci-app-v1" and
      .repository == $repository and .commit == $commit and
      .workflow == ".github/workflows/ci.yml" and
      .runID == $runID and .runAttempt == $runAttempt and
      .runner == "github-hosted" and .xcodeVersion == "Xcode 26.3" and
      .sourceTree == $sourceTree and
      (.macOSVersion | type == "string" and test("^26\\.")) and
      (.cmakeVersion | type == "string" and test("^cmake version [0-9]+\\.")) and
      (.qtVersion | type == "string" and test("^6\\.")) and
      ([.packagingContractSHA256, .appTreeSHA256]
        | all(type == "string" and test("^[a-f0-9]{64}$"))) and
      (.appEntryCount | type == "number" and . > 0) and
      (.appUncompressedBytes | type == "number" and . > 0)
    ' "$metadata" >/dev/null; then
    echo "CI app provenance metadata is invalid" >&2
    exit 1
fi

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
app_manifest="$(python3 "$repo_root/scripts/ci_app_artifact.py" manifest "$app")"
if [[ "$packaging_contract_sha256" != "$(jq -r .packagingContractSHA256 "$metadata")" || \
      "$(jq -r .sha256 <<<"$app_manifest")" != "$(jq -r .appTreeSHA256 "$metadata")" || \
      "$(jq -r .entries <<<"$app_manifest")" != "$(jq -r .appEntryCount "$metadata")" || \
      "$(jq -r .bytes <<<"$app_manifest")" != "$(jq -r .appUncompressedBytes "$metadata")" ]]; then
    echo "CI app contents do not match their exact-commit metadata" >&2
    exit 1
fi

"$repo_root/scripts/macos_verify_bundle.zsh" "$app"
info_plist="$app/Contents/Info.plist"
executable_name="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$info_plist")"
actual_bundle="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$info_plist")"
actual_version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$info_plist")"
expected_bundle="$(sed -nE 's/^set\(APP_BUNDLE_IDENTIFIER "([^"]+)".*/\1/p' "$repo_root/CMakeLists.txt")"
expected_version="$(sed -nE 's/^project\(OwlSwitch VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$repo_root/CMakeLists.txt")"
if [[ "$actual_bundle" != "$expected_bundle" || "$actual_version" != "$expected_version" || \
      ! -x "$app/Contents/MacOS/$executable_name" || \
      "$(lipo -archs "$app/Contents/MacOS/$executable_name")" != "arm64" ]]; then
    echo "CI app identity, version, executable, or architecture is invalid" >&2
    exit 1
fi

bin="$app/Contents/Resources/bin"
helper_home="$work_root/helper-home"
"$repo_root/scripts/macos_verify_bundled_helpers.sh" \
    pinned-only "$bin" "$app/Contents/Resources/helper-manifest.json" "$helper_home"

mkdir -p "$(dirname "$app_destination")"
mv "$app" "$app_destination"
echo "restored verified unsigned app from exact CI run $ci_run_id"
