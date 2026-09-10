find_package(Qt6 REQUIRED COMPONENTS Test Widgets Xml Qml)

set(IMSHARE_TEST_SUPPORT_SOURCES
    cpp/httpcaptureserver.cpp
)

add_library(imshare_test_support STATIC ${IMSHARE_TEST_SUPPORT_SOURCES})
target_link_libraries(imshare_test_support PUBLIC Qt6::Core Qt6::Network Qt6::Test)
target_include_directories(imshare_test_support PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp
)

function(imshare_add_cpp_test target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Test
        Qt6::Widgets
        Qt6::Xml
        KF6::CoreAddons
        KF6::Purpose
        KF6::Notifications
        imshare_test_support
        imshare_runtime
    )
    target_include_directories(${target} PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/cpp
    )
    target_compile_definitions(${target} PRIVATE
        QT_NO_CAST_FROM_ASCII
        IMSHARE_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

imshare_add_cpp_test(imshare_shareinpututils_tests
    cpp/test_shareinpututils.cpp
)

imshare_add_cpp_test(imshare_targetuploader_utils_tests
    cpp/test_targetuploader_utils.cpp
)

imshare_add_cpp_test(imshare_preuploadprocessor_tests
    cpp/test_preuploadprocessor.cpp
)

imshare_add_cpp_test(imshare_targetuploader_tests
    cpp/test_targetuploader.cpp
)

imshare_add_cpp_test(imshare_targetregistry_tests
    cpp/test_targetregistry.cpp
)

imshare_add_cpp_test(imshare_sharejob_tests
    cpp/test_sharejob.cpp
)

imshare_add_cpp_test(imshare_runtimeplugin_tests cpp/test_runtimeplugin.cpp)
target_link_libraries(imshare_runtimeplugin_tests PRIVATE Qt6::Qml)
target_compile_definitions(imshare_runtimeplugin_tests PRIVATE
    IMSHARE_TEST_PLUGIN_PATH="$<TARGET_FILE:runtimeuploadplugin>"
)
add_dependencies(imshare_runtimeplugin_tests runtimeuploadplugin)

imshare_add_cpp_test(imshare_targetmanager_tests cpp/test_targetmanager.cpp)
target_link_libraries(imshare_targetmanager_tests PRIVATE imshare_manager)
