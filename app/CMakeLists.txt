# Everything except main(), so the tests can use the same pieces
qt_add_library(editor_ui STATIC
    AppSettings.h
    AudioPlayer.cpp
    AutoCaptions.cpp
    AutoCaptions.h
    CardDelegate.cpp
    CardDelegate.h
    AudioPlayer.h
    ClipInspector.cpp
    ClipInspector.h
    ExportDialog.cpp
    ExportDialog.h
    FilmstripCache.cpp
    HelpWindow.cpp
    Icons.cpp
    Icons.h
    HelpWindow.h
    FilmstripCache.h
    MainWindow.cpp
    MainWindow.h
    MediaBin.cpp
    MediaBin.h
    NumberSlider.cpp
    NumberSlider.h
    PreviewRenderer.cpp
    PreviewRenderer.h
    PreviewWidget.cpp
    PreviewWidget.h
    ProjectFile.cpp
    ProjectFile.h
    ProjectSettings.h
    RenderClip.h
    SettingsDialog.cpp
    SettingsDialog.h
    SubtitleFile.cpp
    SubtitleFile.h
    SubtitlePanel.cpp
    SubtitlePanel.h
    ThreadName.h
    TimelineClip.h
    TimelineWidget.cpp
    TimelineWidget.h
    Theme.cpp
    Theme.h
    TitleRenderer.cpp
    UpdateChecker.cpp
    UpdateChecker.h
    WelcomeScreen.cpp
    WelcomeScreen.h
    LibraryPanel.cpp
    LibraryPanel.h
    ClipPresets.h
    EffectNames.h
    TitleRenderer.h
    WaveformCache.cpp
    WaveformCache.h
)
target_include_directories(editor_ui PUBLIC .)
# The help pages (docs/), packed inside the app so they work offline
file(GLOB help_pages CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/docs/*.md")
qt_add_resources(editor_ui "docs" PREFIX "/docs" BASE "${CMAKE_SOURCE_DIR}/docs" FILES ${help_pages})
target_compile_definitions(editor_ui PRIVATE MIXMEDIA_VERSION="${PROJECT_VERSION}")
target_link_libraries(editor_ui PUBLIC Qt6::Widgets Qt6::Multimedia Qt6::Network ve_engine)
if(UNIX AND NOT APPLE)
    target_link_libraries(editor_ui PUBLIC Qt6::DBus)
endif()

set(packaging "${PROJECT_SOURCE_DIR}/packaging")

qt_add_executable(mixmedia main.cpp)
target_link_libraries(mixmedia PRIVATE editor_ui)

# The icon, built into the app for the window and taskbar
qt_add_resources(mixmedia "icon" PREFIX "/" BASE "${packaging}" FILES "${packaging}/mixmedia.png")

if(WIN32)
    # Windows: icon on the .exe itself, and no console window popping up behind the app
    target_sources(mixmedia PRIVATE "${packaging}/mixmedia.rc")
    set_target_properties(mixmedia PROPERTIES WIN32_EXECUTABLE ON)
    set(bin_dir ".") # everything sits right next to mixmedia.exe
else()
    set(bin_dir "bin")
endif()

# `cmake --install build --prefix <folder>` puts a ready-to-run copy of the app in <folder>
install(TARGETS mixmedia RUNTIME_DEPENDENCY_SET mixmedia_deps RUNTIME DESTINATION "${bin_dir}")

if(WIN32)
    # Windows doesn't have FFmpeg or Qt lying around, so bring every DLL the app needs along with it
    get_filename_component(toolchain_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    install(RUNTIME_DEPENDENCY_SET mixmedia_deps
        DESTINATION "${bin_dir}"
        DIRECTORIES "${toolchain_bin}"
        PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"
        POST_EXCLUDE_REGEXES ".*[Ss]ystem32/.*\\.dll")

    # Qt's own helper adds its plugins (window system, audio, image formats...)
    set(CMAKE_INSTALL_BINDIR "${bin_dir}") # tell it about our flat layout
    qt_generate_deploy_app_script(TARGET mixmedia OUTPUT_SCRIPT qt_deploy NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT "${qt_deploy}")
elseif(UNIX AND NOT APPLE)
    # So it shows up in app menus with its icon
    install(FILES "${packaging}/mixmedia.desktop" DESTINATION share/applications)
    install(FILES "${packaging}/mixmedia.png" DESTINATION share/icons/hicolor/256x256/apps)
endif()
