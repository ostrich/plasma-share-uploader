pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

PageScrollView {
    id: root

    required property TestController controller
    required property TargetDraft draft

    ColumnLayout {
        id: panel

        height: Math.max(root.availableHeight, implicitHeight)
        width: root.availableWidth

        Label {
            Layout.fillWidth: true
            text: qsTr("Validate and response parsing stay local. Preview runs preprocessing commands. Upload test file sends the sample to the target's endpoint.")
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
        }
        RowLayout {
            Button {
                objectName: "validateTarget"
                text: qsTr("Validate")

                onClicked: {
                    root.controller.validate();
                    results.currentIndex = 1;
                }
            }
            Button {
                enabled: !root.controller.busy
                text: qsTr("Preview preprocessing")

                onClicked: {
                    root.controller.start(true);
                    results.currentIndex = 1;
                }
            }
            Button {
                enabled: !root.controller.busy
                objectName: "uploadTestFile"
                text: qsTr("Upload test file")

                onClicked: {
                    root.controller.start(false);
                    results.currentIndex = 1;
                }
            }
            Button {
                enabled: root.controller.busy
                text: qsTr("Cancel")

                onClicked: root.controller.cancel()
            }
        }
        RowLayout {
            TextField {
                Layout.fillWidth: true
                enabled: !root.controller.busy
                objectName: "testFile"
                placeholderText: qsTr("Leave empty to generate a small PNG test image")
                text: root.controller.file

                onTextEdited: root.controller.file = text
            }
            Button {
                enabled: !root.controller.busy
                text: qsTr("Choose file…")

                onClicked: fileDialog.open()
            }
        }
        ProgressBar {
            Layout.fillWidth: true
            indeterminate: root.controller.progress < 0
            value: root.controller.progress
        }
        TabBar {
            id: results

            Layout.fillWidth: true

            TabButton {
                text: qsTr("Response inspector")
            }
            TabButton {
                text: qsTr("Diagnostics")
            }
        }
        StackLayout {
            Layout.fillHeight: true
            Layout.fillWidth: true
            Layout.minimumHeight: responsePage.implicitHeight
            currentIndex: results.currentIndex

            ColumnLayout {
                id: responsePage

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2

                    Label {
                        text: qsTr("Response URL")
                        textFormat: Text.PlainText
                    }
                    TextField {
                        Layout.fillWidth: true
                        text: root.controller.responseUrl

                        onTextEdited: root.controller.responseUrl = text
                    }
                    Label {
                        text: qsTr("HTTP status")
                        textFormat: Text.PlainText
                    }
                    SpinBox {
                        editable: true
                        from: 100
                        live: true
                        to: 599
                        value: root.controller.status

                        onValueModified: root.controller.status = value
                    }
                    Label {
                        Layout.alignment: Qt.AlignTop
                        text: qsTr("Response headers")
                        textFormat: Text.PlainText
                    }
                    StringTable {
                        rows: root.controller.headers
                    }
                }
                SplitView {
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    Layout.minimumHeight: 130

                    ScrollView {
                        SplitView.fillWidth: true
                        SplitView.minimumWidth: 120
                        clip: true

                        TextArea {
                            font.family: "monospace"
                            objectName: "testResponse"
                            placeholderText: qsTr("Paste a response body here, or capture one with Upload test file.")
                            text: root.controller.responseBody
                            wrapMode: TextEdit.Wrap

                            onTextChanged: if (activeFocus && text !== root.controller.responseBody)
                                root.controller.responseBody = text
                        }
                    }
                    TreeView {
                        id: tree

                        SplitView.minimumWidth: 120
                        SplitView.preferredWidth: 320
                        clip: true
                        columnWidthProvider: function (column) {
                            return column === 0 ? 145 : Math.max(145, width - 145);
                        }
                        model: root.controller.tree
                        objectName: "responseTree"

                        ScrollBar.horizontal: ScrollBar {}
                        ScrollBar.vertical: ScrollBar {}
                        delegate: TreeViewDelegate {}
                        selectionModel: ItemSelectionModel {
                            model: root.controller.tree
                        }
                    }
                }
                RowLayout {
                    Button {
                        objectName: "testResponseParsing"
                        text: qsTr("Test response parsing")

                        onClicked: {
                            root.controller.parseResponse();
                            results.currentIndex = 1;
                        }
                    }
                    ComboBox {
                        id: output

                        model: [qsTr("Shared URL"), qsTr("Thumbnail"), qsTr("Deletion"), qsTr("Error")]
                    }
                    Button {
                        enabled: root.draft.editable && tree.selectionModel.hasSelection
                        text: qsTr("Use selected JSON value")

                        onClicked: root.controller.usePointer(tree.selectionModel.currentIndex, ["", "thumbnail", "deletion", "error"][output.currentIndex])
                    }
                }
            }
            ScrollView {
                clip: true

                TextArea {
                    font.family: "monospace"
                    objectName: "testLog"
                    readOnly: true
                    text: root.controller.diagnostics
                    wrapMode: TextEdit.Wrap
                }
            }
        }
        RowLayout {
            Button {
                enabled: root.controller.preview.length > 0
                text: qsTr("Open prepared file")

                onClicked: root.controller.openPreview()
            }
            Button {
                text: qsTr("Copy diagnostics")

                onClicked: root.controller.copyDiagnostics()
            }
            Button {
                text: qsTr("Clear")

                onClicked: root.controller.clearDiagnostics()
            }
            Item {
                Layout.fillWidth: true
            }
        }
        Connections {
            function onCaptureCompleted() {
                results.currentIndex = 0;
            }
            function onResponseChanged() {
                Qt.callLater(function () {
                    tree.expandRecursively(-1, 3);
                });
            }

            target: root.controller
        }
        FileDialog {
            id: fileDialog

            title: qsTr("Test file")

            onAccepted: root.controller.setFileUrl(selectedFile)
        }
    }
}
