pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

FormPage {
    id: root

    required property CredentialController controller
    required property TargetDraft draft
    readonly property bool writable: draft.editable && draft.hasObject && !draft.formErrors && !controller.busy

    function resetInputs() {
        secret.clear();
        username.clear();
        key.text = controller.suggestedKey;
    }

    Component.onCompleted: resetInputs()

    Connections {
        function onClearSecret() {
            secret.clear();
        }
        function onResetRequested() {
            root.resetInputs();
        }

        target: root.controller
    }
    Label {
        Layout.columnSpan: 2
        Layout.fillWidth: true
        text: qsTr("Anonymous targets need no credentials. Secrets are stored in KWallet; JSON contains ${WALLET:name} references. Environment references remain available in Request. Credentials are shared by name, including across duplicated targets.")
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
    }
    Label {
        Layout.columnSpan: 2
        Layout.fillWidth: true
        text: qsTr("Wallet references in draft: %1").arg(root.controller.references.length ? root.controller.references.join(", ") : qsTr("none"))
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
    }
    Label {
        text: qsTr("Authentication")
        textFormat: Text.PlainText
    }
    ComboBox {
        id: mode

        Layout.fillWidth: true
        enabled: root.writable
        model: [qsTr("API key"), qsTr("Bearer token"), qsTr("Basic authentication")]

        onActivated: {
            if (currentIndex > 0) {
                location.currentIndex = 0;
                field.text = "Authorization";
            }
        }
    }
    Label {
        text: qsTr("Place in")
        textFormat: Text.PlainText
    }
    ComboBox {
        id: location

        Layout.fillWidth: true
        enabled: root.writable && mode.currentIndex === 0
        model: [qsTr("Header"), qsTr("Query parameter"), qsTr("Multipart field"), qsTr("URL encoded form field"), qsTr("JSON object field")]
    }
    Label {
        text: qsTr("Field name")
        textFormat: Text.PlainText
    }
    TextField {
        id: field

        Layout.fillWidth: true
        enabled: root.writable && mode.currentIndex === 0
        text: "Authorization"
    }
    Label {
        text: qsTr("Wallet credential name")
        textFormat: Text.PlainText
    }
    TextField {
        id: key

        Layout.fillWidth: true
        enabled: root.writable
    }
    Label {
        text: qsTr("Username")
        textFormat: Text.PlainText
        visible: mode.currentIndex === 2
    }
    TextField {
        id: username

        Layout.fillWidth: true
        enabled: root.writable
        visible: mode.currentIndex === 2
    }
    Label {
        text: qsTr("Secret / password")
        textFormat: Text.PlainText
    }
    TextField {
        id: secret

        Layout.fillWidth: true
        echoMode: TextInput.Password
        enabled: root.writable
        objectName: "credentialSecret"
    }
    RowLayout {
        Layout.columnSpan: 2
        enabled: root.writable

        Button {
            text: qsTr("Store secret and use")

            onClicked: root.controller.bind(mode.currentIndex, location.currentIndex, field.text, key.text, secret.text, username.text, true)
        }
        Button {
            text: qsTr("Use existing credential")

            onClicked: root.controller.bind(mode.currentIndex, location.currentIndex, field.text, key.text, "", "", false)
        }
    }
    Button {
        Layout.columnSpan: 2
        enabled: root.writable && field.text.length > 0
        text: qsTr("Remove this request field")

        onClicked: root.controller.removeBinding(mode.currentIndex, location.currentIndex, field.text)
    }
    Button {
        Layout.columnSpan: 2
        enabled: root.writable
        text: qsTr("Forget stored credential…")

        onClicked: {
            forget.credentialName = key.text;
            forget.open();
        }
    }
    Button {
        Layout.columnSpan: 2
        enabled: !root.controller.busy
        text: qsTr("Check credentials")

        onClicked: root.controller.check()
    }
    Label {
        Layout.columnSpan: 2
        Layout.fillWidth: true
        text: root.controller.message
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
    }
    Dialog {
        id: forget

        property string credentialName

        anchors.centerIn: Overlay.overlay
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        title: qsTr("Forget credential")
        width: Math.min(500, Overlay.overlay.width - 40)

        onAccepted: root.controller.remove(credentialName)

        Label {
            text: qsTr("Remove '%1' from KWallet? Other targets referencing this name will need a replacement.").arg(forget.credentialName)
            textFormat: Text.PlainText
            width: parent.width
            wrapMode: Text.Wrap
        }
    }
}
