# Everything except main(), so the tests can use the same pieces
qt_add_library(editor_ui STATIC
    AudioPlayer.cpp
    AudioPlayer.h
    ClipInspector.cpp
    ClipInspector.h
    ExportDialog.cpp
    ExportDialog.h
    FilmstripCache.cpp
    FilmstripCache.h
    MainWindow.cpp
    MainWindow.h
    MediaBin.cpp
    MediaBin.h
    PreviewRenderer.cpp
    PreviewRenderer.h
    PreviewWidget.cpp
    PreviewWidget.h
    ProjectFile.cpp
    ProjectFile.h
    RenderClip.h
    TimelineClip.h
    TimelineWidget.cpp
    TimelineWidget.h
    TitleRenderer.cpp
    TitleRenderer.h
    WaveformCache.cpp
    WaveformCache.h
)
target_include_directories(editor_ui PUBLIC .)
target_link_libraries(editor_ui PUBLIC Qt6::Widgets Qt6::Multimedia ve_engine)
if(UNIX AND NOT APPLE)
    target_link_libraries(editor_ui PUBLIC Qt6::DBus)
endif()

qt_add_executable(mixmedia main.cpp)
target_link_libraries(mixmedia PRIVATE editor_ui)

# No console window popping up behind the app on Windows
set_target_properties(mixmedia PROPERTIES WIN32_EXECUTABLE ON)

# `cmake --install build --prefix <folder>` puts a ready-to-run copy of the app in <folder>
install(TARGETS mixmedia RUNTIME_DEPENDENCY_SET mixmedia_deps RUNTIME DESTINATION bin)

if(WIN32)
    # Windows doesn't have FFmpeg or Qt lying around, so bring every DLL the app needs along with it
    get_filename_component(toolchain_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    install(RUNTIME_DEPENDENCY_SET mixmedia_deps
        DESTINATION bin
        DIRECTORIES "${toolchain_bin}"
        PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"
        POST_EXCLUDE_REGEXES ".*[Ss]ystem32/.*\\.dll")

    # Qt's own helper adds its plugins (window system, audio, image formats...)
    qt_generate_deploy_app_script(TARGET mixmedia OUTPUT_SCRIPT qt_deploy NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT "${qt_deploy}")
endif()
