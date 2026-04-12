cmake_minimum_required(VERSION 3.24)

project(plasma-share-uploader VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Widgets Xml)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6Purpose REQUIRED)
find_package(KF6Notifications REQUIRED)
find_package(KDEInstallDirs QUIET)
include(GNUInstallDirs)
include(CTest)

if (NOT KDE_INSTALL_QTPLUGINDIR)
    set(KDE_INSTALL_QTPLUGINDIR "${CMAKE_INSTALL_LIBDIR}/qt6/plugins")
endif()

add_subdirectory(src)

if (BUILD_TESTING)
    add_subdirectory(tests)
endif()
