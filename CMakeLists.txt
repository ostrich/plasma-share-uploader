cmake_minimum_required(VERSION 3.24)

project(plasma-share-uploader VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(ECM REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
set(QT_MAJOR_VERSION 6)
# Purpose must find the plugin even when data is installed under /usr/local.
option(KDE_INSTALL_USE_QT_SYS_PATHS "Install plugins in Qt's system search path" ON)
include(KDEInstallDirs6)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Widgets Xml)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6Purpose REQUIRED)
find_package(KF6Notifications REQUIRED)
include(CTest)

option(PLASMA_SHARE_UPLOADER_USE_SOURCE_DATA "Load targets and icons from the source checkout (development only)" OFF)

add_subdirectory(src)

if (BUILD_TESTING)
    add_subdirectory(tests)
endif()
