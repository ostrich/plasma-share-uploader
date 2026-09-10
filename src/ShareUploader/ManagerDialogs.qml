pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: root

    required property TargetManagerController controller

    function showDetails() {
        details.open();
    }

    Connections {
        function onConfirmationRequested(title, text) {
            confirmation.title = title;
            confirmText.text = text;
            confirmation.open();
        }
        function onDiscardRequested() {
            discard.open();
        }
        function onError(text) {
            errorText.text = text;
            errorDialog.open();
        }
        function onExportReviewRequested(text, replaced) {
            exportText.text = text;
            exportHelp.text = qsTr("Review before sharing. Inline secrets replaced: %1. Wallet and environment references contain names only. Check for any other private values.").arg(replaced);
            review.open();
        }
        function onFileDialogRequested(kind, name) {
            if (kind === "import")
                importDialog.open();
            else {
                exportDialog.selectedFile = name;
                exportDialog.open();
            }
        }

        target: root.controller
    }
    Dialog {
        id: discard

        anchors.centerIn: Overlay.overlay
        closePolicy: Popup.NoAutoClose
        modal: true
        objectName: "discardDialog"
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel
        title: qsTr("Unsaved changes")

        onAccepted: root.controller.resolveDraft("save")
        onDiscarded: root.controller.resolveDraft("discard")
        onRejected: root.controller.resolveDraft("cancel")

        Label {
            text: qsTr("Save changes to this target before continuing?")
            textFormat: Text.PlainText
        }
    }
    Dialog {
        id: confirmation

        anchors.centerIn: Overlay.overlay
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        width: Math.min(520, Overlay.overlay.width - 40)

        onAccepted: root.controller.confirm(true)
        onRejected: root.controller.confirm(false)

        Label {
            id: confirmText

            textFormat: Text.PlainText
            width: parent.width
            wrapMode: Text.Wrap
        }
    }
    Dialog {
        id: errorDialog

        anchors.centerIn: Overlay.overlay
        modal: true
        standardButtons: Dialog.Ok
        title: qsTr("Target configuration")
        width: Math.min(640, Overlay.overlay.width - 40)

        ScrollView {
            clip: true
            height: Math.min(errorText.implicitHeight, 350)
            width: parent.width

            TextArea {
                id: errorText

                readOnly: true
                wrapMode: TextEdit.Wrap
            }
        }
    }
    Dialog {
        id: details

        anchors.centerIn: Overlay.overlay
        height: Math.min(420, Overlay.overlay.height - 40)
        modal: true
        objectName: "diagnosticsDialog"
        standardButtons: Dialog.Close
        title: qsTr("Target diagnostics")
        width: Math.min(720, Overlay.overlay.width - 40)

        ScrollView {
            anchors.fill: parent
            clip: true

            TextArea {
                readOnly: true
                text: root.controller.diagnosticText
                wrapMode: TextEdit.Wrap
            }
        }
    }
    Dialog {
        id: review

        anchors.centerIn: Overlay.overlay
        height: Math.min(600, Overlay.overlay.height - 40)
        modal: true
        objectName: "exportReview"
        standardButtons: Dialog.Save | Dialog.Cancel
        title: qsTr("Review portable JSON")
        width: Math.min(800, Overlay.overlay.width - 40)

        onAccepted: root.controller.acceptExport(exportText.text)

        ColumnLayout {
            anchors.fill: parent

            Label {
                id: exportHelp

                Layout.fillWidth: true
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }
            ScrollView {
                Layout.fillHeight: true
                Layout.fillWidth: true
                clip: true

                TextArea {
                    id: exportText

                    font.family: "monospace"
                    wrapMode: TextEdit.NoWrap
                }
            }
        }
    }
    FileDialog {
        id: importDialog

        nameFilters: [qsTr("JSON files (*.json)"), qsTr("All files (*)")]
        title: qsTr("Import target JSON")

        onAccepted: root.controller.importFile(selectedFile)
    }
    FileDialog {
        id: exportDialog

        defaultSuffix: "json"
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("JSON files (*.json)")]
        objectName: "exportFileDialog"
        title: qsTr("Export target JSON")

        onAccepted: root.controller.exportFile(selectedFile)
    }
}
