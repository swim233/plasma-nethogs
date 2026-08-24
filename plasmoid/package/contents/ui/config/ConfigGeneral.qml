import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kcmutils as KCM
import org.kde.kirigami as Kirigami

KCM.SimpleKCM {
    id: page

    property alias cfg_widgetTitle: titleField.text
    property alias cfg_showTitle: titleBox.checked
    property alias cfg_refreshInterval: intervalBox.value
    property alias cfg_lingerSeconds: lingerBox.value
    property alias cfg_topCount: topCountBox.value
    property alias cfg_showTotals: totalsBox.checked
    property alias cfg_showSparklines: sparklineBox.checked
    property alias cfg_binaryUnits: binaryBox.checked
    property alias cfg_statePath: statePathField.text
    property string cfg_compactDisplay
    property var cfg_excludeProcesses

    // Plasma assigns the kcfg defaults to these when the page opens; they exist
    // only so it has somewhere to put them. Without them the Reset button
    // cannot tell whether a value has been changed, and every open logs a
    // warning per setting.
    property string cfg_widgetTitleDefault
    property bool cfg_showTitleDefault
    property int cfg_refreshIntervalDefault
    property int cfg_lingerSecondsDefault
    property int cfg_topCountDefault
    property bool cfg_showTotalsDefault
    property bool cfg_showSparklinesDefault
    property bool cfg_binaryUnitsDefault
    property string cfg_statePathDefault
    property string cfg_compactDisplayDefault
    property var cfg_excludeProcessesDefault

    Kirigami.FormLayout {
        // ── Appearance ───────────────────────────────────────────────────────

        QQC2.CheckBox {
            id: titleBox
            Kirigami.FormData.label: i18n("Title:")
            text: i18n("Show a title")
        }

        QQC2.TextField {
            id: titleField

            Layout.preferredWidth: Kirigami.Units.gridUnit * 20
            enabled: titleBox.checked
            placeholderText: i18n("Network Top")
        }

        QQC2.SpinBox {
            id: topCountBox

            Kirigami.FormData.label: i18n("Applications to list:")
            from: 1
            to: 20
            editable: true
        }

        QQC2.CheckBox {
            id: totalsBox
            Kirigami.FormData.label: i18n("Show:")
            text: i18n("Total throughput")
        }

        QQC2.CheckBox {
            id: sparklineBox
            text: i18n("Rate history graphs")
        }

        QQC2.CheckBox {
            id: binaryBox
            text: i18n("Binary units (KiB/s) instead of decimal (kB/s)")
        }

        QQC2.ComboBox {
            id: compactBox

            Kirigami.FormData.label: i18n("Panel entry shows:")
            textRole: "label"
            valueRole: "value"

            model: [
                { label: i18n("Busiest application"), value: "topProcess" },
                { label: i18n("Total throughput"), value: "totals" },
                { label: i18n("Busiest application's icon with total throughput"), value: "both" }
            ]

            Component.onCompleted: currentIndex = indexOfValue(page.cfg_compactDisplay)
            onActivated: page.cfg_compactDisplay = currentValue
        }

        Item {
            Kirigami.FormData.isSection: true
        }

        // ── Timing ───────────────────────────────────────────────────────────

        QQC2.SpinBox {
            id: intervalBox

            Kirigami.FormData.label: i18n("Refresh interval:")
            from: 250
            to: 10000
            stepSize: 250
            editable: true

            textFromValue: (value, locale) => i18n("%1 ms", value)
            valueFromText: text => parseInt(text.replace(/[^0-9]/g, ""), 10)
        }

        QQC2.Label {
            text: i18n("Match this to the interval the daemon was started with.")
            font: Kirigami.Theme.smallFont
            opacity: 0.7
            wrapMode: Text.WordWrap
            Layout.maximumWidth: Kirigami.Units.gridUnit * 20
        }

        QQC2.SpinBox {
            id: lingerBox

            Kirigami.FormData.label: i18n("Keep idle applications for:")
            from: 0
            to: 300
            stepSize: 5
            editable: true

            textFromValue: (value, locale) => value === 0
                ? i18n("Remove immediately")
                : i18np("%1 second", "%1 seconds", value)
            valueFromText: text => parseInt(text.replace(/[^0-9]/g, ""), 10) || 0
        }

        QQC2.Label {
            text: i18n("An application that stops transferring stays in the list, dimmed, for this long before fading out. Without it, anything bursty flickers in and out once a second.")
            font: Kirigami.Theme.smallFont
            opacity: 0.7
            wrapMode: Text.WordWrap
            Layout.maximumWidth: Kirigami.Units.gridUnit * 20
        }

        Item {
            Kirigami.FormData.isSection: true
        }

        // ── Exclusions ───────────────────────────────────────────────────────

        QQC2.TextField {
            id: excludeField

            Kirigami.FormData.label: i18n("Exclude processes:")
            Layout.preferredWidth: Kirigami.Units.gridUnit * 20
            placeholderText: i18n("Comma-separated process names")

            text: (page.cfg_excludeProcesses || []).join(", ")
            onTextChanged: page.cfg_excludeProcesses =
                text.split(",").map(entry => entry.trim()).filter(entry => entry.length > 0)
        }

        QQC2.Label {
            text: i18n("A TUN-mode proxy relays traffic through its own sockets, so every byte is counted twice — once for the application and once for the proxy. Excluding the proxy here restores the real figures.")
            font: Kirigami.Theme.smallFont
            opacity: 0.7
            wrapMode: Text.WordWrap
            Layout.maximumWidth: Kirigami.Units.gridUnit * 20
        }

        Flow {
            Layout.preferredWidth: Kirigami.Units.gridUnit * 20
            spacing: Kirigami.Units.smallSpacing

            Repeater {
                model: ["mihomo", "clash", "sing-box", "v2ray", "xray", "tailscaled"]

                QQC2.Button {
                    required property string modelData

                    text: modelData
                    flat: true
                    enabled: (page.cfg_excludeProcesses || []).indexOf(modelData) === -1
                    onClicked: excludeField.text =
                        (excludeField.text.trim().length > 0 ? excludeField.text.trim() + ", " : "") + modelData
                }
            }
        }

        Item {
            Kirigami.FormData.isSection: true
        }

        // ── Advanced ─────────────────────────────────────────────────────────

        QQC2.TextField {
            id: statePathField

            Kirigami.FormData.label: i18n("Snapshot file:")
            Layout.preferredWidth: Kirigami.Units.gridUnit * 20
        }
    }
}
