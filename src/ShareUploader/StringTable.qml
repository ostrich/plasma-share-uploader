pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    required property JsonRowsModel rows
    property bool writable: true

    Layout.fillWidth: true
    spacing: 4

    RowLayout {
        Label {
            Layout.fillWidth: true
            text: qsTr("Name")
            textFormat: Text.PlainText
            visible: root.rows.map
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("Value")
            textFormat: Text.PlainText
        }
    }
    Frame {
        id: tableFrame

        Layout.fillWidth: true
        Layout.preferredHeight: 112
        padding: 1

        Rectangle {
            anchors.fill: parent
            color: tableFrame.palette.base
            z: -1
        }
        ScrollView {
            anchors.fill: parent
            clip: true

            ListView {
                id: grid

                boundsBehavior: Flickable.StopAtBounds
                currentIndex: -1
                model: root.rows
                objectName: root.objectName + ".rows"

                delegate: RowLayout {
                    id: row

                    required property int index
                    required property string rowName
                    required property string rowText

                    function editFirst() {
                        (root.rows.map ? nameField : valueField).forceActiveFocus();
                    }

                    spacing: 4
                    width: ListView.view.width

                    onActiveFocusChanged: if (activeFocus)
                        editFirst()

                    TextField {
                        id: nameField

                        Accessible.name: qsTr("Field name, row %1").arg(row.index + 1)
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        objectName: root.objectName + ".name." + row.index
                        readOnly: !root.writable
                        text: row.rowName
                        visible: root.rows.map

                        onActiveFocusChanged: if (activeFocus)
                            grid.currentIndex = row.index
                        onTextEdited: root.rows.edit(row.index, 0, text)
                    }
                    TextField {
                        id: valueField

                        Accessible.name: qsTr("Value, row %1").arg(row.index + 1)
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        objectName: root.objectName + ".value." + row.index
                        readOnly: !root.writable
                        text: row.rowText

                        onActiveFocusChanged: if (activeFocus)
                            grid.currentIndex = row.index
                        onTextEdited: root.rows.edit(row.index, root.rows.map ? 1 : 0, text)
                    }
                }
            }
        }
    }
    RowLayout {
        Button {
            enabled: root.writable
            objectName: root.objectName + ".add"
            text: qsTr("Add")

            onClicked: {
                root.rows.append("");
                grid.currentIndex = root.rows.count - 1;
                grid.positionViewAtEnd();
                Qt.callLater(function () {
                    if (grid.currentItem)
                        grid.currentItem.forceActiveFocus();
                });
            }
        }
        Button {
            enabled: root.writable && grid.currentIndex >= 0 && grid.currentIndex < root.rows.count
            objectName: root.objectName + ".remove"
            text: qsTr("Remove")

            onClicked: {
                root.rows.remove(grid.currentIndex);
                grid.currentIndex = Math.min(grid.currentIndex, root.rows.count - 1);
            }
        }
        Button {
            enabled: root.writable && grid.currentIndex > 0
            text: qsTr("Up")
            visible: !root.rows.map

            onClicked: {
                const i = grid.currentIndex;
                root.rows.move(i, -1);
                grid.currentIndex = i - 1;
            }
        }
        Button {
            enabled: root.writable && grid.currentIndex >= 0 && grid.currentIndex < root.rows.count - 1
            text: qsTr("Down")
            visible: !root.rows.map

            onClicked: {
                const i = grid.currentIndex;
                root.rows.move(i, 1);
                grid.currentIndex = i + 1;
            }
        }
        Item {
            Layout.fillWidth: true
        }
    }
}
