pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

FormPage {
    id: root

    readonly property string bodyType: {
        draft.document;
        return draft.stringValue(["request", "body", "type"]);
    }
    required property TargetDraft draft

    enabled: draft.editable && draft.hasObject

    Label {
        text: qsTr("Upload endpoint")
        textFormat: Text.PlainText
    }
    FieldLine {
        draft: root.draft
        path: ["request", "url"]
    }
    Label {
        text: qsTr("Method")
        textFormat: Text.PlainText
    }
    ComboBox {
        currentIndex: {
            root.draft.document;
            return model.indexOf(root.draft.stringValue(["request", "method"]));
        }
        model: ["POST", "PUT"]

        onActivated: root.draft.setValue(["request", "method"], currentText)
    }
    Label {
        text: qsTr("Body format")
        textFormat: Text.PlainText
    }
    ComboBox {
        id: format

        readonly property list<string> types: ["multipart", "raw", "form_urlencoded", "json"]

        currentIndex: types.indexOf(root.bodyType)
        model: [qsTr("Multipart form"), qsTr("Raw file"), qsTr("URL encoded form"), qsTr("JSON")]

        onActivated: root.draft.setBodyType(types[currentIndex])
    }
    Label {
        text: qsTr("File field name")
        textFormat: Text.PlainText
        visible: root.bodyType === "multipart"
    }
    FieldLine {
        draft: root.draft
        path: ["request", "body", "fileField"]
        visible: root.bodyType === "multipart"
    }
    Label {
        Layout.alignment: Qt.AlignTop
        text: qsTr("Multipart fields")
        textFormat: Text.PlainText
        visible: root.bodyType === "multipart"
    }
    DraftTable {
        draft: root.draft
        map: true
        path: ["request", "body", "fields"]
        visible: root.bodyType === "multipart"
    }
    Label {
        text: qsTr("Raw content type")
        textFormat: Text.PlainText
        visible: root.bodyType === "raw"
    }
    FieldLine {
        draft: root.draft
        path: ["request", "body", "contentType"]
        placeholderText: "application/octet-stream"
        visible: root.bodyType === "raw"
    }
    Label {
        Layout.alignment: Qt.AlignTop
        text: qsTr("Form fields")
        textFormat: Text.PlainText
        visible: root.bodyType === "form_urlencoded"
    }
    DraftTable {
        draft: root.draft
        map: true
        path: ["request", "body", "fields"]
        visible: root.bodyType === "form_urlencoded"
    }
    Label {
        Layout.alignment: Qt.AlignTop
        text: qsTr("JSON body\n(any JSON value)")
        textFormat: Text.PlainText
        visible: root.bodyType === "json"
    }
    ScrollView {
        Layout.fillWidth: true
        Layout.preferredHeight: 150
        visible: root.bodyType === "json"

        TextArea {
            id: jsonBody

            font.family: "monospace"
            objectName: "jsonBody"
            text: root.draft.jsonBody
            wrapMode: TextEdit.NoWrap

            onTextChanged: if (activeFocus && text !== root.draft.jsonBody)
                root.draft.jsonBody = text
        }
    }
    Label {
        Layout.alignment: Qt.AlignTop
        text: qsTr("Headers")
        textFormat: Text.PlainText
    }
    DraftTable {
        draft: root.draft
        map: true
        path: ["request", "headers"]
    }
    Label {
        Layout.alignment: Qt.AlignTop
        text: qsTr("Query parameters")
        textFormat: Text.PlainText
    }
    DraftTable {
        draft: root.draft
        map: true
        path: ["request", "query"]
    }
    Label {
        Layout.columnSpan: 2
        Layout.fillWidth: true
        text: qsTr("Values support ${FILENAME}, ${ENV:NAME}, and ${WALLET:credential-name}. Multipart uploads require POST. Use Credentials to store secrets outside the JSON.")
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
    }
}
