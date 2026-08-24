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

    // The figure is not eased toward its new value. Tweening it would set a
    // number running for as long as a row takes to slide, so every rank change
    // would come with its readings racing — the movement is meant to be in the
    // list, not in the measurements.
    text: (down ? "↓ " : "↑ ") + Rates.format(value, binaryUnits)
    font: small ? Kirigami.Theme.smallFont : Kirigami.Theme.defaultFont
    opacity: value < dimAt ? 0.4 : 1
    textFormat: Text.PlainText

    Behavior on opacity {
        NumberAnimation {
            duration: Kirigami.Units.longDuration
            easing.type: Easing.InOutQuad
        }
    }

    TextMetrics {
        id: metrics
        font: label.font
        // Widest realistic reading, so the column never resizes mid-transfer.
        text: "↓ 999.9 MiB/s"
    }
}
