cmake_minimum_required(VERSION 3.21)
project(medal_linux VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickControls2 Multimedia Concurrent DBus Test)
find_package(SQLite3 REQUIRED)
find_package(LayerShellQt 6.6 QUIET)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(medal-linux
    src/main.cpp
    src/desktop/tray_controller.cpp
    src/desktop/tray_controller.h
    src/desktop/capture_popup.cpp
    src/desktop/capture_popup.h
    src/library/clip_repository.cpp
    src/library/clip_repository.h
    src/library/clip_library_model.cpp
    src/library/clip_library_model.h
    src/recording/screen_recorder.cpp
    src/recording/screen_recorder.h
    src/recording/instant_replay.cpp
    src/recording/instant_replay.h
    src/recording/global_shortcuts.cpp
    src/recording/global_shortcuts.h
    src/recording/recording_preferences.cpp
    src/recording/recording_preferences.h
    src/recording/game_monitor.cpp
    src/recording/game_monitor.h
    src/recording/wayland_screencast.cpp
    src/recording/wayland_screencast.h
)

qt_add_qml_module(medal-linux
    URI MedalLinux
    VERSION 1.0
    QML_FILES qml/Main.qml qml/CapturePopup.qml
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
    Qt6::DBus
    PkgConfig::GSTREAMER
    SQLite3::SQLite3
)

target_compile_options(medal-linux PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

install(TARGETS medal-linux RUNTIME DESTINATION bin)
install(FILES packaging/io.moment.Moment.desktop DESTINATION share/applications)
install(FILES packaging/io.moment.Moment.svg DESTINATION share/icons/hicolor/scalable/apps)

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(main-window-visual-test
        tests/main_window_visual_test.cpp
        src/recording/game_monitor.cpp src/recording/game_monitor.h
        src/library/clip_repository.cpp src/library/clip_repository.h
        src/library/clip_library_model.cpp src/library/clip_library_model.h
        src/recording/screen_recorder.cpp src/recording/screen_recorder.h
        src/recording/instant_replay.cpp src/recording/instant_replay.h
        src/recording/recording_preferences.cpp src/recording/recording_preferences.h
        src/recording/wayland_screencast.cpp src/recording/wayland_screencast.h)
    qt_add_resources(main-window-visual-test main_visual_qml PREFIX "/qt/qml/MedalLinux" FILES qml/Main.qml)
    qt_add_resources(main-window-visual-test moment_visual_icons PREFIX "/icons" FILES packaging/io.moment.Moment.svg)
    target_include_directories(main-window-visual-test PRIVATE src)
    target_link_libraries(main-window-visual-test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia Qt6::Concurrent Qt6::DBus Qt6::Test SQLite3::SQLite3 PkgConfig::GSTREAMER)
    add_test(NAME main-window-visual-test COMMAND main-window-visual-test)
    set_tests_properties(main-window-visual-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

    find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
    qt_add_executable(global-shortcuts-test
        tests/global_shortcuts_test.cpp
        src/recording/global_shortcuts.cpp src/recording/global_shortcuts.h)
    target_include_directories(global-shortcuts-test PRIVATE src)
    target_link_libraries(global-shortcuts-test PRIVATE Qt6::Core Qt6::Gui Qt6::DBus Qt6::Test)
    add_test(NAME global-shortcuts-test COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:global-shortcuts-test>)
    set_tests_properties(global-shortcuts-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(background-capture-test
        tests/background_capture_test.cpp
        src/recording/global_shortcuts.cpp src/recording/global_shortcuts.h
        src/recording/screen_recorder.cpp src/recording/screen_recorder.h
        src/recording/instant_replay.cpp src/recording/instant_replay.h
        src/recording/recording_preferences.cpp src/recording/recording_preferences.h
        src/recording/wayland_screencast.cpp src/recording/wayland_screencast.h
        src/library/clip_repository.cpp src/library/clip_repository.h
        src/library/clip_library_model.cpp src/library/clip_library_model.h)
    target_include_directories(background-capture-test PRIVATE src)
    target_link_libraries(background-capture-test PRIVATE Qt6::Core Qt6::Gui Qt6::DBus Qt6::Test
        Qt6::Multimedia Qt6::Concurrent SQLite3::SQLite3 PkgConfig::GSTREAMER)
    add_test(NAME background-capture-test COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:background-capture-test>)
    set_tests_properties(background-capture-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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
        src/recording/wayland_screencast.cpp
        src/recording/wayland_screencast.h
        src/library/clip_repository.cpp
        src/library/clip_repository.h
        src/library/clip_library_model.cpp
        src/library/clip_library_model.h
    )
    target_link_libraries(screen-recorder-test PRIVATE Qt6::Core Qt6::Gui Qt6::Multimedia Qt6::Concurrent Qt6::DBus Qt6::Test SQLite3::SQLite3 PkgConfig::GSTREAMER)
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

    qt_add_executable(game-monitor-test
        tests/game_monitor_test.cpp
        src/recording/game_monitor.cpp
        src/recording/game_monitor.h
    )
    target_link_libraries(game-monitor-test PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME game-monitor-test COMMAND game-monitor-test)

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
endif()
