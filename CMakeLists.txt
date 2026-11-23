cmake_minimum_required(VERSION 3.20)

project(sensevoice_stream LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

# CMake 4 rejects the pre-3.5 minimums some vendored projects still declare.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "")

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

# Windows CUDA is opt-in at configure time but automatic when a toolkit is
# installed: a machine without one still gets the CPU-only build, and the app
# reports the missing GPU instead of failing to configure. Pass
# -DSENSEVOICE_WITH_CUDA=OFF for a deterministic CPU build, or
# -DSENSEVOICE_CUDA_REQUIRED=ON to make a missing toolkit a configure error.
if(WIN32)
    option(SENSEVOICE_WITH_CUDA
        "Build the ggml CUDA backend when a CUDA toolkit is available" ON)
    option(SENSEVOICE_CUDA_REQUIRED
        "Fail the configure when SENSEVOICE_WITH_CUDA is ON but no toolkit exists" OFF)
    if(SENSEVOICE_WITH_CUDA)
        find_package(CUDAToolkit QUIET)
        if(CUDAToolkit_FOUND)
            set(GGML_CUDA ON CACHE BOOL "" FORCE)
            message(STATUS "CUDA ${CUDAToolkit_VERSION} found; building the ggml CUDA backend")
            if(CUDAToolkit_VERSION VERSION_GREATER_EQUAL "13")
                # CUDA 13 dropped Maxwell, Pascal and Volta device code. The
                # backend still builds, but sm_61 needs a 12.x toolkit.
                message(WARNING
                    "CUDA ${CUDAToolkit_VERSION} no longer generates Pascal (sm_61) code; "
                    "use CUDA 12.6.x for a GeForce GTX 10 series GPU")
            endif()
        elseif(SENSEVOICE_CUDA_REQUIRED)
            message(FATAL_ERROR
                "SENSEVOICE_WITH_CUDA is ON and SENSEVOICE_CUDA_REQUIRED is ON, "
                "but no CUDA toolkit was found")
        else()
            message(STATUS "CUDA toolkit not found; building CPU-only")
        endif()
    else()
        message(STATUS "SENSEVOICE_WITH_CUDA=OFF; building CPU-only")
    endif()
endif()

# ggml's CUDA language targets default to the static CUDA runtime, while
# CTranslate2's FindCUDA links cudart.lib (the import library for the DLL).
# Both in one image means duplicate cuda* symbols (LNK2005), so keep the whole
# app on the shared runtime, which is also the DLL that release packaging ships.
if(GGML_CUDA)
    set(CMAKE_CUDA_RUNTIME_LIBRARY Shared CACHE STRING "" FORCE)
endif()

add_subdirectory(third_party/llama.cpp EXCLUDE_FROM_ALL)

# llama.cpp still writes u8"..." literals, which MSVC treats as char8_t under
# C++20. Pin the library to C++17 (its own declared minimum) so the Qwen
# translation path builds on Windows alongside this project's C++20 code.
if(TARGET llama)
    set_target_properties(llama PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
endif()

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

# Both translation backends can be compiled into one binary; ComputePolicy
# chooses at runtime (CPU -> Opus-MT, GPU -> Qwen GGUF, Auto -> best available).
# The old single-backend cache entry stays declared so existing build trees do
# not warn about an unused variable, but it no longer selects anything.
set(SENSEVOICE_TRANSLATION_BACKEND "" CACHE STRING
    "Deprecated: the translation backend is chosen at runtime by ComputePolicy")
# Keep the old platform default for Opus-MT: macOS shipped LLM-only and should
# not start building CTranslate2 unless asked. The LLM path is now also built
# on Windows, where the CUDA build must be able to run Qwen.
if(APPLE)
    set(SENSEVOICE_WITH_TRANSLATION_DEFAULT OFF)
else()
    set(SENSEVOICE_WITH_TRANSLATION_DEFAULT ON)
endif()
option(SENSEVOICE_WITH_TRANSLATION "Build Opus-MT translation through CTranslate2"
    ${SENSEVOICE_WITH_TRANSLATION_DEFAULT})
option(SENSEVOICE_WITH_LLM_TRANSLATION "Build Qwen GGUF translation through llama.cpp" ON)

add_library(sensevoice_translation STATIC
    src/languages.cpp
    src/opus_mt_translator.cpp
    src/translation_backend.cpp
    src/translation_session.cpp
    src/translation_worker.cpp
)
target_include_directories(sensevoice_translation PUBLIC src)
if(SENSEVOICE_WITH_LLM_TRANSLATION)
    target_sources(sensevoice_translation PRIVATE src/llm_translator.cpp)
    target_compile_definitions(sensevoice_translation PUBLIC SENSEVOICE_WITH_LLM_TRANSLATION)
    target_link_libraries(sensevoice_translation PRIVATE llama)
endif()
if(GGML_CUDA)
    # "this build has no CUDA" versus "CUDA is compiled in but no device".
    target_compile_definitions(sensevoice_translation PRIVATE SENSEVOICE_WITH_CUDA)
endif()

# Shared by Opus-MT and the Core ML SenseVoice tokenizer.
macro(sensevoice_add_sentencepiece)
    if(NOT TARGET sentencepiece-static)
        set(SPM_ENABLE_SHARED OFF CACHE BOOL "" FORCE)
        set(SPM_ENABLE_TCMALLOC OFF CACHE BOOL "" FORCE)
        set(SPM_ABSL_PROVIDER "internal" CACHE STRING "" FORCE)
        set(SPM_PROTOBUF_PROVIDER "internal" CACHE STRING "" FORCE)
        add_subdirectory(third_party/sentencepiece EXCLUDE_FROM_ALL)
    endif()
endmacro()

if(SENSEVOICE_WITH_TRANSLATION)
    # CTranslate2 has its own CUDA backend, independent of ggml: Opus-MT can
    # run on the GPU while speech recognition stays on the CPU. The default is
    # ON only when a toolkit is installed, so a CPU-only machine configures
    # exactly as before. `SENSEVOICE_CT2_CUDA_AVAILABLE` records whether the
    # CUDA translation backend is actually compiled in (packaging reads it).
    set(_sensevoice_ct2_cuda_default OFF)
    if(NOT APPLE)
        find_package(CUDAToolkit QUIET)
        if(CUDAToolkit_FOUND)
            set(_sensevoice_ct2_cuda_default ON)
        endif()
    endif()
    option(SENSEVOICE_CT2_CUDA
        "Build CTranslate2 with CUDA support for Opus-MT" ${_sensevoice_ct2_cuda_default})
    set(SENSEVOICE_CT2_CUDA_AVAILABLE OFF CACHE INTERNAL
        "CTranslate2 was configured with WITH_CUDA=ON" FORCE)

    if(SENSEVOICE_CT2_CUDA)
        if(APPLE OR NOT CUDAToolkit_FOUND)
            message(STATUS
                "SENSEVOICE_CT2_CUDA is ON but no CUDA toolkit was found; Opus-MT stays CPU-only")
        else()
            if(CUDAToolkit_BIN_DIR)
                get_filename_component(_sensevoice_cuda_root
                    "${CUDAToolkit_BIN_DIR}/.." ABSOLUTE)
            else()
                set(_sensevoice_cuda_root "${CUDAToolkit_ROOT}")
            endif()
            # FindCUDA (used by CTranslate2 4.8) reads CUDA_TOOLKIT_ROOT_DIR,
            # and dynamic loading lets one binary start on a machine without a
            # GPU. CMake 3.27+ hides FindCUDA unless CMP0146 is OLD; the
            # subdirectory's cmake_minimum_required(3.7) leaves it unset, but
            # state it explicitly so a future policy default cannot break it.
            set(CMAKE_POLICY_DEFAULT_CMP0146 OLD)
            # CTranslate2 compiles its .cu files through FindCUDA, which
            # supplies the gencode flags; CMP0104 NEW would also demand
            # CMAKE_CUDA_ARCHITECTURES for the target.
            set(CMAKE_POLICY_DEFAULT_CMP0104 OLD)
            set(CUDA_TOOLKIT_ROOT_DIR "${_sensevoice_cuda_root}" CACHE PATH "" FORCE)
            set(WITH_CUDA ON CACHE BOOL "" FORCE)
            set(WITH_CUDNN OFF CACHE BOOL "" FORCE)
            set(CUDA_ARCH_LIST "6.1" CACHE STRING "" FORCE)
            set(CUDA_DYNAMIC_LOADING ON CACHE BOOL "" FORCE)
            # FindCUDA defaults to the static CUDA runtime. ggml links the
            # shared cudart, and both in one image duplicate every cuda* symbol
            # (LNK2005 on MSVC). The shared runtime is also the DLL packaged.
            set(CUDA_USE_STATIC_CUDA_RUNTIME OFF CACHE BOOL "" FORCE)
            set(SENSEVOICE_CT2_CUDA_AVAILABLE ON CACHE INTERNAL
                "CTranslate2 was configured with WITH_CUDA=ON" FORCE)
            message(STATUS
                "CTranslate2 CUDA enabled (CUDA ${CUDAToolkit_VERSION}, sm_61, dynamic loading)")
        endif()
    endif()

    # Ruy keeps the build free of MKL/oneDNN; see docs/adr/0001-ctranslate2-for-opus-mt.md.
    set(WITH_MKL OFF CACHE BOOL "" FORCE)
    if(APPLE)
        # Accelerate (AMX on Apple Silicon) beats Ruy for int8/fp32 GEMM on macOS.
        set(WITH_ACCELERATE ON CACHE BOOL "" FORCE)
        set(WITH_RUY OFF CACHE BOOL "" FORCE)
        set(OPENMP_RUNTIME "NONE" CACHE STRING "" FORCE)
    else()
        set(WITH_RUY ON CACHE BOOL "" FORCE)
        # MSVC OpenMP (vcomp140.dll, shipped by tools/package_windows.ps1). The
        # OpenMP-free thread pool deadlocks at process exit on Windows.
        set(OPENMP_RUNTIME "COMP" CACHE STRING "" FORCE)
    endif()
    set(BUILD_CLI OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    # CTranslate2 hard-codes /MT for static MSVC builds, including its CUDA
    # host code, while this app and Qt use /MD. Only the CUDA configuration
    # hits that path, so build CTranslate2 shared just for it: the shared
    # configuration uses /MD and links cleanly. CPU-only builds stay static.
    set(SENSEVOICE_CT2_SHARED OFF)
    if(MSVC AND SENSEVOICE_CT2_CUDA_AVAILABLE)
        set(BUILD_SHARED_LIBS ON)
        set(SENSEVOICE_CT2_SHARED ON)
    endif()
    add_subdirectory(third_party/CTranslate2 EXCLUDE_FROM_ALL)
    if(SENSEVOICE_CT2_SHARED)
        set(BUILD_SHARED_LIBS OFF)
    endif()
    if(MSVC)
        sensevoice_use_dynamic_crt(third_party/CTranslate2)
    endif()
    if(SENSEVOICE_CT2_CUDA_AVAILABLE)
        # PUBLIC so translation tests can exercise the CUDA selection matrix
        # even on a machine where the backend would not run.
        target_compile_definitions(sensevoice_translation PUBLIC SENSEVOICE_CT2_CUDA)
    endif()

    sensevoice_add_sentencepiece()
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

# whisper.cpp's sources built against llama.cpp's ggml, so one ggml serves
# SenseVoice, Whisper and the translation LLM.
set(WHISPER_DIR "${CMAKE_SOURCE_DIR}/third_party/whisper.cpp")
add_library(whisper STATIC
    ${WHISPER_DIR}/src/whisper.cpp
    ${WHISPER_DIR}/src/aneforge/whisper-aneforge.cpp
)
target_include_directories(whisper PUBLIC ${WHISPER_DIR}/include PRIVATE ${WHISPER_DIR}/src)
target_compile_definitions(whisper PRIVATE WHISPER_VERSION="vendored" _USE_MATH_DEFINES)
target_link_libraries(whisper PUBLIC ggml)
if(MSVC)
    target_compile_options(whisper PRIVATE /utf-8 /W0)
else()
    target_compile_options(whisper PRIVATE -w)
endif()
if(APPLE)
    # Encoder on the Neural Engine when <model>-encoder.mlmodelc is present,
    # otherwise the Metal path.
    target_sources(whisper PRIVATE
        ${WHISPER_DIR}/src/coreml/whisper-compat.m
        ${WHISPER_DIR}/src/coreml/whisper-encoder.mm
        ${WHISPER_DIR}/src/coreml/whisper-encoder-impl.m)
    target_compile_definitions(whisper PRIVATE WHISPER_USE_COREML WHISPER_COREML_ALLOW_FALLBACK)
    set_source_files_properties(
        ${WHISPER_DIR}/src/coreml/whisper-compat.m
        ${WHISPER_DIR}/src/coreml/whisper-encoder-impl.m
        ${WHISPER_DIR}/src/coreml/whisper-encoder.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(whisper PRIVATE "-framework CoreML" "-framework Foundation")
endif()

add_library(sensevoice_core STATIC
    src/audio_conditioner.cpp
    src/compute_policy.cpp
    src/fbank.cpp
    src/fsmn_vad_engine.cpp
    src/hotword_boost.cpp
    src/sensevoice_engine.cpp
    src/stability_tracker.cpp
    src/stream_recognizer.cpp
    src/system_audio_mute.cpp
    src/text_processor.cpp
    src/whisper_engine.cpp
)
target_include_directories(sensevoice_core PUBLIC src)
target_link_libraries(sensevoice_core PUBLIC ggml whisper)
if(GGML_CUDA)
    # Lets the compute policy tell "no CUDA in this build" apart from
    # "CUDA is compiled in but no device initialized".
    target_compile_definitions(sensevoice_core PUBLIC SENSEVOICE_WITH_CUDA)
endif()
if(APPLE)
    target_link_libraries(sensevoice_core PUBLIC "-framework CoreAudio")

    # SenseVoice on the Neural Engine through Core ML (macOS 15+); the GGUF
    # CPU engine stays available for a .gguf model path.
    option(SENSEVOICE_WITH_COREML "Run SenseVoice through Core ML on the Neural Engine" ON)
    if(SENSEVOICE_WITH_COREML)
        sensevoice_add_sentencepiece()
        target_sources(sensevoice_core PRIVATE src/macos/coreml_sensevoice.mm)
        target_compile_definitions(sensevoice_core PUBLIC SENSEVOICE_WITH_COREML)
        target_compile_options(sensevoice_core PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
        target_include_directories(sensevoice_core PRIVATE third_party/sentencepiece/src)
        target_link_libraries(sensevoice_core PRIVATE sentencepiece-static
            "-framework CoreML" "-framework Foundation")
    endif()
endif()

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
    find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Network)
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

    set(SENSEVOICE_UI_SOURCES
        src/audio_io.cpp
        src/model_catalog.cpp
        src/model_manager.cpp
        src/models_page.cpp
        src/speech_player.cpp
        src/speech_queue.cpp
        src/ui/design_tokens.cpp
        src/ui/overlay_theme.cpp
        src/ui/settings_types.cpp
        src/windows_text_injector.cpp
    )
    add_executable(sensevoice-ui WIN32
        ${SENSEVOICE_UI_SOURCES}
        src/qt_ui_main.cpp
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
        Qt6::Network
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
    # The manifest resource declares UTF-8 as the active code page so vendored
    # C libraries can open model paths that contain non-ASCII characters.
    target_link_options(sensevoice-ui PRIVATE /MANIFEST:NO)

    # Verification harness for the settings dialog. It compiles the UI without
    # its entry point and links the test-only hooks in qt_ui_main.cpp.
    add_executable(settings-ui-test
        tests/settings_ui_test.cpp
        ${SENSEVOICE_UI_SOURCES}
        src/qt_ui_main.cpp
    )
    target_include_directories(settings-ui-test PRIVATE
        src
        third_party/FunASR/runtime/llama.cpp/funasr-common
    )
    target_compile_definitions(settings-ui-test PRIVATE
        SENSEVOICE_UI_TESTING NOMINMAX _USE_MATH_DEFINES UNICODE _UNICODE)
    target_compile_options(settings-ui-test PRIVATE /utf-8 /W4 /permissive-)
    target_link_libraries(settings-ui-test PRIVATE
        sensevoice_core
        sensevoice_translation
        Qt6::Widgets
        Qt6::Network
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
    add_test(NAME settings-ui-test COMMAND settings-ui-test)
    set_tests_properties(settings-ui-test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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

elseif(APPLE)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Network)
    set(CMAKE_AUTORCC ON)

    set(SENSEVOICE_MACOS_FRAMEWORKS
        "-framework AppKit"
        "-framework ApplicationServices"
        "-framework AVFoundation"
        "-framework AudioToolbox"
        "-framework Carbon"
        "-framework CoreAudio"
        "-framework CoreFoundation"
        "-framework ServiceManagement"
    )

    add_executable(sensevoice-ui MACOSX_BUNDLE
        src/audio_io.cpp
        src/model_catalog.cpp
        src/model_manager.cpp
        src/models_page.cpp
        src/qt_ui_main.cpp
        src/speech_player_miniaudio.cpp
        src/speech_queue.cpp
        src/ui/design_tokens.cpp
        src/ui/overlay_theme.cpp
        src/ui/settings_types.cpp
        src/macos/macos_hotkey.mm
        src/macos/macos_login_item.mm
        src/macos/macos_overlay.mm
        src/macos/macos_system_voice.mm
        src/macos/macos_text_injector.mm
        resources/sensevoice.qrc
    )
    target_include_directories(sensevoice-ui PRIVATE
        src
        third_party/FunASR/runtime/llama.cpp/funasr-common
    )
    set_target_properties(sensevoice-ui PROPERTIES
        OUTPUT_NAME SpeakAnything
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER org.speakanything.app
        MACOSX_BUNDLE_BUNDLE_NAME SpeakAnything
    )
    target_compile_options(sensevoice-ui PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_link_libraries(sensevoice-ui PRIVATE
        sensevoice_core
        sensevoice_translation
        Qt6::Widgets
        Qt6::Network
        ${SENSEVOICE_MACOS_FRAMEWORKS}
    )

    # Kokoro speech synthesis in its own process (docs/adr/0002). Point
    # SHERPA_ONNX_MACOS_DIR at an unpacked sherpa-onnx osx-universal2 shared
    # release (include/ + lib/); without it the UI builds and speech is off.
    set(SHERPA_ONNX_MACOS_DIR "${CMAKE_SOURCE_DIR}/third_party/sherpa-onnx-macos"
        CACHE PATH "sherpa-onnx macOS shared release")
    if(EXISTS "${SHERPA_ONNX_MACOS_DIR}/lib/libsherpa-onnx-c-api.dylib")
        add_executable(sensevoice-tts src/tts_helper_main.cpp)
        target_include_directories(sensevoice-tts PRIVATE "${SHERPA_ONNX_MACOS_DIR}/include")
        target_link_libraries(sensevoice-tts PRIVATE
            "${SHERPA_ONNX_MACOS_DIR}/lib/libsherpa-onnx-c-api.dylib")
        set_target_properties(sensevoice-tts PROPERTIES
            BUILD_RPATH "@executable_path;${SHERPA_ONNX_MACOS_DIR}/lib"
            INSTALL_RPATH "@executable_path")
        # The helper lives next to the UI binary inside the bundle.
        add_custom_command(TARGET sensevoice-tts POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:sensevoice-tts>"
                "$<TARGET_FILE_DIR:sensevoice-ui>/sensevoice-tts")
        add_dependencies(sensevoice-ui sensevoice-tts)
    else()
        message(STATUS "sherpa-onnx for macOS not found; Kokoro speech helper skipped")
    endif()

    add_executable(speech-queue-check
        tests/speech_queue_check.cpp
        src/audio_io.cpp
        src/speech_player_miniaudio.cpp
        src/speech_queue.cpp
        src/macos/macos_system_voice.mm
    )
    target_compile_options(speech-queue-check PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_include_directories(speech-queue-check PRIVATE
        src
        third_party/FunASR/runtime/llama.cpp/funasr-common
    )
    target_link_libraries(speech-queue-check PRIVATE
        sensevoice_core Qt6::Core ${SENSEVOICE_MACOS_FRAMEWORKS})

    add_executable(model-download-check
        tests/model_download_check.cpp
        src/model_catalog.cpp
        src/model_manager.cpp
    )
    target_include_directories(model-download-check PRIVATE src)
    target_link_libraries(model-download-check PRIVATE Qt6::Core Qt6::Network)

    add_subdirectory(packaging/macos/virtual-mic)
endif()

enable_testing()

if(TARGET sensevoice-ui)
    # English UI. Chinese is the source language, so only the English
    # catalogue is compiled; refresh it with the update_translations target.
    find_package(Qt6 6.8 QUIET COMPONENTS LinguistTools)
    if(Qt6LinguistTools_FOUND)
        qt_add_translations(sensevoice-ui
            TS_FILES resources/i18n/speakanything_en.ts
            SOURCE_TARGETS sensevoice-ui
            RESOURCE_PREFIX /i18n
            LUPDATE_OPTIONS -locations none -source-language zh_CN
        )
        add_test(NAME translations-check
            COMMAND ${CMAKE_COMMAND}
                -DTS_FILE=${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/speakanything_en.ts
                -DQM_FILE=${CMAKE_CURRENT_BINARY_DIR}/speakanything_en.qm
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_translations.cmake)
        set_tests_properties(translations-check PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    else()
        message(WARNING "Qt LinguistTools not found; the UI will only be available in Chinese")
    endif()
endif()

find_package(Threads REQUIRED)
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

add_executable(compute-policy-test
    tests/compute_policy_test.cpp
    src/compute_policy.cpp
)
target_include_directories(compute-policy-test PRIVATE src)
target_link_libraries(compute-policy-test PRIVATE ggml)
if(GGML_CUDA)
    target_compile_definitions(compute-policy-test PRIVATE SENSEVOICE_WITH_CUDA)
endif()
if(MSVC)
    target_compile_options(compute-policy-test PRIVATE /utf-8 /W4 /permissive-)
endif()
add_test(NAME compute-policy-test COMMAND compute-policy-test)

# Shared log-mel front end. The unit test proves the parallel frame split is
# byte-identical to the serial path; the benchmark prints 1-thread versus
# 4-thread timings for a deterministic 5 s signal.
add_executable(fbank-test
    tests/fbank_test.cpp
    src/fbank.cpp
)
target_include_directories(fbank-test PRIVATE src)
target_link_libraries(fbank-test PRIVATE Threads::Threads)
if(MSVC)
    target_compile_options(fbank-test PRIVATE /utf-8 /W4 /permissive-)
endif()
add_test(NAME fbank-test COMMAND fbank-test)

add_executable(fbank-benchmark
    tests/fbank_benchmark.cpp
    src/fbank.cpp
)
target_include_directories(fbank-benchmark PRIVATE src)
target_link_libraries(fbank-benchmark PRIVATE Threads::Threads)
if(MSVC)
    target_compile_options(fbank-benchmark PRIVATE /utf-8 /W4 /permissive-)
endif()

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
if(EXISTS "${CMAKE_SOURCE_DIR}/modelspt/opus-mt-zh-en-ct2/model.bin" OR
   EXISTS "${CMAKE_SOURCE_DIR}/modelspt/translator/opus-mt-zh-en-ct2/model.bin")
    set(SENSEVOICE_TEST_MODELS_DIR "${CMAKE_SOURCE_DIR}/modelspt")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/models/opus-mt-zh-en-ct2/model.bin" OR
       EXISTS "${CMAKE_SOURCE_DIR}/models/translator/opus-mt-zh-en-ct2/model.bin")
    set(SENSEVOICE_TEST_MODELS_DIR "${CMAKE_SOURCE_DIR}/models")
else()
    set(SENSEVOICE_TEST_MODELS_DIR "${CMAKE_SOURCE_DIR}/modelspt")
endif()
target_compile_definitions(translator-test PRIVATE
    SENSEVOICE_TEST_MODELS_DIR="${SENSEVOICE_TEST_MODELS_DIR}"
)
if(MSVC)
    target_compile_options(translator-test PRIVATE /utf-8 /W4 /permissive-)
endif()
add_test(NAME translator-test COMMAND translator-test)

if(TARGET Qt6::Core)
    add_executable(model-manager-test
        tests/model_manager_test.cpp
        src/model_catalog.cpp
        src/model_manager.cpp
    )
    target_include_directories(model-manager-test PRIVATE src)
    target_link_libraries(model-manager-test PRIVATE Qt6::Core Qt6::Network)
    if(MSVC)
        target_compile_options(model-manager-test PRIVATE /utf-8 /W4 /permissive-)
    endif()
    add_test(NAME model-manager-test COMMAND model-manager-test)
endif()

# Release builds define NDEBUG, which turns assert() into a no-op. Test
# executables must keep their assertions active in every configuration.
foreach(test_target
    windows-text-injector-test
    stability-tracker-test
    text-processor-test
    audio-conditioner-test
    compute-policy-test
    fbank-test
    hotword-boost-test
    translator-test
    model-manager-test
)
    if(TARGET ${test_target})
        if(MSVC)
            target_compile_options(${test_target} PRIVATE $<$<CONFIG:Release>:/UNDEBUG>)
        else()
            target_compile_options(${test_target} PRIVATE $<$<CONFIG:Release>:-UNDEBUG>)
        endif()
    endif()
endforeach()

if(TARGET Qt6::Core)
    add_executable(settings-types-test
        tests/settings_types_test.cpp
        src/ui/settings_types.cpp
        src/compute_policy.cpp
        src/text_processor.cpp
        src/translation_session.cpp
        src/languages.cpp
    )
    target_include_directories(settings-types-test PRIVATE src)
    target_link_libraries(settings-types-test PRIVATE Qt6::Core Qt6::Gui ggml)
    if(MSVC)
        target_compile_options(settings-types-test PRIVATE /utf-8 /W4 /permissive-)
    endif()
    add_test(NAME settings-types-test COMMAND settings-types-test)
endif()

if(TARGET Qt6::Core)
    add_executable(design-tokens-test
        tests/design_tokens_test.cpp
        src/ui/design_tokens.cpp
    )
    target_include_directories(design-tokens-test PRIVATE src)
    target_link_libraries(design-tokens-test PRIVATE Qt6::Core Qt6::Gui)
    if(MSVC)
        target_compile_options(design-tokens-test PRIVATE /utf-8 /W4 /permissive-)
    endif()
    add_test(NAME design-tokens-test COMMAND design-tokens-test)
endif()

if(SENSEVOICE_CT2_SHARED)
    # The CUDA translation build links CTranslate2 as a DLL; every executable
    # that uses it needs the DLL beside it, tests included.
    foreach(target sensevoice-stream sensevoice-ui translator-test)
        if(TARGET ${target})
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:ctranslate2>"
                    "$<TARGET_FILE_DIR:${target}>")
        endif()
    endforeach()
endif()
