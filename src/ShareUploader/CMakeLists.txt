qt_add_library(imshare_ui STATIC)
set_target_properties(imshare_ui PROPERTIES POSITION_INDEPENDENT_CODE ON)
qt_add_qml_module(imshare_ui
    URI ShareUploader
    VERSION 1.0
    CLASS_NAME ShareUploaderPlugin
    SOURCES
        ../jsonutils.h ../jsonutils.cpp
        ../targetdraft.h ../targetdraft.cpp
        ../jsonrowsmodel.h ../jsonrowsmodel.cpp
        ../targetlistmodel.h ../targetlistmodel.cpp
        ../credentialcontroller.h ../credentialcontroller.cpp
        ../testcontroller.h ../testcontroller.cpp
        ../targetmanagercontroller.h ../targetmanagercontroller.cpp
        ../uploadtestrunner.h ../uploadtestrunner.cpp
        ../pickercontroller.h ../pickercontroller.cpp
    QML_FILES
        ManagerWindow.qml ManagerDialogs.qml TargetSidebar.qml
        PageScrollView.qml FormPage.qml FieldLine.qml StringTable.qml DraftTable.qml OrderedList.qml
        GeneralPage.qml RequestPage.qml CredentialsPage.qml ResponsePage.qml
        ExtractorEditor.qml BeforeUploadPage.qml JsonPage.qml TestPage.qml
        PickerWindow.qml
)
target_link_libraries(imshare_ui PUBLIC imshare_core Qt6::Qml Qt6::Quick Qt6::QuickControls2)
