find_package(Qt6 REQUIRED COMPONENTS Test Widgets Xml)

set(IMSHARE_TEST_SUPPORT_SOURCES
    cpp/httpcaptureserver.cpp
)

set(IMSHARE_CORE_SOURCES
    ${PROJECT_SOURCE_DIR}/src/preuploadprocessor.cpp
    ${PROJECT_SOURCE_DIR}/src/targetconfigparser.cpp
    ${PROJECT_SOURCE_DIR}/src/targetpreuploadconfigparser.cpp
    ${PROJECT_SOURCE_DIR}/src/targetcoreconfigparser.cpp
    ${PROJECT_SOURCE_DIR}/src/targetconfigvalidator.cpp
    ${PROJECT_SOURCE_DIR}/src/targetdefinition.cpp
    ${PROJECT_SOURCE_DIR}/src/targeticonprovider.cpp
    ${PROJECT_SOURCE_DIR}/src/targetregistry.cpp
    ${PROJECT_SOURCE_DIR}/src/constraintmatcher.cpp
    ${PROJECT_SOURCE_DIR}/src/targetpickerdialog.cpp
    ${PROJECT_SOURCE_DIR}/src/shareinpututils.cpp
    ${PROJECT_SOURCE_DIR}/src/sharejob.cpp
    ${PROJECT_SOURCE_DIR}/src/targetuploader.cpp
    ${PROJECT_SOURCE_DIR}/src/targetrequestconfigparser.cpp
    ${PROJECT_SOURCE_DIR}/src/targetresponseconfigparser.cpp
    ${PROJECT_SOURCE_DIR}/src/targetuploader_utils.cpp
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
    target_include_directories(${target} PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/cpp
    )
    target_compile_definitions(${target} PRIVATE
        QT_NO_CAST_FROM_ASCII
        PLASMA_SHARE_UPLOADER_VERSION="${PROJECT_VERSION}"
        PLASMA_SHARE_UPLOADER_SYSTEM_ICONS_PATH="${PROJECT_SOURCE_DIR}/icons"
        PLASMA_SHARE_UPLOADER_SYSTEM_TARGETS_PATH="${PROJECT_SOURCE_DIR}/targets"
        PLASMA_SHARE_UPLOADER_DEV_ICONS_PATH="${PROJECT_SOURCE_DIR}/icons"
        PLASMA_SHARE_UPLOADER_DEV_TARGETS_PATH="${PROJECT_SOURCE_DIR}/targets"
        IMSHARE_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

imshare_add_cpp_test(imshare_shareinpututils_tests
    cpp/test_shareinpututils.cpp
    ${PROJECT_SOURCE_DIR}/src/shareinpututils.cpp
)

imshare_add_cpp_test(imshare_targetuploader_utils_tests
    cpp/test_targetuploader_utils.cpp
    ${PROJECT_SOURCE_DIR}/src/targetuploader_utils.cpp
)

imshare_add_cpp_test(imshare_preuploadprocessor_tests
    cpp/test_preuploadprocessor.cpp
    ${PROJECT_SOURCE_DIR}/src/preuploadprocessor.cpp
    ${PROJECT_SOURCE_DIR}/src/targetconfigparser.cpp
    ${PROJECT_SOURCE_DIR}/src/targetcoreconfigparser.cpp
    ${PROJECT_SOURCE_DIR}/src/targetpreuploadconfigparser.cpp
    ${PROJECT_SOURCE_DIR}/src/targetrequestconfigparser.cpp
    ${PROJECT_SOURCE_DIR}/src/targetresponseconfigparser.cpp
)

imshare_add_cpp_test(imshare_targetuploader_tests
    cpp/test_targetuploader.cpp
    ${PROJECT_SOURCE_DIR}/src/preuploadprocessor.cpp
    ${PROJECT_SOURCE_DIR}/src/targetconfigparser.cpp
    ${PROJECT_SOURCE_DIR}/src/targetpreuploadconfigparser.cpp
    ${PROJECT_SOURCE_DIR}/src/targetcoreconfigparser.cpp
    ${PROJECT_SOURCE_DIR}/src/targetrequestconfigparser.cpp
    ${PROJECT_SOURCE_DIR}/src/targetresponseconfigparser.cpp
    ${PROJECT_SOURCE_DIR}/src/targetuploader.cpp
    ${PROJECT_SOURCE_DIR}/src/targetuploader_utils.cpp
)

imshare_add_cpp_test(imshare_targetregistry_tests
    cpp/test_targetregistry.cpp
    ${PROJECT_SOURCE_DIR}/src/constraintmatcher.cpp
    ${PROJECT_SOURCE_DIR}/src/targetconfigparser.cpp
    ${PROJECT_SOURCE_DIR}/src/targetpreuploadconfigparser.cpp
    ${PROJECT_SOURCE_DIR}/src/targetcoreconfigparser.cpp
    ${PROJECT_SOURCE_DIR}/src/targetrequestconfigparser.cpp
    ${PROJECT_SOURCE_DIR}/src/targetresponseconfigparser.cpp
    ${PROJECT_SOURCE_DIR}/src/targetconfigvalidator.cpp
    ${PROJECT_SOURCE_DIR}/src/targetdefinition.cpp
    ${PROJECT_SOURCE_DIR}/src/targetregistry.cpp
)

imshare_add_cpp_test(imshare_sharejob_tests
    cpp/test_sharejob.cpp
    ${PROJECT_SOURCE_DIR}/src/constraintmatcher.cpp
    ${PROJECT_SOURCE_DIR}/src/sharejob.cpp
    ${PROJECT_SOURCE_DIR}/src/shareinpututils.cpp
    ${PROJECT_SOURCE_DIR}/src/preuploadprocessor.cpp
    ${PROJECT_SOURCE_DIR}/src/targetconfigparser.cpp
    ${PROJECT_SOURCE_DIR}/src/targetpreuploadconfigparser.cpp
    ${PROJECT_SOURCE_DIR}/src/targetcoreconfigparser.cpp
    ${PROJECT_SOURCE_DIR}/src/targetrequestconfigparser.cpp
    ${PROJECT_SOURCE_DIR}/src/targetresponseconfigparser.cpp
    ${PROJECT_SOURCE_DIR}/src/targetconfigvalidator.cpp
    ${PROJECT_SOURCE_DIR}/src/targetdefinition.cpp
    ${PROJECT_SOURCE_DIR}/src/targeticonprovider.cpp
    ${PROJECT_SOURCE_DIR}/src/targetpickerdialog.cpp
    ${PROJECT_SOURCE_DIR}/src/targetregistry.cpp
    ${PROJECT_SOURCE_DIR}/src/targetuploader.cpp
    ${PROJECT_SOURCE_DIR}/src/targetuploader_utils.cpp
)
