pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root

    property bool allowClose: false
    required property TargetManagerController controller

    height: 860
    minimumHeight: 720
    minimumWidth: 960
    objectName: "managerWindow"
    title: qsTr("Plasma Share Uploader Settings")
    visible: true
    width: 1180

    footer: Label {
        bottomPadding: 3
        elide: Text.ElideRight
        leftPadding: 8
        rightPadding: 8
        text: root.controller.message
        textFormat: Text.PlainText
        topPadding: 3
    }

    Component.onCompleted: if (controller.hasEntry && !controller.draft.hasObject)
        tabs.currentIndex = 5
    onClosing: function (close) {
        if (!allowClose) {
            close.accepted = false;
            controller.request("close");
        }
    }

    Connections {
        function onCloseReady() {
            root.allowClose = true;
            // Finish the original closing event before retrying an approved close.
            Qt.callLater(root.close);
        }
        function onShowTab(index) {
            tabs.currentIndex = index;
        }

        target: root.controller
    }
    Shortcut {
        sequences: [StandardKey.Save]

        onActivated: root.controller.request("save")
    }
    Shortcut {
        sequences: [StandardKey.Close]

        onActivated: root.controller.request("close")
    }
    SplitView {
        anchors.fill: parent
        anchors.margins: 8

        handle: Rectangle {
            color: SplitHandle.pressed ? root.palette.highlight : "transparent"
            implicitHeight: 8
            implicitWidth: 8
        }

        TargetSidebar {
            SplitView.maximumWidth: Math.max(210, root.width - 680)
            SplitView.minimumWidth: 210
            SplitView.preferredWidth: 256
            controller: root.controller
        }
        ColumnLayout {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 640
            spacing: 8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Label {
                    Layout.fillWidth: true
                    ToolTip.text: root.controller.title
                    ToolTip.visible: titleHover.hovered
                    elide: Text.ElideRight
                    font.bold: true
                    font.pointSize: root.font.pointSize * 1.3
                    objectName: "targetTitle"
                    text: root.controller.title.replace(/\s+/g, " ")
                    textFormat: Text.PlainText

                    HoverHandler {
                        id: titleHover
                    }
                }
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: root.controller.state
                    textFormat: Text.PlainText
                }
                Label {
                    Layout.fillWidth: true
                    ToolTip.text: text
                    ToolTip.visible: pathHover.hovered
                    elide: Text.ElideMiddle
                    text: root.controller.selectedPath.replace(/\s+/g, " ") || " "
                    textFormat: Text.PlainText

                    HoverHandler {
                        id: pathHover
                    }
                }
            }
            RowLayout {
                Button {
                    enabled: root.controller.actions.toggle
                    objectName: "toggleTarget"
                    text: root.controller.actions.toggleText

                    onClicked: root.controller.request("toggle")
                }
                Button {
                    enabled: root.controller.actions.customize
                    text: root.controller.actions.customizeText

                    onClicked: root.controller.request("customize")
                }
                Button {
                    enabled: root.controller.actions.duplicate
                    objectName: "duplicateTarget"
                    text: qsTr("Duplicate")

                    onClicked: root.controller.request("duplicate")
                }
                Button {
                    enabled: root.controller.actions.export
                    objectName: "exportTarget"
                    text: qsTr("Export…")

                    onClicked: root.controller.request("export")
                }
                Button {
                    enabled: root.controller.actions.restore
                    text: qsTr("Restore preset…")

                    onClicked: root.controller.request("restore")
                }
                Button {
                    enabled: root.controller.actions.delete
                    text: qsTr("Delete…")

                    onClicked: root.controller.request("delete")
                }
            }
            TabBar {
                id: tabs

                Layout.fillWidth: true
                enabled: root.controller.hasEntry
                objectName: "editorTabs"

                TabButton {
                    text: qsTr("General")
                }
                TabButton {
                    text: qsTr("Request")
                }
                TabButton {
                    text: qsTr("Credentials")
                }
                TabButton {
                    text: qsTr("Response")
                }
                TabButton {
                    text: qsTr("Before Upload")
                }
                TabButton {
                    text: qsTr("JSON")
                }
                TabButton {
                    text: qsTr("Test")
                }
            }
            Frame {
                Layout.fillHeight: true
                Layout.fillWidth: true
                objectName: "editorFrame"
                padding: 0

                StackLayout {
                    anchors.fill: parent
                    currentIndex: tabs.currentIndex
                    enabled: root.controller.hasEntry

                    GeneralPage {
                        draft: root.controller.draft
                    }
                    RequestPage {
                        draft: root.controller.draft
                    }
                    CredentialsPage {
                        controller: root.controller.credentials
                        draft: root.controller.draft
                    }
                    ResponsePage {
                        draft: root.controller.draft
                    }
                    BeforeUploadPage {
                        draft: root.controller.draft
                    }
                    Item {
                        JsonPage {
                            anchors.fill: parent
                            anchors.margins: 10
                            draft: root.controller.draft
                        }
                    }
                    TestPage {
                        controller: root.controller.test
                        draft: root.controller.draft
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true

                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    objectName: "targetStatus"
                    text: root.controller.status
                    textFormat: Text.PlainText
                }
                Button {
                    enabled: root.controller.diagnosticText.length > 0
                    objectName: "targetDiagnostics"
                    text: qsTr("Details…")

                    onClicked: dialogs.showDetails()
                }
            }
            RowLayout {
                Button {
                    enabled: root.controller.actions.open
                    text: qsTr("Open JSON externally")

                    onClicked: root.controller.request("open")
                }
                Item {
                    Layout.fillWidth: true
                }
                Button {
                    enabled: root.controller.actions.save
                    objectName: "saveTarget"
                    text: root.controller.actions.saveText

                    onClicked: root.controller.request("save")
                }
            }
        }
    }
    ManagerDialogs {
        id: dialogs

        controller: root.controller
    }
}
