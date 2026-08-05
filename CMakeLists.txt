cmake_minimum_required(VERSION 3.25)

project(OpenScope
    VERSION 0.1.0
    LANGUAGES C CXX
)

set(DECKLINK_SDK_ROOT
    ""
    CACHE PATH
    "Path to the Blackmagic DeckLink SDK"
)

if(NOT DECKLINK_SDK_ROOT)
    message(FATAL_ERROR
        "DECKLINK_SDK_ROOT is not set. "
        "Set it in CMakeUserPresets.json or via -DDECKLINK_SDK_ROOT=..."
    )
endif()

if(NOT EXISTS "${DECKLINK_SDK_ROOT}/Win/include/DeckLinkAPI.idl")
    message(FATAL_ERROR
        "DeckLinkAPI.idl not found below: ${DECKLINK_SDK_ROOT}"
    )
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

set(DECKLINK_IDL
    "${DECKLINK_SDK_ROOT}/Win/include/DeckLinkAPI.idl"
)

set(DECKLINK_GENERATED_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/decklink_generated"
)

set(DECKLINK_GENERATED_HEADER
    "${DECKLINK_GENERATED_DIR}/DeckLinkAPI_h.h"
)

set(DECKLINK_GENERATED_IID
    "${DECKLINK_GENERATED_DIR}/DeckLinkAPI_i.c"
)

add_custom_command(
    OUTPUT
        "${DECKLINK_GENERATED_HEADER}"
        "${DECKLINK_GENERATED_IID}"

    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${DECKLINK_GENERATED_DIR}"

    COMMAND midl
        /nologo
        /env x64
        /h DeckLinkAPI_h.h
        /iid DeckLinkAPI_i.c
        /out "${DECKLINK_GENERATED_DIR}"
        "${DECKLINK_IDL}"

    DEPENDS
        "${DECKLINK_IDL}"

    VERBATIM
)

add_custom_target(DeckLinkApiGenerated
    DEPENDS
        "${DECKLINK_GENERATED_HEADER}"
        "${DECKLINK_GENERATED_IID}"
)

qt_add_executable(OpenScope
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/VideoWidget.cpp
    src/VideoWidget.h
    src/WaveformWidget.h
    src/WaveformWidget.cpp
    src/TestPatternGenerator.cpp
    src/TestPatternGenerator.h
    src/VideoEngine.cpp
    src/VideoEngine.h
    src/VideoSource.cpp
    src/VideoSource.h
    src/DeckLinkProbe.cpp
    src/DeckLinkProbe.h
    src/DeckLinkInputCallback.cpp
    src/DeckLinkInputCallback.h
    src/ScopeViewport.h
    src/ScopeViewport.cpp
    src/ScopeWorkspace.cpp
    src/ScopeWorkspace.h
    src/video/Yuv444Frame.h
    src/video/Uyvy422ToYuv444Converter.h
    src/video/Uyvy422ToYuv444Converter.cpp
    src/video/V210ToYuv444Converter.h
    src/video/V210ToYuv444Converter.cpp
    src/video/VideoConverter.h
    src/video/DisplayConverter.h
    src/video/DisplayConverter.cpp
    src/video/FrameBufferPool.h
    src/video/FrameBufferPool.cpp
    src/analysis/Analyzer.h
    src/rendering/WaveformRenderer.h
    src/rendering/WaveformRenderer.cpp
    src/processing/LineResampler.cpp
    src/processing/LineResampler.h

    "${DECKLINK_GENERATED_IID}"
)

add_dependencies(OpenScope DeckLinkApiGenerated)

target_include_directories(OpenScope
    PRIVATE
        "${DECKLINK_SDK_ROOT}/Win/include"
        "${DECKLINK_GENERATED_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(OpenScope
    PRIVATE
        Qt6::Widgets
        ole32
        comsuppw
)

if(WIN32)
    add_custom_command(TARGET OpenScope POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:OpenScope>/platforms"

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>"
            "$<TARGET_FILE_DIR:OpenScope>/platforms/"
    )
endif()