find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
    libswscale
    libswresample
)

add_library(ve_engine STATIC
    src/audio_reader.cpp
    src/engine.cpp
    src/exporter.cpp
    src/effects.cpp
    src/ffmpeg_util.cpp
    src/stream_copy.cpp
    src/timeline.cpp
    src/video_encoder.cpp
    src/video_reader.cpp
)

target_include_directories(ve_engine PUBLIC include)
target_link_libraries(ve_engine PRIVATE PkgConfig::FFMPEG)
target_compile_definitions(ve_engine PRIVATE VE_VERSION="${PROJECT_VERSION}")
