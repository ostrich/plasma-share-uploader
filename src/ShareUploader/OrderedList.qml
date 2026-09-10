pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    required property string addText
    property alias currentIndex: list.currentIndex
    required property var newValue
    required property string removeText
    required property JsonRowsModel rows

    Layout.fillWidth: true

    Frame {
        id: listFrame

        Layout.fillWidth: true
        Layout.preferredHeight: 100
        padding: 1

        Rectangle {
            anchors.fill: parent
            color: listFrame.palette.base
            z: -1
        }
        ScrollView {
            anchors.fill: parent
            clip: true

            ListView {
                id: list

                currentIndex: count ? 0 : -1
                model: root.rows

                delegate: ItemDelegate {
                    required property int index
                    required property string rowLabel

                    highlighted: ListView.isCurrentItem
                    objectName: root.objectName + ".row." + index
                    text: rowLabel
                    width: ListView.view.width

                    onClicked: list.currentIndex = index
                }
            }
        }
    }
    RowLayout {
        Button {
            objectName: root.objectName + ".add"
            text: root.addText

            onClicked: {
                root.rows.append(root.newValue);
                list.currentIndex = root.rows.count - 1;
            }
        }
        Button {
            enabled: list.currentIndex >= 0
            text: root.removeText

            onClicked: {
                const i = list.currentIndex;
                root.rows.remove(i);
                list.currentIndex = Math.min(i, root.rows.count - 1);
            }
        }
        Button {
            enabled: list.currentIndex > 0
            text: qsTr("Up")

            onClicked: {
                const i = list.currentIndex;
                root.rows.move(i, -1);
                list.currentIndex = i - 1;
            }
        }
        Button {
            enabled: list.currentIndex >= 0 && list.currentIndex < root.rows.count - 1
            text: qsTr("Down")

            onClicked: {
                const i = list.currentIndex;
                root.rows.move(i, 1);
                list.currentIndex = i + 1;
            }
        }
        Item {
            Layout.fillWidth: true
        }
    }
}
