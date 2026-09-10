pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

FormPage {
    id: root

    required property TargetDraft draft

    enabled: draft.editable && draft.hasObject

    Label {
        text: qsTr("Name")
        textFormat: Text.PlainText
    }
    FieldLine {
        draft: root.draft
        path: ["displayName"]
    }
    Label {
        text: qsTr("Description")
        textFormat: Text.PlainText
    }
    FieldLine {
        draft: root.draft
        path: ["description"]
    }
    Label {
        text: qsTr("Icon name or URL")
        textFormat: Text.PlainText
    }
    FieldLine {
        draft: root.draft
        path: ["icon"]
    }
    Label {
        text: qsTr("Unique ID")
        textFormat: Text.PlainText
    }
    FieldLine {
        draft: root.draft
        path: ["id"]
        placeholderText: qsTr("Lowercase letters, digits, underscores and dashes")
    }
    Label {
        Layout.alignment: Qt.AlignTop
        text: qsTr("Accepted MIME types")
        textFormat: Text.PlainText
    }
    DraftTable {
        draft: root.draft
        path: ["accept", "mimeTypes"]
    }
    Label {
        Layout.columnSpan: 2
        Layout.fillWidth: true
        text: qsTr("For example: image/* or image/png. A file must match any listed MIME type and any listed extension. Empty lists allow all; every selected file must qualify.")
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
    }
    Label {
        Layout.alignment: Qt.AlignTop
        text: qsTr("File extensions")
        textFormat: Text.PlainText
    }
    DraftTable {
        draft: root.draft
        path: ["accept", "extensions"]
    }
}
