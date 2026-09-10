pragma ComponentBehavior: Bound
import QtQuick

StringTable {
    id: root

    required property TargetDraft draft
    property bool map: false
    required property list<string> path

    objectName: path.join(".")
    writable: draft.editable && draft.hasObject

    rows: JsonRowsModel {
        draft: root.draft
        map: root.map
        path: root.path
    }
}
