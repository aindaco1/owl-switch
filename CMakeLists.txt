cmake_minimum_required(VERSION 3.21)
project(240mp VERSION 1.6.2 LANGUAGES CXX)

if(NOT APPLE)
    message(FATAL_ERROR "This fork is macOS-only. Configure and build it on macOS.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
# Keep generated moc includes relative to declared include directories. Besides
# making moc output reproducible, this avoids broken paths when the build tree
# is reached through a symlink such as macOS's /tmp -> /private/tmp.
set(CMAKE_AUTOMOC_PATH_PREFIX ON)
set(APP_DISPLAY_NAME "240-mp-jellyfin")
set(APP_RELEASE_TAG "v1.6.2")
set(APP_BUNDLE_IDENTIFIER "com.240mp.jellyfin" CACHE STRING "macOS bundle identifier")
set(APP_UPDATE_REPOSITORY "aindaco1/240-mp-jellyfin" CACHE STRING "GitHub update repository")
set(APP_UPDATE_TEAM_ID "PWT3Q52LZ2" CACHE STRING "Expected Developer ID team for updates")

include(GNUInstallDirs)
include(CTest)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml Network Svg)
find_package(OpenSSL REQUIRED)
find_program(MPV_EXECUTABLE NAMES mpv)
find_program(FFMPEG_EXECUTABLE NAMES ffmpeg)
find_program(FFPROBE_EXECUTABLE NAMES ffprobe)
include(cmake/BundledHelpers.cmake)

if(MPV_EXECUTABLE)
    get_filename_component(MPV_EXECUTABLE_REAL "${MPV_EXECUTABLE}" REALPATH)
    message(STATUS "mpv found and will be bundled for packaged macOS apps: ${MPV_EXECUTABLE_REAL}")
else()
    message(WARNING "mpv was not found. Packaged apps will still build, but playback will require mpv on PATH.")
endif()

if(FFPROBE_EXECUTABLE)
    get_filename_component(FFPROBE_EXECUTABLE_REAL "${FFPROBE_EXECUTABLE}" REALPATH)
    message(STATUS "ffprobe found and will be bundled for packaged macOS apps: ${FFPROBE_EXECUTABLE_REAL}")
else()
    message(WARNING "ffprobe was not found. Packaged apps will still run, but local track probing will require ffprobe on PATH.")
endif()

if(FFMPEG_EXECUTABLE)
    get_filename_component(FFMPEG_EXECUTABLE_REAL "${FFMPEG_EXECUTABLE}" REALPATH)
    message(STATUS "ffmpeg found and will be bundled for Karaoke prefetch: ${FFMPEG_EXECUTABLE_REAL}")
else()
    message(WARNING "ffmpeg was not found. Karaoke will play normally, but high-quality background prefetch will be unavailable.")
endif()

enable_language(OBJCXX)

add_executable(240mp
    src/main.cpp
    src/AppCore.cpp
    src/display/DisplaySelection.cpp
    src/modules/local_files/LocalFilesBackend.cpp
    src/modules/plex/PlexBackend.cpp
    src/modules/jellyfin/JellyfinBackend.cpp
    src/modules/karaoke/KaraokeBackend.cpp
    src/modules/tumblr_screensaver/TumblrScreensaverBackend.cpp
    src/modules/nature/NatureBackend.cpp
    src/player/MpvController.cpp
    src/input/IdleTracker.cpp
    src/input/InputManager.mm
    src/update/UpdateManager.cpp
    src/tools/HelperResolver.cpp
    src/macos_utils.mm
)

add_library(app_build_config INTERFACE)
target_compile_definitions(app_build_config INTERFACE
    APP_VERSION="${PROJECT_VERSION}"
    APP_BUNDLE_IDENTIFIER="${APP_BUNDLE_IDENTIFIER}"
    APP_UPDATE_REPOSITORY="${APP_UPDATE_REPOSITORY}"
    APP_UPDATE_TEAM_ID="${APP_UPDATE_TEAM_ID}"
)

target_compile_definitions(240mp PRIVATE
    DEV_YT_DLP_EXECUTABLE="${YT_DLP_EXECUTABLE}"
    DEV_DENO_EXECUTABLE="${DENO_EXECUTABLE}"
    BUNDLED_YT_DLP_VERSION="${YT_DLP_VERSION}"
)

target_link_libraries(240mp PRIVATE
    app_build_config
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::Network
    Qt6::Svg
    OpenSSL::Crypto
    "-framework AppKit"
    "-framework CoreFoundation"
    "-framework IOKit"
    "-framework GameController"
)

target_include_directories(240mp PRIVATE
    src
)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    add_executable(app_core_tests
        tests/AppCoreTest.cpp
        src/AppCore.cpp
    )
    target_compile_definitions(app_core_tests PRIVATE
        TEST_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    )
    target_include_directories(app_core_tests PRIVATE src)
    target_link_libraries(app_core_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network Qt6::Test)
    add_test(NAME app_core COMMAND app_core_tests)

    add_executable(display_selection_tests
        tests/DisplaySelectionTest.cpp
        src/display/DisplaySelection.cpp
    )
    target_include_directories(display_selection_tests PRIVATE src)
    target_link_libraries(display_selection_tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME display_selection COMMAND display_selection_tests)

    add_executable(karaoke_backend_tests
        tests/KaraokeBackendTest.cpp
        src/modules/karaoke/KaraokeBackend.cpp
        src/tools/HelperResolver.cpp
    )
    target_compile_definitions(karaoke_backend_tests PRIVATE
        DEV_YT_DLP_EXECUTABLE="${YT_DLP_EXECUTABLE}"
        DEV_DENO_EXECUTABLE="${DENO_EXECUTABLE}"
        BUNDLED_YT_DLP_VERSION="${YT_DLP_VERSION}"
    )
    target_include_directories(karaoke_backend_tests PRIVATE src)
    target_link_libraries(karaoke_backend_tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME karaoke_backend COMMAND karaoke_backend_tests)

    add_executable(playback_selector_qml_tests
        tests/PlaybackSelectorQmlTest.cpp
    )
    target_compile_definitions(playback_selector_qml_tests PRIVATE
        TEST_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    )
    target_link_libraries(playback_selector_qml_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml Qt6::Test)
    add_test(NAME playback_selector_qml COMMAND playback_selector_qml_tests)

    add_executable(media_output_lease_tests
        tests/MediaOutputLeaseTest.cpp
    )
    target_compile_definitions(media_output_lease_tests PRIVATE
        TEST_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    )
    target_link_libraries(media_output_lease_tests PRIVATE Qt6::Core Qt6::Qml Qt6::Test)
    add_test(NAME media_output_lease COMMAND media_output_lease_tests)

    add_executable(mpv_controller_tests
        tests/MpvControllerTest.cpp
        src/player/MpvController.cpp
        src/tools/HelperResolver.cpp
        src/AppCore.cpp
    )
    target_include_directories(mpv_controller_tests PRIVATE src)
    target_link_libraries(mpv_controller_tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network Qt6::Test
    )
    add_test(NAME mpv_controller COMMAND mpv_controller_tests)

    add_executable(retro_tv_data_tests
        tests/RetroTvDataTest.cpp
    )
    target_compile_definitions(retro_tv_data_tests PRIVATE
        TEST_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    )
    target_link_libraries(retro_tv_data_tests PRIVATE Qt6::Core Qt6::Qml Qt6::Test)
    add_test(NAME retro_tv_data COMMAND retro_tv_data_tests)

    add_executable(local_files_backend_tests
        tests/LocalFilesBackendTest.cpp
        src/modules/local_files/LocalFilesBackend.cpp
        src/tools/HelperResolver.cpp
    )
    target_compile_definitions(local_files_backend_tests PRIVATE
        DEV_YT_DLP_EXECUTABLE="${YT_DLP_EXECUTABLE}"
        DEV_DENO_EXECUTABLE="${DENO_EXECUTABLE}"
    )
    target_include_directories(local_files_backend_tests PRIVATE src)
    target_link_libraries(local_files_backend_tests PRIVATE Qt6::Core Qt6::Network Qt6::Test)
    add_test(NAME local_files_backend COMMAND local_files_backend_tests)

    add_executable(update_manager_tests
        tests/UpdateManagerTest.cpp
        src/update/UpdateManager.cpp
    )
    target_include_directories(update_manager_tests PRIVATE src)
    target_link_libraries(update_manager_tests PRIVATE app_build_config Qt6::Core Qt6::Gui Qt6::Network Qt6::Test)
    add_test(NAME update_manager COMMAND update_manager_tests)

    add_executable(jellyfin_backend_tests
        tests/JellyfinBackendTest.cpp
        src/modules/jellyfin/JellyfinBackend.cpp
    )
    target_include_directories(jellyfin_backend_tests PRIVATE src)
    target_link_libraries(jellyfin_backend_tests PRIVATE Qt6::Core Qt6::Network Qt6::Test)
    add_test(NAME jellyfin_backend COMMAND jellyfin_backend_tests)

    add_executable(jellyfin_media_tests
        tests/JellyfinMediaTest.cpp
    )
    target_compile_definitions(jellyfin_media_tests PRIVATE
        TEST_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    )
    target_link_libraries(jellyfin_media_tests PRIVATE Qt6::Core Qt6::Qml Qt6::Test)
    add_test(NAME jellyfin_media COMMAND jellyfin_media_tests)

    add_executable(tumblr_screensaver_backend_tests
        tests/TumblrScreensaverBackendTest.cpp
        src/modules/tumblr_screensaver/TumblrScreensaverBackend.cpp
    )
    target_include_directories(tumblr_screensaver_backend_tests PRIVATE src)
    target_link_libraries(tumblr_screensaver_backend_tests PRIVATE Qt6::Core Qt6::Network Qt6::Test)
    add_test(NAME tumblr_screensaver_backend COMMAND tumblr_screensaver_backend_tests)

    add_executable(tumblr_media_qml_tests
        tests/TumblrMediaQmlTest.cpp
    )
    target_compile_definitions(tumblr_media_qml_tests PRIVATE
        TEST_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    )
    target_link_libraries(tumblr_media_qml_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml Qt6::Test)
    add_test(NAME tumblr_media_qml COMMAND tumblr_media_qml_tests)

    add_executable(nature_backend_tests
        tests/NatureBackendTest.cpp
        src/modules/nature/NatureBackend.cpp
    )
    target_include_directories(nature_backend_tests PRIVATE src)
    target_link_libraries(nature_backend_tests PRIVATE
        Qt6::Core Qt6::Network Qt6::Test "-framework CoreFoundation"
    )
    add_test(NAME nature_backend COMMAND nature_backend_tests)

    add_test(
        NAME macos_dmg_contract
        COMMAND /bin/zsh
            "${CMAKE_SOURCE_DIR}/tests/MacosDmgContractTest.zsh"
            "${CMAKE_SOURCE_DIR}/scripts/macos_dmg_contract.zsh"
    )

    add_test(
        NAME release_metadata
        COMMAND "${CMAKE_COMMAND}"
            "-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}"
            "-DRELEASE_VERSION=${PROJECT_VERSION}"
            "-DRELEASE_TAG=${APP_RELEASE_TAG}"
            -P "${CMAKE_SOURCE_DIR}/tests/ReleaseMetadataTest.cmake"
    )

    add_test(
        NAME ci_app_artifact
        COMMAND /usr/bin/python3
            "${CMAKE_SOURCE_DIR}/tests/CiAppArtifactTest.py"
            "${CMAKE_SOURCE_DIR}/scripts/ci_app_artifact.py"
    )

    add_test(
        NAME release_ci_reuse
        COMMAND /bin/zsh
            "${CMAKE_SOURCE_DIR}/tests/ReleaseCiReuseTest.zsh"
            "${CMAKE_SOURCE_DIR}"
    )

    add_test(
        NAME bundled_helper_verification
        COMMAND /bin/zsh
            "${CMAKE_SOURCE_DIR}/tests/BundledHelpersVerificationTest.zsh"
            "${CMAKE_SOURCE_DIR}/scripts/macos_verify_bundled_helpers.sh"
    )
endif()

# ── macOS bundle metadata ──────────────────────────────────────────────────────
set_target_properties(240mp PROPERTIES
    OUTPUT_NAME "${APP_DISPLAY_NAME}"
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "${APP_DISPLAY_NAME}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${APP_BUNDLE_IDENTIFIER}"
    MACOSX_BUNDLE_ICON_FILE "assets/images/240-mp"
    INSTALL_RPATH_USE_LINK_PATH TRUE
)

# ── Dev bundle symlink (macOS only, non-CI) ───────────────────────────────────
# Makes double-clicking the app bundle work during development by symlinking
# Contents/Resources → repo root so resolveAppRoot() finds the QML files.
# Skipped in CI (ENV{CI} is set by GitHub Actions) because cmake --install
# populates Resources with real files — a symlink in the build tree would be
# packaged into the DMG pointing to a CI path that doesn't exist on end-user Macs.
if(APPLE AND NOT DEFINED ENV{CI})
    add_custom_command(TARGET 240mp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove -f
            "$<TARGET_BUNDLE_CONTENT_DIR:240mp>/Resources"
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${CMAKE_SOURCE_DIR}"
            "$<TARGET_BUNDLE_CONTENT_DIR:240mp>/Resources"
        COMMENT "Symlinking repo root as bundle Resources (dev build)"
    )
endif()

# ── Install rules ──────────────────────────────────────────────────────────────
install(TARGETS 240mp BUNDLE DESTINATION . COMPONENT Runtime)

# Dev builds symlink Contents/Resources to the repo root. If a local
# cmake --install copies that symlink, subsequent install rules write back
# into the source tree. Replace it with a real Resources directory in the
# install image before copying app resources and helper tools.
install(CODE "
    set(installed_resources \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${APP_DISPLAY_NAME}.app/Contents/Resources\")
    execute_process(COMMAND /bin/rm -rf \"\${installed_resources}\")
    file(MAKE_DIRECTORY \"\${installed_resources}\")
")

# Use a hardcoded relative path — $<TARGET_BUNDLE_CONTENT_DIR> resolves to
# the build tree, not the install prefix, inside install() rules.
install(FILES "${CMAKE_SOURCE_DIR}/Main.qml"
    DESTINATION "${APP_DISPLAY_NAME}.app/Contents/Resources"
    COMPONENT Runtime)

install(DIRECTORY
    "${CMAKE_SOURCE_DIR}/views"
    "${CMAKE_SOURCE_DIR}/modules"
    "${CMAKE_SOURCE_DIR}/assets"
    DESTINATION "${APP_DISPLAY_NAME}.app/Contents/Resources"
    COMPONENT Runtime
    PATTERN ".DS_Store" EXCLUDE
    PATTERN "*.iconset" EXCLUDE)

install(FILES
    "${CMAKE_SOURCE_DIR}/scripts/media-keys.lua"
    "${CMAKE_SOURCE_DIR}/scripts/mpv-osc.lua"
    "${CMAKE_SOURCE_DIR}/scripts/screensaver.lua"
    DESTINATION "${APP_DISPLAY_NAME}.app/Contents/Resources/scripts"
    COMPONENT Runtime)

install(PROGRAMS "${YT_DLP_EXECUTABLE}"
    DESTINATION "${APP_DISPLAY_NAME}.app/Contents/Resources/bin"
    RENAME yt-dlp
    COMPONENT Runtime)

install(PROGRAMS "${DENO_EXECUTABLE}"
    DESTINATION "${APP_DISPLAY_NAME}.app/Contents/Resources/bin"
    RENAME deno
    COMPONENT Runtime)

install(FILES
    "${CMAKE_SOURCE_DIR}/LICENSE"
    ${BUNDLED_YT_DLP_LICENSES}
    "${BUNDLED_DENO_LICENSE}"
    DESTINATION "${APP_DISPLAY_NAME}.app/Contents/Resources/licenses"
    COMPONENT Runtime)

if(MPV_EXECUTABLE)
    install(CODE "
        execute_process(
            COMMAND /bin/zsh
                \"${CMAKE_SOURCE_DIR}/scripts/macos_bundle_tool_deps.zsh\"
                \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${APP_DISPLAY_NAME}.app\"
                \"${MPV_EXECUTABLE_REAL}\"
                \"Contents/Resources/bin\"
            RESULT_VARIABLE bundle_mpv_result
        )
        if(NOT bundle_mpv_result EQUAL 0)
            message(FATAL_ERROR \"Failed to bundle mpv into ${APP_DISPLAY_NAME}.app\")
        endif()
    ")
endif()

if(FFPROBE_EXECUTABLE)
    install(CODE "
        execute_process(
            COMMAND /bin/zsh
                \"${CMAKE_SOURCE_DIR}/scripts/macos_bundle_tool_deps.zsh\"
                \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${APP_DISPLAY_NAME}.app\"
                \"${FFPROBE_EXECUTABLE_REAL}\"
                \"Contents/Resources/bin\"
            RESULT_VARIABLE bundle_ffprobe_result
        )
        if(NOT bundle_ffprobe_result EQUAL 0)
            message(FATAL_ERROR \"Failed to bundle ffprobe into ${APP_DISPLAY_NAME}.app\")
        endif()
    ")
endif()

if(FFMPEG_EXECUTABLE)
    install(CODE "
        execute_process(
            COMMAND /bin/zsh
                \"${CMAKE_SOURCE_DIR}/scripts/macos_bundle_tool_deps.zsh\"
                \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${APP_DISPLAY_NAME}.app\"
                \"${FFMPEG_EXECUTABLE_REAL}\"
                \"Contents/Resources/bin\"
            RESULT_VARIABLE bundle_ffmpeg_result
        )
        if(NOT bundle_ffmpeg_result EQUAL 0)
            message(FATAL_ERROR \"Failed to bundle ffmpeg into ${APP_DISPLAY_NAME}.app\")
        endif()
    ")
endif()

# macdeployqt is NOT run here — it requires Qt frameworks to be present and
# is environment-specific. Run it manually in CI after cmake --install.

# ── CPack ──────────────────────────────────────────────────────────────────────
set(CPACK_PACKAGE_NAME "${APP_DISPLAY_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "240-mp-jellyfin Media Player")
set(CPACK_PACKAGE_VENDOR "240-mp-jellyfin")

set(CPACK_GENERATOR "DragNDrop")
set(CPACK_DMG_VOLUME_NAME "${APP_DISPLAY_NAME}")
set(CPACK_DMG_FORMAT "UDZO")

include(CPack)
