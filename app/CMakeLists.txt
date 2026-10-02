# Everything except main(), so the tests can use the same pieces
qt_add_library(editor_ui STATIC
    AudioPlayer.cpp
    AudioPlayer.h
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
    RenderClip.h
    TimelineClip.h
    TimelineWidget.cpp
    TimelineWidget.h
)
target_include_directories(editor_ui PUBLIC .)
target_link_libraries(editor_ui PUBLIC Qt6::Widgets Qt6::Multimedia ve_engine)
if(UNIX AND NOT APPLE)
    target_link_libraries(editor_ui PUBLIC Qt6::DBus)
endif()
target_compile_options(editor_ui PRIVATE -Wall -Wextra)

qt_add_executable(mixmedia main.cpp)
target_link_libraries(mixmedia PRIVATE editor_ui)
target_compile_options(mixmedia PRIVATE -Wall -Wextra)
