pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

TextField {
    id: root

    required property TargetDraft draft
    property string fallback: ""
    required property list<string> path

    Layout.fillWidth: true
    Layout.minimumWidth: 80
    objectName: path.join(".")
    text: {
        root.draft.document;
        return root.draft.stringValue(root.path, root.fallback);
    }

    onTextEdited: root.draft.setValue(root.path, text)
}
