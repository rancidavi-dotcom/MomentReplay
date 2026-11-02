cmake_minimum_required(VERSION 3.21)
project(medal_linux VERSION 0.1.1 LANGUAGES CXX)

set(MOMENT_DISCORD_APPLICATION_ID "1565077771309555864" CACHE STRING "Public Discord application ID for Rich Presence")
set(MOMENT_GOOGLE_OAUTH_CLIENT_ID "" CACHE STRING "Google OAuth desktop client ID for Drive sharing")
set(MOMENT_GOOGLE_OAUTH_CLIENT_SECRET "" CACHE STRING "Google OAuth desktop client secret for this local build")
set(MOMENT_FRIENDS_API_URL "" CACHE STRING "Moment Friends service URL (leave empty until the service is deployed)")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

include(CTest)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickControls2 Multimedia Concurrent Network WebSockets)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
endif()
find_package(SQLite3 REQUIRED)
find_package(Qt6Keychain REQUIRED)
# Keep Linux desktop dependencies out of the Windows build.
add_library(moment-platform INTERFACE)
if(WIN32)
    set(MOMENT_SCREEN_SOURCE src/recording/windows_screencast.cpp src/recording/windows_screencast.h)
    set(MOMENT_SHORTCUT_SOURCE src/recording/windows_shortcuts.cpp src/recording/global_shortcuts.h)
    target_link_libraries(moment-platform INTERFACE user32)
    target_compile_definitions(moment-platform INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Qt6 REQUIRED COMPONENTS DBus)
    find_package(LayerShellQt 6.6 QUIET)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0)
    set(MOMENT_SCREEN_SOURCE src/recording/wayland_screencast.cpp src/recording/wayland_screencast.h)
    set(MOMENT_SHORTCUT_SOURCE src/recording/global_shortcuts.cpp src/recording/global_shortcuts.h)
    target_link_libraries(moment-platform INTERFACE Qt6::DBus PkgConfig::GSTREAMER)
else()
    message(FATAL_ERROR "Moment currently supports Linux and Windows")
endif()

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(medal-linux
    src/main.cpp
    src/editor/video_editor.cpp
    src/editor/video_editor.h
    src/share/google_drive_share.cpp
    src/share/google_drive_share.h
    src/update/update_checker.cpp
    src/update/update_checker.h
    src/friends/friends_client.cpp
    src/friends/friends_client.h
    src/desktop/tray_controller.cpp
    src/desktop/tray_controller.h
    src/desktop/capture_popup.cpp
    src/desktop/capture_popup.h
    src/desktop/discord_presence.cpp
    src/desktop/discord_presence.h
    src/library/clip_repository.cpp
    src/library/clip_repository.h
    src/library/clip_library_model.cpp
    src/library/clip_library_model.h
    src/recording/screen_recorder.cpp
    src/recording/screen_recorder.h
    src/recording/instant_replay.cpp
    src/recording/instant_replay.h
    ${MOMENT_SHORTCUT_SOURCE}
    src/recording/recording_preferences.cpp
    src/recording/recording_preferences.h
    src/recording/game_monitor.cpp
    src/recording/game_monitor.h
    ${MOMENT_SCREEN_SOURCE}
)

qt_add_qml_module(medal-linux
    URI MedalLinux
    VERSION 1.0
    QML_FILES qml/Main.qml qml/CapturePopup.qml qml/FriendsPage.qml qml/Localization.js
)

set_source_files_properties(packaging/io.moment.Moment.svg PROPERTIES QT_RESOURCE_ALIAS io.moment.Moment.svg)
qt_add_resources(medal-linux moment_icons PREFIX "/icons" FILES packaging/io.moment.Moment.svg)

if(LayerShellQt_FOUND)
    target_link_libraries(medal-linux PRIVATE LayerShellQt::Interface)
    target_compile_definitions(medal-linux PRIVATE MOMENT_HAS_LAYER_SHELL)
endif()

target_link_libraries(medal-linux PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
    Qt6::Concurrent
    Qt6::Network
    Qt6::WebSockets
    Qt6Keychain::Qt6Keychain
    moment-platform
    SQLite3::SQLite3
)

if(NOT MOMENT_DISCORD_APPLICATION_ID STREQUAL "")
    target_compile_definitions(medal-linux PRIVATE
        "MOMENT_DISCORD_APPLICATION_ID=\"${MOMENT_DISCORD_APPLICATION_ID}\"")
endif()

target_compile_definitions(medal-linux PRIVATE "MOMENT_VERSION=\"${PROJECT_VERSION}\"")

if(NOT MOMENT_FRIENDS_API_URL STREQUAL "")
    target_compile_definitions(medal-linux PRIVATE
        "MOMENT_FRIENDS_API_URL=\"${MOMENT_FRIENDS_API_URL}\"")
endif()

if(NOT MOMENT_GOOGLE_OAUTH_CLIENT_ID STREQUAL "")
    target_compile_definitions(medal-linux PRIVATE
        "MOMENT_GOOGLE_OAUTH_CLIENT_ID=\"${MOMENT_GOOGLE_OAUTH_CLIENT_ID}\"")
endif()

if(NOT MOMENT_GOOGLE_OAUTH_CLIENT_SECRET STREQUAL "")
    target_compile_definitions(medal-linux PRIVATE
        "MOMENT_GOOGLE_OAUTH_CLIENT_SECRET=\"${MOMENT_GOOGLE_OAUTH_CLIENT_SECRET}\"")
endif()

target_compile_options(medal-linux PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

if(WIN32)
    enable_language(RC)
    target_sources(medal-linux PRIVATE packaging/windows/Moment.rc)
    target_include_directories(medal-linux PRIVATE packaging/windows)
    set_target_properties(medal-linux PROPERTIES OUTPUT_NAME Moment WIN32_EXECUTABLE TRUE)
    install(TARGETS medal-linux RUNTIME DESTINATION .)
else()
    install(TARGETS medal-linux RUNTIME DESTINATION bin)
    install(FILES packaging/io.moment.Moment.desktop DESTINATION share/applications)
    install(FILES packaging/io.moment.Moment.svg DESTINATION share/icons/hicolor/scalable/apps)
endif()

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(main-window-visual-test
        tests/main_window_visual_test.cpp
        src/editor/video_editor.cpp src/editor/video_editor.h
        src/share/google_drive_share.cpp src/share/google_drive_share.h
        src/recording/game_monitor.cpp src/recording/game_monitor.h
        src/library/clip_repository.cpp src/library/clip_repository.h
        src/library/clip_library_model.cpp src/library/clip_library_model.h
        src/recording/screen_recorder.cpp src/recording/screen_recorder.h
        src/recording/instant_replay.cpp src/recording/instant_replay.h
        src/recording/recording_preferences.cpp src/recording/recording_preferences.h
        ${MOMENT_SCREEN_SOURCE})
    qt_add_resources(main-window-visual-test main_visual_qml PREFIX "/qt/qml/MedalLinux"
        FILES qml/Main.qml qml/FriendsPage.qml qml/Localization.js)
    qt_add_resources(main-window-visual-test moment_visual_icons PREFIX "/icons" FILES packaging/io.moment.Moment.svg)
    target_include_directories(main-window-visual-test PRIVATE src)
    target_link_libraries(main-window-visual-test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia Qt6::Concurrent Qt6::Network Qt6Keychain::Qt6Keychain Qt6::Test SQLite3::SQLite3 moment-platform)
    add_test(NAME main-window-visual-test COMMAND main-window-visual-test)
    set_tests_properties(main-window-visual-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

    qt_add_executable(google-drive-share-test
        tests/google_drive_share_test.cpp
        src/share/google_drive_share.cpp src/share/google_drive_share.h)
    target_include_directories(google-drive-share-test PRIVATE src)
    target_link_libraries(google-drive-share-test PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Test Qt6Keychain::Qt6Keychain)
    add_test(NAME google-drive-share-test COMMAND google-drive-share-test)

    if(NOT WIN32)
    find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
    qt_add_executable(global-shortcuts-test
        tests/global_shortcuts_test.cpp
        src/recording/global_shortcuts.cpp src/recording/global_shortcuts.h)
    target_include_directories(global-shortcuts-test PRIVATE src)
    target_link_libraries(global-shortcuts-test PRIVATE Qt6::Core Qt6::Gui Qt6::Test moment-platform)
    add_test(NAME global-shortcuts-test COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:global-shortcuts-test>)
    set_tests_properties(global-shortcuts-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(background-capture-test
        tests/background_capture_test.cpp
        src/recording/global_shortcuts.cpp src/recording/global_shortcuts.h
        src/recording/screen_recorder.cpp src/recording/screen_recorder.h
        src/recording/instant_replay.cpp src/recording/instant_replay.h
        src/recording/recording_preferences.cpp src/recording/recording_preferences.h
        ${MOMENT_SCREEN_SOURCE}
        src/library/clip_repository.cpp src/library/clip_repository.h
        src/library/clip_library_model.cpp src/library/clip_library_model.h)
    target_include_directories(background-capture-test PRIVATE src)
    target_link_libraries(background-capture-test PRIVATE Qt6::Core Qt6::Gui Qt6::Test
        Qt6::Multimedia Qt6::Concurrent SQLite3::SQLite3 moment-platform)
    add_test(NAME background-capture-test COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:background-capture-test>)
    set_tests_properties(background-capture-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    endif()

    if(WIN32)
        qt_add_executable(windows-shortcuts-test tests/windows_shortcuts_test.cpp ${MOMENT_SHORTCUT_SOURCE})
        target_include_directories(windows-shortcuts-test PRIVATE src)
        target_link_libraries(windows-shortcuts-test PRIVATE Qt6::Core Qt6::Gui Qt6::Test moment-platform)
        add_test(NAME windows-shortcuts-test COMMAND windows-shortcuts-test)
        qt_add_executable(windows-capture-test tests/windows_capture_test.cpp
            src/recording/screen_recorder.cpp src/recording/screen_recorder.h
            src/recording/instant_replay.cpp src/recording/instant_replay.h
            src/recording/recording_preferences.cpp src/recording/recording_preferences.h
            src/library/clip_repository.cpp src/library/clip_repository.h
            src/library/clip_library_model.cpp src/library/clip_library_model.h
            ${MOMENT_SCREEN_SOURCE})
        target_include_directories(windows-capture-test PRIVATE src)
        target_link_libraries(windows-capture-test PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets
            Qt6::Multimedia Qt6::Concurrent Qt6::Test SQLite3::SQLite3 moment-platform)
        add_test(NAME windows-capture-test COMMAND windows-capture-test)
    endif()

    qt_add_executable(capture-popup-test
        tests/capture_popup_test.cpp
        src/desktop/capture_popup.cpp src/desktop/capture_popup.h)
    qt_add_resources(capture-popup-test popup_test_qml PREFIX "/qt/qml/MedalLinux" FILES qml/CapturePopup.qml)
    target_include_directories(capture-popup-test PRIVATE src)
    target_link_libraries(capture-popup-test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
    if(LayerShellQt_FOUND)
        target_link_libraries(capture-popup-test PRIVATE LayerShellQt::Interface)
        target_compile_definitions(capture-popup-test PRIVATE MOMENT_HAS_LAYER_SHELL)
    endif()
    add_test(NAME capture-popup-test COMMAND capture-popup-test)
    set_tests_properties(capture-popup-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

    qt_add_executable(clip-library-tests
        tests/clip_repository_test.cpp
        src/library/clip_repository.cpp
        src/library/clip_repository.h
        src/library/clip_library_model.cpp
        src/library/clip_library_model.h
    )
    target_link_libraries(clip-library-tests PRIVATE Qt6::Core Qt6::Test SQLite3::SQLite3)
    target_compile_options(clip-library-tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
    )
    add_test(NAME clip-library-tests COMMAND clip-library-tests)

    qt_add_executable(media-player-test tests/media_player_test.cpp)
    target_link_libraries(media-player-test PRIVATE Qt6::Core Qt6::Gui Qt6::Multimedia Qt6::Test)
    target_compile_options(media-player-test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
    )
    add_test(NAME media-player-test COMMAND media-player-test)
    set_tests_properties(media-player-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(screen-recorder-test
        tests/screen_recorder_test.cpp
        src/recording/screen_recorder.cpp
        src/recording/screen_recorder.h
        src/recording/instant_replay.cpp
        src/recording/instant_replay.h
        src/recording/recording_preferences.cpp
        src/recording/recording_preferences.h
        ${MOMENT_SCREEN_SOURCE}
        src/library/clip_repository.cpp
        src/library/clip_repository.h
        src/library/clip_library_model.cpp
        src/library/clip_library_model.h
    )
    target_link_libraries(screen-recorder-test PRIVATE Qt6::Core Qt6::Gui Qt6::Multimedia Qt6::Concurrent Qt6::Test SQLite3::SQLite3 moment-platform)
    target_compile_options(screen-recorder-test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
    )
    add_test(NAME screen-recorder-test COMMAND screen-recorder-test)
    set_tests_properties(screen-recorder-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(recording-preferences-test
        tests/recording_preferences_test.cpp
        src/recording/recording_preferences.cpp
        src/recording/recording_preferences.h
    )
    target_link_libraries(recording-preferences-test PRIVATE Qt6::Core Qt6::Test)
    target_compile_options(recording-preferences-test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
    )
    add_test(NAME recording-preferences-test COMMAND recording-preferences-test)
    set_tests_properties(recording-preferences-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    if(NOT WIN32)
    qt_add_executable(game-monitor-test
        tests/game_monitor_test.cpp
        src/recording/game_monitor.cpp
        src/recording/game_monitor.h
    )
    target_link_libraries(game-monitor-test PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME game-monitor-test COMMAND game-monitor-test)

    qt_add_executable(discord-presence-test
        tests/discord_presence_test.cpp
        src/desktop/discord_presence.cpp src/desktop/discord_presence.h
        src/recording/game_monitor.cpp src/recording/game_monitor.h
    )
    target_link_libraries(discord-presence-test PRIVATE Qt6::Core Qt6::Network Qt6::Test)
    if(NOT MOMENT_DISCORD_APPLICATION_ID STREQUAL "")
        target_compile_definitions(discord-presence-test PRIVATE
            "MOMENT_DISCORD_APPLICATION_ID=\"${MOMENT_DISCORD_APPLICATION_ID}\"")
    endif()
    add_test(NAME discord-presence-test COMMAND discord-presence-test)

    endif()

    qt_add_executable(video-editor-test
        tests/video_editor_test.cpp
        src/editor/video_editor.cpp src/editor/video_editor.h
        src/library/clip_repository.cpp src/library/clip_repository.h
        src/library/clip_library_model.cpp src/library/clip_library_model.h
    )
    target_include_directories(video-editor-test PRIVATE src)
    target_link_libraries(video-editor-test PRIVATE Qt6::Core Qt6::Test SQLite3::SQLite3)
    add_test(NAME video-editor-test COMMAND video-editor-test)

    qt_add_executable(instant-replay-test
        tests/instant_replay_test.cpp
        src/recording/instant_replay.cpp
        src/recording/instant_replay.h
    )
    target_link_libraries(instant-replay-test PRIVATE Qt6::Core Qt6::Gui Qt6::Multimedia Qt6::Concurrent Qt6::Test)
    target_compile_options(instant-replay-test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
    )
    add_test(NAME instant-replay-test COMMAND instant-replay-test)
    set_tests_properties(instant-replay-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        qt_add_executable(update-checker-test
            tests/update_checker_test.cpp
            src/update/update_checker.cpp src/update/update_checker.h)
        target_include_directories(update-checker-test PRIVATE src)
        target_link_libraries(update-checker-test PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Test)
        add_test(NAME update-checker-test COMMAND update-checker-test)
    endif()
endif()
