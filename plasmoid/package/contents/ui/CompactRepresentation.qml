import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid

import "Rates.js" as Rates

Item {
    id: compact

    required property PlasmoidItem main

    readonly property bool vertical: Plasmoid.formFactor === PlasmaCore.Types.Vertical

    readonly property string mode: Plasmoid.configuration.compactDisplay
    readonly property bool showIcon: mode !== "totals"
    /// "topProcess" reads out the busiest application; the other two modes
    /// report the machine's overall throughput, with "both" keeping the icon
    /// as a hint at who is responsible.
    readonly property bool showAppRates: mode === "topProcess"

    readonly property var app: main.topApp
    readonly property real rx: showAppRates ? (app ? app.rx : 0) : main.totalRx
    readonly property real tx: showAppRates ? (app ? app.tx : 0) : main.totalTx

    readonly property bool offline: main.daemonStatus !== "ok"

    Layout.minimumWidth: vertical ? 0 : layout.implicitWidth
    Layout.preferredWidth: vertical ? 0 : layout.implicitWidth
    Layout.minimumHeight: vertical ? layout.implicitHeight : 0
    Layout.preferredHeight: vertical ? layout.implicitHeight : 0

    GridLayout {
        id: layout

        anchors.centerIn: parent
        // Icon beside the readings on a horizontal panel, above them on a
        // vertical one.
        flow: compact.vertical ? GridLayout.TopToBottom : GridLayout.LeftToRight
        rowSpacing: Kirigami.Units.smallSpacing
        columnSpacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            Layout.alignment: Qt.AlignCenter
            Layout.preferredWidth: Kirigami.Units.iconSizes.small
            Layout.preferredHeight: Kirigami.Units.iconSizes.small
            visible: compact.showIcon

            source: {
                if (compact.offline) {
                    return "network-offline";
                }
                if (!compact.app) {
                    return "network-transmit-receive";
                }
                if (compact.app.icon.length > 0) {
                    return compact.app.icon;
                }
                return compact.app.exe.length > 0
                    ? compact.app.exe.split("/").pop()
                    : "application-x-executable";
            }
            fallback: "application-x-executable"
            opacity: compact.offline || !compact.app ? 0.5 : 1
        }

        ColumnLayout {
            Layout.alignment: Qt.AlignCenter
            spacing: 0

            PlasmaComponents.Label {
                Layout.alignment: Qt.AlignRight
                text: compact.offline ? "↑ —" : "↑ " + Rates.format(compact.tx, main.binaryUnits)
                font: Kirigami.Theme.smallFont
                textFormat: Text.PlainText
                opacity: compact.tx < 1 ? 0.5 : 1
            }

            PlasmaComponents.Label {
                Layout.alignment: Qt.AlignRight
                text: compact.offline ? "↓ —" : "↓ " + Rates.format(compact.rx, main.binaryUnits)
                font: Kirigami.Theme.smallFont
                textFormat: Text.PlainText
                opacity: compact.rx < 1 ? 0.5 : 1
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        // Left only: a right click has to reach the applet so Plasma can show
        // its own Configure/Remove menu.
        acceptedButtons: Qt.LeftButton
        onClicked: main.expanded = !main.expanded
    }
}
