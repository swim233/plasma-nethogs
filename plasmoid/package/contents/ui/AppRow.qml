import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import org.kde.plasma.plasmoid

import "Rates.js" as Rates

/// One application in the expanded list. Clicking it reveals the individual
/// pids that were merged into the row.
Item {
    id: row

    required property bool binaryUnits
    required property string name
    required property string icon
    required property string exe
    required property real rx
    required property real tx
    required property int pidCount
    /// See rowFor() in main.qml for why these two arrive as JSON text.
    required property string pidsJson
    required property string sparkJson

    readonly property string exeName: exe.length > 0 ? exe.split("/").pop() : ""

    readonly property var pids: pidsJson.length > 0 ? JSON.parse(pidsJson) : []
    readonly property var spark: sparkJson.length > 0 ? JSON.parse(sparkJson) : []

    property bool expanded: false

    implicitHeight: layout.implicitHeight + Kirigami.Units.smallSpacing * 2

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: row.pidCount > 1 ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: {
            if (row.pidCount > 1) {
                row.expanded = !row.expanded;
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: Kirigami.Units.cornerRadius
            color: Kirigami.Theme.highlightColor
            opacity: parent.containsMouse && row.pidCount > 1 ? 0.15 : 0
            Behavior on opacity {
                NumberAnimation {
                    duration: Kirigami.Units.shortDuration
                }
            }
        }
    }

    ColumnLayout {
        id: layout

        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                // pnmd resolves Icon= from the .desktop file when it can; the
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
                down: true
                value: row.rx
                binaryUnits: row.binaryUnits
            }

            RateLabel {
                down: false
                value: row.tx
                binaryUnits: row.binaryUnits
            }
        }

        // Per-pid breakdown. Only worth showing when the row actually merged
        // several processes.
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
                        down: true
                        value: modelData.rx
                        binaryUnits: row.binaryUnits
                        small: true
                    }

                    RateLabel {
                        down: false
                        value: modelData.tx
                        binaryUnits: row.binaryUnits
                        small: true
                    }
                }
            }
        }
    }
}
