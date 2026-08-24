import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents

import "Rates.js" as Rates

/// A single "↓ 3.1 MiB/s" reading. Fixed-width so the panel entry does not
/// twitch as the number changes.
PlasmaComponents.Label {
    id: label

    required property real value
    required property bool down
    required property bool binaryUnits
    property bool small: false

    readonly property real dimAt: 1 // bytes/s below which the reading is idle

    Layout.preferredWidth: metrics.width
    horizontalAlignment: Text.AlignRight

    text: (down ? "↓ " : "↑ ") + Rates.format(value, binaryUnits)
    font: small ? Kirigami.Theme.smallFont : Kirigami.Theme.defaultFont
    opacity: value < dimAt ? 0.4 : 1
    textFormat: Text.PlainText

    TextMetrics {
        id: metrics
        font: label.font
        // Widest realistic reading, so the column never resizes mid-transfer.
        text: "↓ 999.9 MiB/s"
    }
}
