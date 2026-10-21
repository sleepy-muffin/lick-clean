cmake_minimum_required(VERSION 3.20)

project(
        lick-clean
        LANGUAGES CXX
)

# ========== ---------- ---------- ---------- ==========
#                   PLATFORM VARIABLES
# ========== ---------- ---------- ---------- ==========


# Platform string
if (WIN32)
    set(PLATFORM "windows")
elseif (APPLE)
    set(PLATFORM "macos")
elseif (UNIX)
    set(PLATFORM "linux")
else ()
    set(PLATFORM "unknown")
endif ()

# Architecture
if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(ARCH "x86_64")
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64")
    set(ARCH "arm64")
else ()
    set(ARCH "${CMAKE_SYSTEM_PROCESSOR}")
endif ()

# ========== ---------- ---------- ---------- ==========
#                         SET UP
# ========== ---------- ---------- ---------- ==========


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The application uses Qt Widgets without moc (no Q_OBJECT anywhere), so AUTOMOC
# stays off. AUTORCC embeds the window icon from assets/appicon.
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# shark library
#
# Only the audio module is used. The optional modules are turned off before
# shark is added because shark resolves the dependencies of every enabled
# module at configure time: image would require OpenGL/JPEG/PNG, external
# Boost.JSON and sqlite3 SQLite, none of which this project touches.
set(SHARK_BUILD_EXTERNAL OFF CACHE BOOL "Build shark's Boost.JSON helpers")
set(SHARK_BUILD_SQLITE3 OFF CACHE BOOL "Build shark's SQLite3 helpers")
set(SHARK_BUILD_IMAGE OFF CACHE BOOL "Build shark's OpenGL/JPEG/PNG helpers")

add_subdirectory(
        external/shark
        build/shark
)

add_library(
        ${PROJECT_NAME}-core
        STATIC
        src/declick.cpp
        src/declick.hpp
        src/utils.hpp
)

target_include_directories(
        ${PROJECT_NAME}-core
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
        ${PROJECT_NAME}-core
        PUBLIC
        shark::audio
)

# vcpkg's opusfile/opusenc import libraries do not record their libogg
# dependency on Windows the way ELF shared libraries do, so the linker has to
# be told about ogg (and opus) explicitly or opusfile's ogg_* symbols stay
# unresolved. shark only looks for opusfile/opusenc itself.
if (WIN32)
    find_library(OGG_LIBRARY NAMES ogg libogg REQUIRED)
    find_library(OPUS_LIBRARY NAMES opus libopus REQUIRED)

    target_link_libraries(
            ${PROJECT_NAME}-core
            PUBLIC
            ${OGG_LIBRARY}
            ${OPUS_LIBRARY}
    )
endif ()


set_target_properties(
        ${PROJECT_NAME}-core
        PROPERTIES
        OUTPUT_NAME "${PROJECT_NAME}-core-${PLATFORM}-${ARCH}"
)

# ========== ---------- ---------- ---------- ==========
#                           UI
# ========== ---------- ---------- ---------- ==========

add_executable(
        ${PROJECT_NAME}
        src/ui.cpp
        assets/appicon/app.qrc
)

target_link_libraries(
        ${PROJECT_NAME}
        PRIVATE
        ${PROJECT_NAME}-core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)


set_target_properties(
        ${PROJECT_NAME}
        PROPERTIES
        OUTPUT_NAME "${PROJECT_NAME}-${PLATFORM}-${ARCH}"
)


# ========== ---------- ---------- ---------- ==========
#                        TERMINAL
# ========== ---------- ---------- ---------- ==========

add_executable(
        ${PROJECT_NAME}-cli
        src/cli.cpp
)

target_link_libraries(
        ${PROJECT_NAME}-cli
        PRIVATE
        ${PROJECT_NAME}-core
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME}-cli PRIVATE Shell32)
endif()


set_target_properties(
        ${PROJECT_NAME}-cli
        PROPERTIES
        OUTPUT_NAME "${PROJECT_NAME}-cli-${PLATFORM}-${ARCH}"
)
