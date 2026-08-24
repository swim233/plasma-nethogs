import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.ksvg as KSvg
import org.kde.plasma.components as PlasmaComponents
import org.kde.plasma.extras as PlasmaExtras
import org.kde.plasma.plasmoid

import "Rates.js" as Rates

/**
 * Deliberately built without PlasmaExtras.Representation, PlasmoidHeading or
 * PlasmaComponents.ScrollView, which would be the obvious choices here.
 *
 * All three swallow right clicks, so the containment never sees the press and
 * the widget gets no Configure/Remove menu. The failure is silent: everything
 * else about the widget works, and nothing is logged. See
 * autotests/tst_eventpropagation.qml, which probes each component and asserts
 * that the structure below lets a right click through.
 *
 * Anything that fills the applet area has to be checked against that test
 * before being used here. Item, ColumnLayout, ListView, Label, Kirigami.Icon,
 * KSvg.FrameSvgItem, PlaceholderMessage and an attached ScrollBar are known to
 * be safe.
 */
Item {
    id: full

    required property PlasmoidItem main

    readonly property bool online: main.daemonStatus === "ok"
    readonly property bool headerVisible:
        (Plasmoid.configuration.showTitle || Plasmoid.configuration.showTotals) && online

    Layout.minimumWidth: Kirigami.Units.gridUnit * 20
    Layout.minimumHeight: Kirigami.Units.gridUnit * 12
    Layout.preferredWidth: Kirigami.Units.gridUnit * 24
    Layout.preferredHeight: Kirigami.Units.gridUnit * 16

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            id: heading

            Layout.fillWidth: true
            implicitHeight: headingContent.implicitHeight + Kirigami.Units.smallSpacing * 2
            visible: full.headerVisible

            // Matches what PlasmoidHeading would have drawn.
            Kirigami.Theme.colorSet: Kirigami.Theme.Header
            Kirigami.Theme.inherit: false

            KSvg.FrameSvgItem {
                anchors.fill: parent
                imagePath: "widgets/plasmoidheading"
                prefix: "header"
            }

            RowLayout {
                id: headingContent

                anchors.fill: parent
                anchors.leftMargin: Kirigami.Units.smallSpacing
                anchors.rightMargin: Kirigami.Units.smallSpacing
                spacing: Kirigami.Units.smallSpacing

                PlasmaExtras.Heading {
                    Layout.fillWidth: true
                    level: 4
                    text: full.main.title
                    visible: Plasmoid.configuration.showTitle
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }

                // Keeps the readings hard right when the title is hidden.
                Item {
                    Layout.fillWidth: !Plasmoid.configuration.showTitle
                }

                RateLabel {
                    down: true
                    value: full.main.totalRx
                    binaryUnits: full.main.binaryUnits
                    visible: Plasmoid.configuration.showTotals
                }

                RateLabel {
                    down: false
                    value: full.main.totalTx
                    binaryUnits: full.main.binaryUnits
                    visible: Plasmoid.configuration.showTotals
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            PlasmaExtras.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.gridUnit * 4
                visible: !full.online

                iconName: "network-offline"
                text: full.main.daemonStatus === "stale"
                    ? i18n("The monitoring service stopped responding")
                    : i18n("The monitoring service is not running")
                explanation: i18n("Start it with:\nsudo systemctl enable --now pnmd")
            }

            PlasmaExtras.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.gridUnit * 4
                visible: full.online && list.count === 0

                iconName: "network-idle"
                text: i18n("No network activity")
            }

            ListView {
                id: list

                anchors.fill: parent
                anchors.margins: Kirigami.Units.smallSpacing
                visible: full.online && list.count > 0

                model: full.main.apps
                clip: true

                // Recycling a delegate skips the add and remove transitions it
                // would otherwise play, and loses the expanded state of a row.
                reuseItems: false

                PlasmaComponents.ScrollBar.vertical: PlasmaComponents.ScrollBar {}

                delegate: AppRow {
                    required property var model

                    width: list.width

                    binaryUnits: full.main.binaryUnits
                    name: model.name
                    icon: model.icon
                    exe: model.exe
                    rx: model.rx
                    tx: model.tx
                    idle: model.idle
                    pidCount: model.pidCount
                    pidsJson: model.pidsJson
                    sparkJson: model.sparkJson
                }

                // Every kind of list change gets its own transition. Ranking by
                // a rate that moves every second means rows are constantly
                // swapping places, appearing and expiring; without all four the
                // list reads as a flicker rather than as movement.

                // A newly seen application grows in from the left.
                add: Transition {
                    ParallelAnimation {
                        NumberAnimation {
                            property: "opacity"
                            from: 0
                            to: 1
                            duration: Kirigami.Units.longDuration
                            easing.type: Easing.OutCubic
                        }
                        NumberAnimation {
                            property: "x"
                            from: -Kirigami.Units.gridUnit * 2
                            to: 0
                            duration: Kirigami.Units.longDuration
                            easing.type: Easing.OutCubic
                        }
                    }
                }

                // An application whose grace period ran out fades away rather
                // than vanishing between two frames.
                remove: Transition {
                    ParallelAnimation {
                        NumberAnimation {
                            property: "opacity"
                            to: 0
                            duration: Kirigami.Units.longDuration
                            easing.type: Easing.InCubic
                        }
                        NumberAnimation {
                            property: "x"
                            to: Kirigami.Units.gridUnit * 2
                            duration: Kirigami.Units.longDuration
                            easing.type: Easing.InCubic
                        }
                    }
                }

                // The row that actually changed rank, e.g. third place
                // overtaking first.
                move: Transition {
                    NumberAnimation {
                        properties: "y"
                        duration: Kirigami.Units.longDuration
                        easing.type: Easing.InOutCubic
                    }
                }

                // Everything shoved aside by an add, a remove or a move.
                displaced: Transition {
                    NumberAnimation {
                        properties: "x,y"
                        duration: Kirigami.Units.longDuration
                        easing.type: Easing.InOutCubic
                    }
                }
            }
        }
    }
}
