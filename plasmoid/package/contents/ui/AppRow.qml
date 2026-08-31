import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import org.kde.plasma.plasmoid

import "Animation.js" as Animation
import "Rates.js" as Rates

/// One application in the expanded list. Clicking it reveals the individual
/// pids that were merged into the row.
Item {
    id: row

    readonly property bool animate: Plasmoid.configuration.animationsEnabled
    readonly property int hoverDuration:
        Animation.scaled(Kirigami.Units.shortDuration, Plasmoid.configuration.animationSpeed)
    readonly property int dimDuration:
        Animation.scaled(Kirigami.Units.longDuration, Plasmoid.configuration.animationSpeed)

    required property bool binaryUnits
    required property string name
    required property string icon
    required property string exe
    required property real rx
    required property real tx
    /// Still inside its grace period after going quiet. Dimmed rather than
    /// dropped, so a bursty application holds its place in the ranking.
    required property bool idle
    required property int pidCount
    /// See rowFor() in main.qml for why these two arrive as JSON text.
    required property string pidsJson
    required property string sparkJson

    readonly property string exeName: exe.length > 0 ? exe.split("/").pop() : ""

    readonly property var pids: pidsJson.length > 0 ? JSON.parse(pidsJson) : []
    readonly property var spark: sparkJson.length > 0 ? JSON.parse(sparkJson) : []

    property bool expanded: false

    /// A single-process row has nothing to disclose, so it is inert — unless it
    /// is already open, which happens when an expanded application's extra
    /// processes exit. Gating on the process count alone would strand the row
    /// open with no way to close it: the click would stop toggling while the
    /// breakdown stayed on screen.
    readonly property bool interactive: pidCount > 1 || expanded

    implicitHeight: layout.implicitHeight + Kirigami.Units.smallSpacing * 2

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        // Left only: a right click has to reach the applet so Plasma can show
        // its own Configure/Remove menu.
        acceptedButtons: Qt.LeftButton
        cursorShape: row.interactive ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: {
            if (row.interactive) {
                row.expanded = !row.expanded;
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: Kirigami.Units.cornerRadius
            color: Kirigami.Theme.highlightColor
            opacity: parent.containsMouse && row.interactive ? 0.15 : 0
            Behavior on opacity {
                enabled: row.animate
                NumberAnimation {
                    duration: row.hoverDuration
                }
            }
        }
    }

    ColumnLayout {
        id: layout

        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing
        spacing: Kirigami.Units.smallSpacing

        // Dimming lives here rather than on the delegate root, whose opacity
        // the ListView's add and remove transitions animate.
        opacity: row.idle ? 0.45 : 1
        Behavior on opacity {
            enabled: row.animate
            NumberAnimation {
                duration: row.dimDuration
                easing.type: Easing.InOutQuad
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                // plasma-nethogsd resolves Icon= from the .desktop file when it can; the
                // executable name is a decent guess for everything else.
                source: row.icon.length > 0 ? row.icon : row.exeName
                fallback: "application-x-executable"
            }

            PlasmaComponents.Label {
                Layout.fillWidth: true
                text: row.name
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }

            Sparkline {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 3
                Layout.preferredHeight: Kirigami.Units.gridUnit
                Layout.alignment: Qt.AlignVCenter
                visible: Plasmoid.configuration.showSparklines
                values: row.spark
            }

            RateLabel {
                down: false
                value: row.tx
                binaryUnits: row.binaryUnits
            }

            RateLabel {
                down: true
                value: row.rx
                binaryUnits: row.binaryUnits
            }
        }

        // Per-pid breakdown. Only a row that merged several processes can be
        // opened, but one that is already open keeps showing the breakdown as
        // its processes exit, down to the last one, rather than emptying out
        // from under the pointer.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
            spacing: 0
            visible: row.expanded

            Repeater {
                model: row.expanded ? row.pids : []

                RowLayout {
                    required property var modelData

                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    PlasmaComponents.Label {
                        Layout.fillWidth: true
                        text: i18nc("process name and its pid", "%1 (%2)",
                                    modelData.comm, modelData.pid)
                        font: Kirigami.Theme.smallFont
                        opacity: 0.7
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }

                    RateLabel {
                        down: false
                        value: modelData.tx
                        binaryUnits: row.binaryUnits
                        small: true
                    }

                    RateLabel {
                        down: true
                        value: modelData.rx
                        binaryUnits: row.binaryUnits
                        small: true
                    }
                }
            }
        }
    }
}
