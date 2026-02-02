cmake_minimum_required(VERSION 3.24)

project(plasma-share-uploader VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6Purpose REQUIRED)
find_package(KF6Notifications REQUIRED)
find_package(KDEInstallDirs QUIET)
include(GNUInstallDirs)

if (NOT KDE_INSTALL_QTPLUGINDIR)
    set(KDE_INSTALL_QTPLUGINDIR "${CMAKE_INSTALL_LIBDIR}/qt6/plugins")
endif()

set(IMSHARE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${IMSHARE_GENERATED_DIR}")

execute_process(
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_targets.py"
            "--input" "${CMAKE_CURRENT_SOURCE_DIR}/targets.json"
            "--output" "${IMSHARE_GENERATED_DIR}"
            "--version" "${PROJECT_VERSION}"
    RESULT_VARIABLE IMSHARE_GEN_RESULT
)
if (NOT IMSHARE_GEN_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to generate targets (exit ${IMSHARE_GEN_RESULT}).")
endif()

include("${IMSHARE_GENERATED_DIR}/targets.cmake")

add_subdirectory(src)
