set(IMSHARE_COMMON_SOURCES
    constraintmatcher.cpp
    preuploadprocessor.cpp
    runtimeplugin.cpp
    sharejob.cpp
    shareinpututils.cpp
    targetcoreconfigparser.cpp
    targetconfigvalidator.cpp
    targetdefinition.cpp
    targetpickerdialog.cpp
    targetregistry.cpp
    targetuploader.cpp
    targetuploader_utils.cpp
)

add_library(runtimeuploadplugin MODULE ${IMSHARE_COMMON_SOURCES})

target_link_libraries(runtimeuploadplugin
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Widgets
    Qt6::Xml
    KF6::CoreAddons
    KF6::Purpose
    KF6::Notifications
)

target_include_directories(runtimeuploadplugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(runtimeuploadplugin PRIVATE
    QT_NO_CAST_FROM_ASCII
    PLASMA_SHARE_UPLOADER_VERSION="${PROJECT_VERSION}"
    PLASMA_SHARE_UPLOADER_SYSTEM_TARGETS_PATH="${CMAKE_INSTALL_FULL_DATADIR}/plasma-share-uploader/targets.d"
)

set_target_properties(runtimeuploadplugin PROPERTIES
    OUTPUT_NAME "runtimeuploadplugin"
    PREFIX ""
)

install(TARGETS runtimeuploadplugin
    DESTINATION ${KDE_INSTALL_QTPLUGINDIR}/kf6/purpose
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/data/targets.d
    DESTINATION ${CMAKE_INSTALL_DATADIR}/plasma-share-uploader
)
