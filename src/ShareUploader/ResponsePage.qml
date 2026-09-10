pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts

PageScrollView {
    id: root

    required property TargetDraft draft

    enabled: draft.editable && draft.hasObject

    ColumnLayout {
        spacing: 24
        width: root.availableWidth

        ExtractorEditor {
            draft: root.draft
            path: ["response", "url"]
            title: qsTr("Shared URL")
        }
        ExtractorEditor {
            draft: root.draft
            optional: true
            path: ["response", "thumbnail"]
            title: qsTr("Thumbnail URL")
        }
        ExtractorEditor {
            draft: root.draft
            optional: true
            path: ["response", "deletion"]
            title: qsTr("Deletion URL")
        }
        ExtractorEditor {
            draft: root.draft
            optional: true
            path: ["response", "error"]
            errorMessage: true
            title: qsTr("Error message")
        }
    }
}
