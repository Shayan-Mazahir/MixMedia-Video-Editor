find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
    libswscale
    libswresample
)

add_library(ve_engine STATIC
    src/audio_fx.cpp
    src/audio_reader.cpp
    src/engine.cpp
    src/exporter.cpp
    src/export_other.cpp
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

# Auto-captions: whisper.cpp (speech to text, plain C/C++), downloaded and built along with the engine.
# Don't want it? cmake -DMIXMEDIA_CAPTIONS=OFF
option(MIXMEDIA_CAPTIONS "Build auto-captions (whisper.cpp)" ON)
if(MIXMEDIA_CAPTIONS)
    include(FetchContent)
    set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(GGML_OPENMP OFF CACHE BOOL "" FORCE) # its own threads, one less library to ship
    # Not tuned to this exact computer (or it'd crash on others), but still using the fast
    # instructions every PC from the last ten-ish years has
    set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
        foreach(flag GGML_AVX GGML_AVX2 GGML_FMA GGML_F16C)
            set(${flag} ON CACHE BOOL "" FORCE)
        endforeach()
    endif()
    FetchContent_Declare(whisper
        URL https://github.com/ggml-org/whisper.cpp/archive/refs/tags/v1.9.4.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(whisper)
    # (their code, their warnings: keep our build output readable)
    foreach(target whisper ggml ggml-base ggml-cpu)
        if(TARGET ${target})
            set_target_properties(${target} PROPERTIES COMPILE_OPTIONS "-w")
        endif()
    endforeach()
    target_link_libraries(ve_engine PRIVATE whisper)
    target_compile_definitions(ve_engine PRIVATE VE_HAVE_WHISPER=1)
    target_sources(ve_engine PRIVATE src/captions.cpp)
endif()
