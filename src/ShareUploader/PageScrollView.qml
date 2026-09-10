pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

ScrollView {
    clip: true
    contentWidth: availableWidth
    padding: 10
    topPadding: padding
    bottomPadding: padding
    leftPadding: padding + (mirrored ? effectiveScrollBarWidth : 0)
    rightPadding: padding + (mirrored ? 0 : effectiveScrollBarWidth)

    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
}
