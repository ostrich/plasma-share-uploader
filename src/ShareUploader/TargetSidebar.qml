pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root

    required property TargetManagerController controller

    TextField {
        Layout.fillWidth: true
        objectName: "targetSearch"
        placeholderText: qsTr("Search targets")
        text: root.controller.search

        onTextEdited: root.controller.search = text
    }
    ComboBox {
        Layout.fillWidth: true
        currentIndex: root.controller.filterKind
        model: [qsTr("Configured targets"), qsTr("Enabled targets"), qsTr("Disabled targets"), qsTr("Presets and templates"), qsTr("Needs attention"), qsTr("All targets")]
        objectName: "targetFilter"

        onActivated: root.controller.filterKind = currentIndex
    }
    Frame {
        id: listFrame

        Layout.fillHeight: true
        Layout.fillWidth: true
        padding: 2

        Rectangle {
            anchors.fill: parent
            color: listFrame.palette.base
            z: -1
        }
        ScrollView {
            anchors.fill: parent
            clip: true

            ListView {
                id: targets

                function selectRow(row) {
                    if (row < 0 || row >= count)
                        return;
                    root.controller.request("select", root.controller.targets.pathAt(row));
                }

                activeFocusOnTab: true
                boundsBehavior: Flickable.StopAtBounds
                currentIndex: {
                    root.controller.selectedPath;
                    root.controller.filterKind;
                    root.controller.search;
                    return root.controller.targets.indexOfPath(root.controller.selectedPath);
                }
                keyNavigationEnabled: false
                model: root.controller.targets
                objectName: "targetList"

                delegate: ItemDelegate {
                    id: entry

                    required property int index
                    required property string targetHost
                    required property string targetName
                    required property string targetPath
                    required property list<string> targetProblems
                    required property string targetState

                    Accessible.name: targetName + ", " + targetState + (targetProblems.length ? ", " + qsTr("Needs attention") : "")
                    ToolTip.text: targetName + "\n" + targetState + (targetProblems.length ? "\n" + targetProblems.join("\n") : "")
                    ToolTip.visible: hovered
                    height: 58
                    highlighted: root.controller.selectedPath === targetPath
                    width: ListView.view.width

                    onClicked: {
                        root.controller.request("select", targetPath);
                        targets.forceActiveFocus();
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 8

                        Kirigami.Icon {
                            Layout.preferredHeight: 18
                            Layout.preferredWidth: 18
                            color: entry.highlighted ? entry.palette.highlightedText : entry.palette.text
                            // Keep the full-color diagnostic glyph intact on selection.
                            isMask: !entry.targetProblems.length
                            source: entry.targetProblems.length ? "dialog-error" : "document-send"
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Label {
                                Layout.fillWidth: true
                                color: entry.highlighted ? entry.palette.highlightedText : entry.palette.text
                                elide: Text.ElideRight
                                text: entry.targetName.replace(/\s+/g, " ")
                                textFormat: Text.PlainText
                            }
                            Label {
                                Layout.fillWidth: true
                                color: entry.highlighted ? entry.palette.highlightedText : entry.palette.text
                                elide: Text.ElideRight
                                text: entry.targetState + (entry.targetHost ? " · " + entry.targetHost : "")
                                textFormat: Text.PlainText
                            }
                        }
                    }
                }

                Keys.onPressed: function (event) {
                    if (event.key === Qt.Key_Down)
                        selectRow(currentIndex + 1);
                    else if (event.key === Qt.Key_Up)
                        selectRow(currentIndex - 1);
                    else if (event.key === Qt.Key_Home)
                        selectRow(0);
                    else if (event.key === Qt.Key_End)
                        selectRow(count - 1);
                    else
                        return;
                    event.accepted = true;
                }

                Label {
                    anchors.centerIn: parent
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("No targets match. Add a target or choose another filter.")
                    textFormat: Text.PlainText
                    visible: targets.count === 0
                    width: parent.width - 20
                    wrapMode: Text.Wrap
                }
            }
        }
    }
    Button {
        text: qsTr("Add Target…")

        onClicked: addMenu.open()

        Menu {
            id: addMenu

            MenuItem {
                text: qsTr("New custom target")

                onTriggered: root.controller.request("new")
            }
            MenuItem {
                text: qsTr("Enable a preset")

                onTriggered: root.controller.request("presets")
            }
            MenuItem {
                text: qsTr("Import JSON…")

                onTriggered: root.controller.request("import")
            }
        }
    }
    Button {
        Layout.fillWidth: true
        enabled: !root.controller.test.busy
        text: qsTr("Reload")

        onClicked: root.controller.request("reload")
    }
    Button {
        Layout.fillWidth: true
        text: qsTr("Open target folder")

        onClicked: root.controller.request("folder")
    }
}
