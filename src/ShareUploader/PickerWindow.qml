pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ApplicationWindow {
    id: root

    required property PickerController controller

    flags: Qt.Dialog
    height: 360
    minimumHeight: 300
    minimumWidth: 420
    objectName: "targetPicker"
    title: qsTr("Upload To…")
    width: 460

    onClosing: controller.reject()

    Shortcut {
        sequence: "Escape"

        onActivated: root.controller.reject()
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        Label {
            Layout.fillWidth: true
            text: targets.count ? qsTr("Choose an upload target:") : qsTr("No compatible targets. Configure targets, then reload the list.")
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
        }
        ScrollView {
            Layout.fillHeight: true
            Layout.fillWidth: true
            clip: true

            ListView {
                id: targets

                activeFocusOnTab: true
                currentIndex: count ? 0 : -1
                focus: true
                keyNavigationEnabled: true
                model: root.controller
                spacing: 8

                delegate: Button {
                    id: target

                    required property string iconSource
                    required property int index
                    required property string targetDescription
                    required property string targetName

                    Accessible.name: targetName + ". " + targetDescription
                    height: Math.max(56, labels.implicitHeight + 16)
                    highlighted: ListView.isCurrentItem
                    objectName: "pickerTarget" + index
                    width: ListView.view.width

                    onClicked: root.controller.choose(index)

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 9
                        spacing: 12

                        Item {
                            Layout.alignment: Qt.AlignTop
                            Layout.preferredHeight: 36
                            Layout.preferredWidth: 36

                            Image {
                                id: targetImage

                                anchors.fill: parent
                                asynchronous: true
                                fillMode: Image.PreserveAspectCrop
                                source: target.iconSource.startsWith("file:") ? target.iconSource : ""
                                sourceSize: Qt.size(36 * Screen.devicePixelRatio, 36 * Screen.devicePixelRatio)
                                visible: status === Image.Ready
                            }
                            Kirigami.Icon {
                                anchors.fill: parent
                                isMask: false
                                source: target.iconSource.startsWith("file:") ? "image-x-generic" : target.iconSource
                                visible: !targetImage.visible
                            }
                        }
                        ColumnLayout {
                            id: labels

                            Layout.fillWidth: true
                            spacing: 2

                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                font.bold: true
                                text: target.targetName.replace(/\s+/g, " ")
                                textFormat: Text.PlainText
                            }
                            Label {
                                Layout.fillWidth: true
                                text: target.targetDescription
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }

                Keys.onEnterPressed: root.controller.choose(currentIndex)
                Keys.onReturnPressed: root.controller.choose(currentIndex)
                Keys.onSpacePressed: root.controller.choose(currentIndex)
            }
        }
        Label {
            Layout.fillWidth: true
            text: root.controller.message
            textFormat: Text.PlainText
            visible: text.length > 0
            wrapMode: Text.Wrap
        }
        RowLayout {
            Button {
                icon.name: "configure"
                objectName: "configureTargets"
                text: qsTr("Configure…")

                onClicked: root.controller.configure()
            }
            Button {
                objectName: "reloadTargets"
                text: qsTr("Reload")

                onClicked: root.controller.reload()
            }
            Button {
                text: qsTr("%1 errors").arg(root.controller.diagnosticCount)
                visible: root.controller.diagnosticCount > 0

                onClicked: details.open()
            }
            Item {
                Layout.fillWidth: true
            }
            Button {
                objectName: "cancelPicker"
                text: qsTr("Cancel")

                onClicked: root.controller.reject()
            }
        }
    }
    Dialog {
        id: details

        anchors.centerIn: Overlay.overlay
        height: root.height - 30
        modal: true
        standardButtons: Dialog.Close
        title: qsTr("Target Configuration Errors")
        width: root.width - 30

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
}
