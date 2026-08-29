# Release pins are source-controlled authority, not sticky cache knobs. Keeping
# them out of CMakeCache ensures an existing developer build adopts a pin or
# layout update on its next ordinary configure.
set(YT_DLP_VERSION "2026.08.19")
set(YT_DLP_SHA256 "07e54b0865303c864006925913bce2604f8ee8cc6f18699bac9c309f9328a6d8")
set(DENO_VERSION "2.9.3")
set(DENO_SHA256 "1b2972f7ceb6df28d9600eab18d423bebb9aa18db02f01d7eb37a5b501482203")

set(YT_DLP_EXECUTABLE_OVERRIDE "" CACHE FILEPATH
    "Use an existing yt-dlp executable instead of downloading the pinned helper")
set(DENO_EXECUTABLE_OVERRIDE "" CACHE FILEPATH
    "Use an existing Deno executable instead of downloading the pinned helper")

set(_helper_cache "${CMAKE_BINARY_DIR}/bundled-helpers")
file(MAKE_DIRECTORY "${_helper_cache}")

function(download_verified url destination expected_sha256)
    if(EXISTS "${destination}")
        file(SHA256 "${destination}" actual_sha256)
        if(NOT actual_sha256 STREQUAL expected_sha256)
            message(STATUS "Removing helper with stale checksum: ${destination}")
            file(REMOVE "${destination}")
        endif()
    endif()

    if(NOT EXISTS "${destination}")
        message(STATUS "Downloading pinned helper asset: ${url}")
        file(DOWNLOAD "${url}" "${destination}"
            EXPECTED_HASH "SHA256=${expected_sha256}"
            TLS_VERIFY ON
            SHOW_PROGRESS
            STATUS download_status)
        list(GET download_status 0 download_code)
        list(GET download_status 1 download_message)
        if(NOT download_code EQUAL 0)
            file(REMOVE "${destination}")
            message(FATAL_ERROR "Could not download ${url}: ${download_message}")
        endif()
    endif()
endfunction()

if(YT_DLP_EXECUTABLE_OVERRIDE)
    if(NOT EXISTS "${YT_DLP_EXECUTABLE_OVERRIDE}")
        message(FATAL_ERROR
            "YT_DLP_EXECUTABLE_OVERRIDE does not exist: ${YT_DLP_EXECUTABLE_OVERRIDE}")
    endif()
    set(YT_DLP_EXECUTABLE "${YT_DLP_EXECUTABLE_OVERRIDE}")
    set(YT_DLP_RUNTIME_DIRECTORY "")
    set(YT_DLP_INSTALLED_RUNTIME_DIRECTORY "")
    set(YT_DLP_LAYOUT "standalone-override")
else()
    set(_yt_dlp_archive "${_helper_cache}/yt-dlp-${YT_DLP_VERSION}-macos.zip")
    set(_yt_dlp_dir "${_helper_cache}/yt-dlp-${YT_DLP_VERSION}-macos")
    set(_yt_dlp_path "${_yt_dlp_dir}/yt-dlp_macos")
    set(_yt_dlp_runtime_dir "${_yt_dlp_dir}/_internal")
    download_verified(
        "https://github.com/yt-dlp/yt-dlp/releases/download/${YT_DLP_VERSION}/yt-dlp_macos.zip"
        "${_yt_dlp_archive}"
        "${YT_DLP_SHA256}")
    if(NOT EXISTS "${_yt_dlp_path}" OR NOT IS_DIRECTORY "${_yt_dlp_runtime_dir}")
        file(REMOVE_RECURSE "${_yt_dlp_dir}")
        file(MAKE_DIRECTORY "${_yt_dlp_dir}")
        file(ARCHIVE_EXTRACT INPUT "${_yt_dlp_archive}" DESTINATION "${_yt_dlp_dir}")
    endif()
    if(NOT EXISTS "${_yt_dlp_path}" OR NOT EXISTS "${_yt_dlp_runtime_dir}/Python")
        message(FATAL_ERROR "Pinned yt-dlp onedir archive is missing its executable or runtime")
    endif()
    file(CHMOD "${_yt_dlp_path}" PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)
    set(YT_DLP_EXECUTABLE "${_yt_dlp_path}")
    set(YT_DLP_RUNTIME_DIRECTORY "${_yt_dlp_runtime_dir}")
    set(YT_DLP_INSTALLED_RUNTIME_DIRECTORY "bin/_internal")
    set(YT_DLP_LAYOUT "onedir")
endif()

if(DENO_EXECUTABLE_OVERRIDE)
    if(NOT EXISTS "${DENO_EXECUTABLE_OVERRIDE}")
        message(FATAL_ERROR
            "DENO_EXECUTABLE_OVERRIDE does not exist: ${DENO_EXECUTABLE_OVERRIDE}")
    endif()
    set(DENO_EXECUTABLE "${DENO_EXECUTABLE_OVERRIDE}")
else()
    set(_deno_archive "${_helper_cache}/deno-${DENO_VERSION}-aarch64-apple-darwin.zip")
    set(_deno_dir "${_helper_cache}/deno-${DENO_VERSION}-aarch64-apple-darwin")
    set(_deno_path "${_deno_dir}/deno")
    download_verified(
        "https://github.com/denoland/deno/releases/download/v${DENO_VERSION}/deno-aarch64-apple-darwin.zip"
        "${_deno_archive}"
        "${DENO_SHA256}")
    if(NOT EXISTS "${_deno_path}")
        file(REMOVE_RECURSE "${_deno_dir}")
        file(MAKE_DIRECTORY "${_deno_dir}")
        file(ARCHIVE_EXTRACT INPUT "${_deno_archive}" DESTINATION "${_deno_dir}")
    endif()
    if(NOT EXISTS "${_deno_path}")
        message(FATAL_ERROR "Pinned Deno archive did not contain a deno executable")
    endif()
    file(CHMOD "${_deno_path}" PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)
    set(DENO_EXECUTABLE "${_deno_path}")
endif()

set(_yt_dlp_licenses "${_helper_cache}/yt-dlp-THIRD_PARTY_LICENSES.txt")
download_verified(
    "https://raw.githubusercontent.com/yt-dlp/yt-dlp/${YT_DLP_VERSION}/THIRD_PARTY_LICENSES.txt"
    "${_yt_dlp_licenses}"
    "472aefe951c7db35e1657c1d13fd337140511ed6f2b329205105ad441c5a02b7")

set(_yt_dlp_license "${_helper_cache}/yt-dlp-LICENSE")
download_verified(
    "https://raw.githubusercontent.com/yt-dlp/yt-dlp/${YT_DLP_VERSION}/LICENSE"
    "${_yt_dlp_license}"
    "7e12e5df4bae12cb21581ba157ced20e1986a0508dd10d0e8a4ab9a4cf94e85c")

set(_deno_license "${_helper_cache}/deno-LICENSE.md")
download_verified(
    "https://raw.githubusercontent.com/denoland/deno/v${DENO_VERSION}/LICENSE.md"
    "${_deno_license}"
    "f62497fffecc0852960c8d3e6934b9db86d16396e9b604072e923892cae3a588")

set(BUNDLED_YT_DLP_LICENSES "${_yt_dlp_license}" "${_yt_dlp_licenses}")
set(BUNDLED_DENO_LICENSE "${_deno_license}")
