cmake_minimum_required(VERSION 3.25)

project(OpenScope
    VERSION 0.1.0
    LANGUAGES CXX
)

set(DECKLINK_SDK_ROOT
    "C:/Users/David/Documents/Blackmagic_DeckLink_SDK_16.0"
    CACHE PATH
    "Path to the Blackmagic DeckLink SDK"
)

if(NOT DECKLINK_SDK_ROOT)
    message(FATAL_ERROR
        "DECKLINK_SDK_ROOT is not set. "
        "Point it to the root of the DeckLink SDK."
    )
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

qt_add_executable(OpenScope
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/VideoWidget.cpp
    src/VideoWidget.h
    src/TestPatternGenerator.cpp
    src/TestPatternGenerator.h
    src/VideoEngine.cpp
    src/VideoEngine.h
    src/VideoSource.cpp
    src/VideoSource.h
    src/DeckLinkProbe.cpp
)

target_include_directories(OpenScope
    PRIVATE
        "${DECKLINK_SDK_ROOT}/Win/include"
)

target_link_libraries(OpenScope
    PRIVATE
        Qt6::Widgets
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

target_sources(OpenScope PRIVATE
    src/main.cpp
    src/MainWindow.cpp
)