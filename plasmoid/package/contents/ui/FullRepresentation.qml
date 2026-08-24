import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import org.kde.plasma.extras as PlasmaExtras
import org.kde.plasma.plasmoid

import "Rates.js" as Rates

PlasmaExtras.Representation {
    id: full

    required property PlasmoidItem main

    Layout.minimumWidth: Kirigami.Units.gridUnit * 20
    Layout.minimumHeight: Kirigami.Units.gridUnit * 12
    Layout.preferredWidth: Kirigami.Units.gridUnit * 24
    Layout.preferredHeight: Kirigami.Units.gridUnit * 16

    header: PlasmaExtras.PlasmoidHeading {
        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing

            PlasmaComponents.Label {
                Layout.fillWidth: true
                text: i18n("Total")
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }

            RateLabel {
                down: true
                value: full.main.totalRx
                binaryUnits: full.main.binaryUnits
            }

            RateLabel {
                down: false
                value: full.main.totalTx
                binaryUnits: full.main.binaryUnits
            }
        }

        visible: Plasmoid.configuration.showTotals && full.main.daemonStatus === "ok"
    }

    contentItem: Item {
        PlasmaExtras.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: full.main.daemonStatus !== "ok"

            iconName: "network-offline"
            text: full.main.daemonStatus === "stale"
                ? i18n("The monitoring service stopped responding")
                : i18n("The monitoring service is not running")
            explanation: i18n("Start it with:\nsudo systemctl enable --now pnmd")
        }

        PlasmaExtras.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: full.main.daemonStatus === "ok" && list.count === 0

            iconName: "network-idle"
            text: i18n("No network activity")
        }

        PlasmaComponents.ScrollView {
            anchors.fill: parent
            visible: full.main.daemonStatus === "ok" && list.count > 0

            ListView {
                id: list

                model: full.main.apps
                reuseItems: true
                clip: true

                delegate: AppRow {
                    required property var model

                    width: list.width

                    binaryUnits: full.main.binaryUnits
                    name: model.name
                    icon: model.icon
                    exe: model.exe
                    rx: model.rx
                    tx: model.tx
                    pidCount: model.pidCount
                    pidsJson: model.pidsJson
                    sparkJson: model.sparkJson
                }

                // Rows are ranked by rate, so they change places constantly.
                // Sliding them keeps that legible instead of making the list
                // flicker once a second.
                displaced: Transition {
                    NumberAnimation {
                        properties: "y"
                        duration: Kirigami.Units.longDuration
                        easing.type: Easing.OutCubic
                    }
                }

                add: Transition {
                    NumberAnimation {
                        property: "opacity"
                        from: 0
                        to: 1
                        duration: Kirigami.Units.shortDuration
                    }
                }
            }
        }
    }
}
