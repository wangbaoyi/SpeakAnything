cmake_minimum_required(VERSION 3.20)

project(sensevoice_stream LANGUAGES C CXX)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_COMMON OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_APP OFF CACHE BOOL "" FORCE)
set(LLAMA_CURL OFF CACHE BOOL "" FORCE)
set(GGML_NATIVE ON CACHE BOOL "" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/llama.cpp EXCLUDE_FROM_ALL)

# CTranslate2 forces the static CRT when built static; keep every target on the
# project's dynamic CRT so they link into one image.
function(sensevoice_use_dynamic_crt directory)
    get_property(targets DIRECTORY ${directory} PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        get_target_property(type ${target} TYPE)
        if(NOT type STREQUAL "INTERFACE_LIBRARY")
            set_property(TARGET ${target} PROPERTY
                MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        endif()
    endforeach()
    get_property(children DIRECTORY ${directory} PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS children)
        sensevoice_use_dynamic_crt(${child})
    endforeach()
endfunction()

option(SENSEVOICE_WITH_TRANSLATION "Build Opus-MT translation through CTranslate2" ON)

add_library(sensevoice_translation STATIC
    src/opus_mt_translator.cpp
    src/translation_session.cpp
    src/translation_worker.cpp
)
target_include_directories(sensevoice_translation PUBLIC src)

if(SENSEVOICE_WITH_TRANSLATION)
    # Ruy keeps the build free of MKL/oneDNN; see docs/adr/0001-ctranslate2-for-opus-mt.md.
    set(WITH_MKL OFF CACHE BOOL "" FORCE)
    set(WITH_RUY ON CACHE BOOL "" FORCE)
    # MSVC OpenMP (vcomp140.dll, shipped by tools/package_windows.ps1). The
    # OpenMP-free thread pool deadlocks at process exit on Windows.
    set(OPENMP_RUNTIME "COMP" CACHE STRING "" FORCE)
    set(BUILD_CLI OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/CTranslate2 EXCLUDE_FROM_ALL)
    if(MSVC)
        sensevoice_use_dynamic_crt(third_party/CTranslate2)
    endif()

    set(SPM_ENABLE_SHARED OFF CACHE BOOL "" FORCE)
    set(SPM_ENABLE_TCMALLOC OFF CACHE BOOL "" FORCE)
    set(SPM_ABSL_PROVIDER "internal" CACHE STRING "" FORCE)
    set(SPM_PROTOBUF_PROVIDER "internal" CACHE STRING "" FORCE)
    add_subdirectory(third_party/sentencepiece EXCLUDE_FROM_ALL)
    if(WIN32)
        # Without these SentencePiece opens model paths in the ANSI code page,
        # which breaks installs under non-ASCII directories.
        target_compile_definitions(sentencepiece-static PRIVATE UNICODE _UNICODE)
    endif()

    target_compile_definitions(sensevoice_translation PUBLIC SENSEVOICE_WITH_TRANSLATION)
    target_include_directories(sensevoice_translation PRIVATE third_party/sentencepiece/src)
    target_link_libraries(sensevoice_translation PRIVATE ctranslate2 sentencepiece-static)
endif()

if(MSVC)
    target_compile_definitions(sensevoice_translation PRIVATE NOMINMAX)
    target_compile_options(sensevoice_translation PRIVATE /utf-8 /W4 /permissive-)
endif()

add_library(sensevoice_core STATIC
    src/audio_conditioner.cpp
    src/fsmn_vad_engine.cpp
    src/hotword_boost.cpp
    src/sensevoice_engine.cpp
    src/stability_tracker.cpp
    src/stream_recognizer.cpp
    src/system_audio_mute.cpp
    src/text_processor.cpp
)
target_include_directories(sensevoice_core PUBLIC src)
target_link_libraries(sensevoice_core PUBLIC ggml)

if(MSVC)
    target_compile_definitions(sensevoice_core PRIVATE NOMINMAX _USE_MATH_DEFINES)
    target_compile_options(sensevoice_core PRIVATE /utf-8 /W4 /permissive-)
endif()

add_executable(sensevoice-stream
    src/audio_io.cpp
    src/main.cpp
)
target_include_directories(sensevoice-stream PRIVATE
    src
    third_party/FunASR/runtime/llama.cpp/funasr-common
)
target_link_libraries(sensevoice-stream PRIVATE sensevoice_core sensevoice_translation)

if(WIN32)
    target_compile_definitions(sensevoice-stream PRIVATE NOMINMAX _USE_MATH_DEFINES)
    target_compile_options(sensevoice-stream PRIVATE /utf-8 /W4 /permissive-)
    target_link_libraries(sensevoice-stream PRIVATE
        ole32
        winmm
        mfplat
        mfreadwrite
        mfuuid
    )
endif()

if(WIN32)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)
    set(CMAKE_AUTORCC ON)

    set(OPENLESS_IME_SOURCE_DIR
        "${CMAKE_SOURCE_DIR}/third_party/openless/openless-all/app/windows-ime/src")
    add_library(OpenLessIme SHARED
        ${OPENLESS_IME_SOURCE_DIR}/class_factory.cpp
        ${OPENLESS_IME_SOURCE_DIR}/dllmain.cpp
        ${OPENLESS_IME_SOURCE_DIR}/edit_session.cpp
        ${OPENLESS_IME_SOURCE_DIR}/ipc_client.cpp
        ${OPENLESS_IME_SOURCE_DIR}/registry.cpp
        ${OPENLESS_IME_SOURCE_DIR}/resource.rc
        ${OPENLESS_IME_SOURCE_DIR}/text_service.cpp
        ${OPENLESS_IME_SOURCE_DIR}/OpenLessIme.def
    )
    target_include_directories(OpenLessIme PRIVATE ${OPENLESS_IME_SOURCE_DIR})
    target_compile_definitions(OpenLessIme PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_compile_options(OpenLessIme PRIVATE /utf-8 /W4 /permissive-)
    target_link_libraries(OpenLessIme PRIVATE advapi32 ole32 uuid)
    set_property(TARGET OpenLessIme PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    add_executable(sensevoice-ui WIN32
        src/audio_io.cpp
        src/qt_ui_main.cpp
        src/speech_player.cpp
        src/speech_queue.cpp
        src/windows_text_injector.cpp
        resources/sensevoice.qrc
        resources/sensevoice.rc
    )
    target_include_directories(sensevoice-ui PRIVATE
        src
        third_party/FunASR/runtime/llama.cpp/funasr-common
    )
    target_compile_definitions(sensevoice-ui PRIVATE NOMINMAX _USE_MATH_DEFINES UNICODE _UNICODE)
    target_compile_options(sensevoice-ui PRIVATE /utf-8 /W4 /permissive-)
    target_link_libraries(sensevoice-ui PRIVATE
        sensevoice_core
        sensevoice_translation
        Qt6::Widgets
        psapi
        ole32
        oleaut32
        uiautomationcore
        uuid
        winmm
        mfplat
        mfreadwrite
        mfuuid
    )

    # Kokoro speech synthesis lives in its own process; see
    # docs/adr/0002-kokoro-tts-in-separate-process.md. sherpa-onnx is the
    # prebuilt win-x64 shared MD release vendored under third_party/sherpa-onnx.
    set(SHERPA_ONNX_DIR "${CMAKE_SOURCE_DIR}/third_party/sherpa-onnx")
    add_executable(sensevoice-tts src/tts_helper_main.cpp)
    target_include_directories(sensevoice-tts PRIVATE "${SHERPA_ONNX_DIR}/include")
    target_compile_definitions(sensevoice-tts PRIVATE NOMINMAX UNICODE _UNICODE)
    target_compile_options(sensevoice-tts PRIVATE /utf-8 /W4 /permissive-)
    target_link_libraries(sensevoice-tts PRIVATE "${SHERPA_ONNX_DIR}/lib/sherpa-onnx-c-api.lib")
    add_custom_command(TARGET sensevoice-tts POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHERPA_ONNX_DIR}/lib/sherpa-onnx-c-api.dll"
            "${SHERPA_ONNX_DIR}/lib/onnxruntime.dll"
            "${SHERPA_ONNX_DIR}/lib/onnxruntime_providers_shared.dll"
            "$<TARGET_FILE_DIR:sensevoice-tts>")
    add_dependencies(sensevoice-ui sensevoice-tts)

    add_executable(speech-queue-check
        tests/speech_queue_check.cpp
        src/speech_player.cpp
        src/speech_queue.cpp
    )
    target_include_directories(speech-queue-check PRIVATE src)
    target_compile_definitions(speech-queue-check PRIVATE NOMINMAX UNICODE _UNICODE)
    target_compile_options(speech-queue-check PRIVATE /utf-8 /W4 /permissive-)
    target_link_libraries(speech-queue-check PRIVATE Qt6::Core ole32 propsys)
    add_dependencies(speech-queue-check sensevoice-tts)

    add_executable(windows-text-injector-test
        tests/windows_text_injector_test.cpp
        src/windows_text_injector.cpp
    )
    target_include_directories(windows-text-injector-test PRIVATE src)
    target_compile_definitions(windows-text-injector-test PRIVATE NOMINMAX UNICODE _UNICODE)
    target_compile_options(windows-text-injector-test PRIVATE /utf-8 /W4 /permissive-)
    target_link_libraries(windows-text-injector-test PRIVATE
        ole32
        oleaut32
        uiautomationcore
        uuid
    )
    add_test(NAME windows-text-injector-test COMMAND windows-text-injector-test)

    add_executable(windows-text-target WIN32
        tests/windows_text_target.cpp
    )
    target_compile_definitions(windows-text-target PRIVATE NOMINMAX UNICODE _UNICODE)
    target_compile_options(windows-text-target PRIVATE /utf-8 /W4 /permissive-)
    target_link_libraries(windows-text-target PRIVATE Qt6::Widgets)
    add_dependencies(windows-text-injector-test windows-text-target)

endif()

enable_testing()
add_executable(stability-tracker-test
    tests/stability_tracker_test.cpp
    src/stability_tracker.cpp
)
target_include_directories(stability-tracker-test PRIVATE src)
if(MSVC)
    target_compile_options(stability-tracker-test PRIVATE /utf-8 /W4 /permissive-)
endif()
add_test(NAME stability-tracker-test COMMAND stability-tracker-test)

add_executable(text-processor-test
    tests/text_processor_test.cpp
    src/text_processor.cpp
)
target_include_directories(text-processor-test PRIVATE src)
target_compile_definitions(text-processor-test PRIVATE
    SENSEVOICE_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
if(MSVC)
    target_compile_options(text-processor-test PRIVATE /utf-8 /W4 /permissive-)
endif()
add_test(NAME text-processor-test COMMAND text-processor-test)

add_executable(audio-conditioner-test
    tests/audio_conditioner_test.cpp
    src/audio_conditioner.cpp
)
target_include_directories(audio-conditioner-test PRIVATE src)
if(MSVC)
    target_compile_options(audio-conditioner-test PRIVATE /utf-8 /W4 /permissive-)
endif()
add_test(NAME audio-conditioner-test COMMAND audio-conditioner-test)

add_executable(hotword-boost-test
    tests/hotword_boost_test.cpp
    src/hotword_boost.cpp
)
target_include_directories(hotword-boost-test PRIVATE src)
if(MSVC)
    target_compile_options(hotword-boost-test PRIVATE /utf-8 /W4 /permissive-)
endif()
add_test(NAME hotword-boost-test COMMAND hotword-boost-test)

add_executable(translator-test
    tests/translator_test.cpp
)
target_link_libraries(translator-test PRIVATE sensevoice_translation)
target_compile_definitions(translator-test PRIVATE
    SENSEVOICE_TEST_MODELS_DIR="${CMAKE_SOURCE_DIR}/models"
)
if(MSVC)
    target_compile_options(translator-test PRIVATE /utf-8 /W4 /permissive-)
endif()
add_test(NAME translator-test COMMAND translator-test)
