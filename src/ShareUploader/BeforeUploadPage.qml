pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

FormPage {
    id: root

    required property TargetDraft draft
    readonly property list<string> rulePath: ["preUpload", String(rules.currentIndex)]

    enabled: draft.editable && draft.hasObject

    Label {
        Layout.columnSpan: 2
        Layout.fillWidth: true
        text: qsTr("The first matching MIME rule runs. Commands use separate arguments, without a shell. Originals remain unchanged.")
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
    }
    OrderedList {
        id: rules

        Layout.columnSpan: 2
        addText: qsTr("Add rule")
        newValue: ({
                mime: ["image/jpeg", "image/tiff"],
                fileHandling: "inplace_copy",
                commands: [
                    {
                        argv: ["exiv2", "rm", "${FILE}"]
                    }
                ]
            })
        objectName: "preRules"
        removeText: qsTr("Remove rule")

        rows: JsonRowsModel {
            draft: root.draft
            path: ["preUpload"]
        }
    }
    Label {
        Layout.alignment: Qt.AlignTop
        text: qsTr("MIME patterns")
        textFormat: Text.PlainText
    }
    DraftTable {
        draft: root.draft
        enabled: rules.currentIndex >= 0
        path: root.rulePath.concat("mime")
    }
    Label {
        text: qsTr("File handling")
        textFormat: Text.PlainText
    }
    ComboBox {
        Layout.fillWidth: true
        currentIndex: {
            root.draft.document;
            return ["inplace_copy", "output_file"].indexOf(root.draft.stringValue(root.rulePath.concat("fileHandling")));
        }
        enabled: rules.currentIndex >= 0
        model: [qsTr("Modify a temporary copy"), qsTr("Write a separate output file")]

        onActivated: root.draft.setValue(root.rulePath.concat("fileHandling"), currentIndex === 0 ? "inplace_copy" : "output_file")
    }
    Label {
        text: qsTr("Timeout per command (ms)")
        textFormat: Text.PlainText
    }
    SpinBox {
        editable: true
        enabled: rules.currentIndex >= 0
        from: 1
        live: true
        to: 2147483647
        value: {
            root.draft.document;
            const v = root.draft.value(root.rulePath.concat("timeoutMs"));
            return v === undefined ? 30000 : v;
        }

        onValueModified: root.draft.setValue(root.rulePath.concat("timeoutMs"), value)
    }
    Label {
        Layout.alignment: Qt.AlignTop
        text: qsTr("Commands, in order")
        textFormat: Text.PlainText
    }
    OrderedList {
        id: commands

        addText: qsTr("Add command")
        enabled: rules.currentIndex >= 0
        newValue: ({
                argv: ["program", "${FILE}"]
            })
        objectName: "preCommands"
        removeText: qsTr("Remove command")

        rows: JsonRowsModel {
            draft: root.draft
            path: root.rulePath.concat("commands")
        }
    }
    Label {
        Layout.alignment: Qt.AlignTop
        text: qsTr("Program, then each argument")
        textFormat: Text.PlainText
    }
    DraftTable {
        draft: root.draft
        enabled: rules.currentIndex >= 0 && commands.currentIndex >= 0
        path: root.rulePath.concat(["commands", String(commands.currentIndex), "argv"])
    }
    Label {
        Layout.columnSpan: 2
        Layout.fillWidth: true
        text: qsTr("Use ${FILE} for the input; output-file rules also require ${OUT_FILE}.")
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
    }
}
