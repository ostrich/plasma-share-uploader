pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    required property TargetDraft draft

    Label {
        Layout.fillWidth: true
        text: qsTr("Advanced JSON edits update the forms. Unknown fields are preserved. Save explicitly to apply changes.")
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
    }
    ScrollView {
        Layout.fillHeight: true
        Layout.fillWidth: true
        clip: true

        TextArea {
            id: editor

            font.family: "monospace"
            objectName: "targetJson"
            readOnly: !root.draft.editable
            text: root.draft.rawText
            wrapMode: TextEdit.NoWrap

            onTextChanged: if (activeFocus && text !== root.draft.rawText)
                root.draft.rawText = text
        }
    }
}
