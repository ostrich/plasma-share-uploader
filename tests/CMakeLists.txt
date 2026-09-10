find_package(Qt6 6.8 REQUIRED COMPONENTS Test Widgets Xml Qml QuickTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

qt_add_executable(imshare_targetformat_check cpp/targetformatcheck.cpp)
target_link_libraries(imshare_targetformat_check PRIVATE imshare_core)
add_test(NAME imshare_target_schema_tests
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/validate_target_schema.py
        $<TARGET_FILE:imshare_targetformat_check>
)

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
    )
    if (target STREQUAL "imshare_runtimeplugin_tests")
        # Load the real plugin, without a second copy of its static QML module in this host.
        target_link_libraries(${target} PRIVATE imshare_core Qt6::Quick)
    else()
        target_link_libraries(${target} PRIVATE imshare_runtime)
    endif()
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
target_link_libraries(imshare_targetmanager_tests PRIVATE imshare_ui)
imshare_add_cpp_test(imshare_picker_tests cpp/test_picker.cpp)
set_tests_properties(imshare_picker_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

qt_add_executable(imshare_qml_tests cpp/qmltests.cpp cpp/uifixture.h)
target_link_libraries(imshare_qml_tests PRIVATE imshare_ui imshare_uiplugin imshare_test_support Qt6::QuickTest Qt6::Widgets)
target_compile_definitions(imshare_qml_tests PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml" IMSHARE_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME imshare_qml_tests COMMAND imshare_qml_tests)
set_tests_properties(imshare_qml_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
