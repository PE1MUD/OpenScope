cmake_minimum_required(VERSION 3.25)

project(OpenScope
    VERSION 0.1.0
    LANGUAGES CXX
)

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