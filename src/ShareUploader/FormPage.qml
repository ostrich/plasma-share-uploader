pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts

PageScrollView {
    id: root

    default property alias fields: form.data

    GridLayout {
        id: form

        columnSpacing: 12
        columns: 2
        rowSpacing: 8
        width: root.availableWidth
    }
}
