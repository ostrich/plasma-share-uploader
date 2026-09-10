set(IMSHARE_CORE_SOURCES
    credentialstore.cpp
    constraintmatcher.cpp
    preuploadprocessor.cpp
    shareinpututils.cpp
    targetconfigparser.cpp
    targetcoreconfigparser.cpp
    targetconfigvalidator.cpp
    targetdefinition.cpp
    targeticonprovider.cpp
    targetpreuploadconfigparser.cpp
    targetrequestconfigparser.cpp
    targetresponseconfigparser.cpp
    targetfilestore.cpp
    targetregistry.cpp
    targetuploader.cpp
    targetuploader_utils.cpp
)

add_library(imshare_core STATIC ${IMSHARE_CORE_SOURCES})
set_target_properties(imshare_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(imshare_runtime STATIC sharejob.cpp targetpickerdialog.cpp)
set_target_properties(imshare_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(imshare_runtime PUBLIC imshare_core KF6::Purpose KF6::Notifications)
add_library(runtimeuploadplugin MODULE runtimeplugin.cpp)
target_link_libraries(runtimeuploadplugin PRIVATE imshare_runtime)

add_library(imshare_manager STATIC configwidgets.cpp targeteditor.cpp credentialpanel.cpp
    uploadtestrunner.cpp targettestpanel.cpp targetmanagerwindow.cpp)
target_link_libraries(imshare_manager PUBLIC imshare_core)
add_executable(plasma-share-uploader-config targetmanagerapp.cpp)
target_link_libraries(plasma-share-uploader-config PRIVATE imshare_manager)
install(TARGETS plasma-share-uploader-config DESTINATION ${KDE_INSTALL_BINDIR})
install(FILES org.kde.plasma-share-uploader.config.desktop DESTINATION ${KDE_INSTALL_APPDIR})

target_link_libraries(imshare_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Widgets
    Qt6::Xml
    KF6::CoreAddons
    KF6::Wallet
)

target_include_directories(imshare_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(imshare_core PUBLIC
    QT_NO_CAST_FROM_ASCII
    PLASMA_SHARE_UPLOADER_CONFIG_PATH="${KDE_INSTALL_FULL_BINDIR}/plasma-share-uploader-config"
    PLASMA_SHARE_UPLOADER_VERSION="${PROJECT_VERSION}"
    PLASMA_SHARE_UPLOADER_SYSTEM_ICONS_PATH="${KDE_INSTALL_FULL_DATADIR}/plasma-share-uploader/icons"
    PLASMA_SHARE_UPLOADER_SYSTEM_TARGETS_PATH="${KDE_INSTALL_FULL_DATADIR}/plasma-share-uploader/targets"
)

if (PLASMA_SHARE_UPLOADER_USE_SOURCE_DATA)
    target_compile_definitions(imshare_core PUBLIC
        PLASMA_SHARE_UPLOADER_DEV_CONFIG_PATH="${CMAKE_CURRENT_BINARY_DIR}/plasma-share-uploader-config"
        PLASMA_SHARE_UPLOADER_DEV_ICONS_PATH="${CMAKE_SOURCE_DIR}/icons"
        PLASMA_SHARE_UPLOADER_DEV_TARGETS_PATH="${CMAKE_SOURCE_DIR}/targets"
    )
endif()

set_target_properties(runtimeuploadplugin PROPERTIES
    OUTPUT_NAME "runtimeuploadplugin"
    PREFIX ""
)

install(TARGETS runtimeuploadplugin
    DESTINATION ${KDE_INSTALL_QTPLUGINDIR}/kf6/purpose
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/targets
    DESTINATION ${KDE_INSTALL_DATADIR}/plasma-share-uploader
)
