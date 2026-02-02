set(IMSHARE_COMMON_SOURCES
    sharejob.cpp
    targetuploader.cpp
)

foreach(target_id IN LISTS IMSHARE_TARGET_IDS)
    set(target_plugin "${target_id}plugin")
    add_library(${target_plugin} MODULE
        ${IMSHARE_COMMON_SOURCES}
        "${IMSHARE_GENERATED_DIR}/${target_id}plugin.cpp"
    )

    target_link_libraries(${target_plugin}
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        KF6::CoreAddons
        KF6::Purpose
        KF6::Notifications
    )

    target_include_directories(${target_plugin} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${IMSHARE_GENERATED_DIR}
    )

    target_compile_definitions(${target_plugin} PRIVATE QT_NO_CAST_FROM_ASCII)
    target_compile_definitions(${target_plugin} PRIVATE PLASMA_SHARE_UPLOADER_VERSION="${PROJECT_VERSION}")

    set_target_properties(${target_plugin} PROPERTIES
        OUTPUT_NAME "${target_id}plugin"
        PREFIX ""
    )

    install(TARGETS ${target_plugin}
        DESTINATION ${KDE_INSTALL_QTPLUGINDIR}/kf6/purpose
    )
endforeach()
