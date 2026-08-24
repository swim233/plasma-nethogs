import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

Kirigami.FormLayout {
    id: page

    property alias cfg_refreshInterval: intervalBox.value
    property alias cfg_topCount: topCountBox.value
    property alias cfg_showTotals: totalsBox.checked
    property alias cfg_showSparklines: sparklineBox.checked
    property alias cfg_binaryUnits: binaryBox.checked
    property alias cfg_statePath: statePathField.text
    property string cfg_compactDisplay
    property var cfg_excludeProcesses

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
        Kirigami.FormData.label: " "
        text: i18n("Match this to the daemon's --interval; the widget polls twice as often.")
        font: Kirigami.Theme.smallFont
        opacity: 0.7
        wrapMode: Text.WordWrap
        Layout.maximumWidth: Kirigami.Units.gridUnit * 20
    }

    Item {
        Kirigami.FormData.isSection: true
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
        text: i18n("Total throughput header")
    }

    QQC2.CheckBox {
        id: sparklineBox
        text: i18n("Rate history graphs")
    }

    QQC2.CheckBox {
        id: binaryBox
        text: i18n("Binary units (KiB/s) instead of decimal (kB/s)")
    }

    Item {
        Kirigami.FormData.isSection: true
    }

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
        Kirigami.FormData.label: " "
        text: i18n("A TUN-mode proxy relays traffic through its own sockets, so every byte is counted twice — once for the application and once for the proxy. Excluding the proxy here restores the real figures.")
        font: Kirigami.Theme.smallFont
        opacity: 0.7
        wrapMode: Text.WordWrap
        Layout.maximumWidth: Kirigami.Units.gridUnit * 20
    }

    RowLayout {
        Kirigami.FormData.label: " "
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

    QQC2.TextField {
        id: statePathField

        Kirigami.FormData.label: i18n("Snapshot file:")
        Layout.preferredWidth: Kirigami.Units.gridUnit * 20
    }
}
