pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

TextField {
    id: root

    required property TargetDraft draft
    property string fallback: ""
    required property list<string> path
    readonly property var fieldValue: path.reduce((value, key) => value == null ? undefined : value[key], draft.document)

    Layout.fillWidth: true
    Layout.minimumWidth: 80
    objectName: path.join(".")
    text: typeof fieldValue === "string" ? fieldValue : fallback

    onTextEdited: root.draft.setValue(root.path, text)
}
