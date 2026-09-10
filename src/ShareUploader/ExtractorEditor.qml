pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    required property TargetDraft draft
    readonly property string kind: {
        draft.document;
        return draft.stringValue(path.concat("type"));
    }
    property bool optional: false
    property bool errorMessage: false
    required property list<string> path
    required property string title
    readonly property var types: optional ? ["", "text_url", "json_pointer", "regex", "header", "redirect_url", "xml_path"] : ["text_url", "json_pointer", "regex", "header", "redirect_url", "xml_path"]

    Accessible.name: title
    Accessible.role: Accessible.Grouping
    Layout.fillWidth: true
    spacing: 8

    Label {
        Layout.fillWidth: true
        font.bold: true
        text: root.title
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
    }

    GridLayout {
        Layout.fillWidth: true
        columnSpacing: 12
        columns: 2
        rowSpacing: 8

        Label {
            text: qsTr("Extract using")
            textFormat: Text.PlainText
        }
        ComboBox {
            Layout.fillWidth: true
            currentIndex: root.types.indexOf(root.kind)
            model: root.optional ? [qsTr("Not configured"), (root.errorMessage ? qsTr("Plain text") : qsTr("Plain URL")), qsTr("JSON pointer"), qsTr("Regular expression"), qsTr("Response header"), qsTr("Redirect URL"), qsTr("XML path")] : [(root.errorMessage ? qsTr("Plain text") : qsTr("Plain URL")), qsTr("JSON pointer"), qsTr("Regular expression"), qsTr("Response header"), qsTr("Redirect URL"), qsTr("XML path")]

            onActivated: root.draft.setExtractorType(root.path, root.types[currentIndex])
        }
        Label {
            text: qsTr("JSON pointer")
            textFormat: Text.PlainText
            visible: root.kind === "json_pointer"
        }
        FieldLine {
            draft: root.draft
            path: root.path.concat("pointer")
            placeholderText: qsTr("/files/0/url; empty selects the whole response")
            visible: root.kind === "json_pointer"
        }
        Label {
            text: qsTr("Pattern")
            textFormat: Text.PlainText
            visible: root.kind === "regex"
        }
        FieldLine {
            draft: root.draft
            path: root.path.concat("pattern")
            visible: root.kind === "regex"
        }
        Label {
            text: qsTr("Capture group (0 = whole match)")
            textFormat: Text.PlainText
            visible: root.kind === "regex"
        }
        SpinBox {
            editable: true
            from: 0
            live: true
            to: 100000
            value: {
                root.draft.document;
                const v = root.draft.value(root.path.concat("group"));
                return v === undefined ? 1 : v;
            }
            visible: root.kind === "regex"

            onValueModified: root.draft.setValue(root.path.concat("group"), value)
        }
        Label {
            text: qsTr("Header name")
            textFormat: Text.PlainText
            visible: root.kind === "header"
        }
        FieldLine {
            draft: root.draft
            path: root.path.concat("name")
            visible: root.kind === "header"
        }
        Label {
            text: qsTr("XML path")
            textFormat: Text.PlainText
            visible: root.kind === "xml_path"
        }
        FieldLine {
            draft: root.draft
            path: root.path.concat("path")
            placeholderText: "/files/file[2]/url"
            visible: root.kind === "xml_path"
        }
    }
}
