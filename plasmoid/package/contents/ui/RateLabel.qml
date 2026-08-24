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

    /// The rendered figure, which chases `value` instead of jumping to it.
    /// Snapshots land once a second; without this the readings tick like a
    /// stopwatch while everything around them slides.
    property real displayValue: value

    Behavior on displayValue {
        NumberAnimation {
            duration: Kirigami.Units.longDuration
            easing.type: Easing.OutCubic
        }
    }

    Layout.preferredWidth: metrics.width
    horizontalAlignment: Text.AlignRight

    text: (down ? "↓ " : "↑ ") + Rates.format(displayValue, binaryUnits)
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
