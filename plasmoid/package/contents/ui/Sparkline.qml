import QtQuick
import QtQuick.Shapes

import org.kde.kirigami as Kirigami

/// A filled rate trace, scaled to the tallest sample in its own window. It says
/// "busier than a moment ago", not "how many bytes" — the number next to it
/// already answers that.
Item {
    id: root

    property var values: []
    property color lineColor: Kirigami.Theme.highlightColor

    // Bound to a model role that is undefined until the delegate's row lands.
    // Testing `length` rather than the value itself, because an unset var
    // property can still be truthy here.
    readonly property int count: values && values.length !== undefined ? values.length : 0

    readonly property real peak: {
        let max = 0;
        for (let i = 0; i < count; ++i) {
            if (values[i] > max) {
                max = values[i];
            }
        }
        return max;
    }

    /// The trace is scaled to the tallest sample in its window, so the whole
    /// curve jumps whenever that sample enters or leaves. Easing the scale
    /// turns those jumps into a stretch.
    property real smoothPeak: peak

    Behavior on smoothPeak {
        NumberAnimation {
            duration: Kirigami.Units.longDuration
            easing.type: Easing.OutCubic
        }
    }

    readonly property bool drawable: count >= 2 && smoothPeak > 0

    readonly property var linePoints: {
        if (!drawable) {
            return [];
        }
        const points = [];
        const step = width / (count - 1);
        for (let i = 0; i < count; ++i) {
            const scaled = Math.min(1, values[i] / smoothPeak);
            points.push(Qt.point(i * step, height - scaled * height));
        }
        return points;
    }

    /// Same trace, closed along the baseline so the area below it can be shaded.
    readonly property var areaPoints: {
        if (!drawable) {
            return [];
        }
        return linePoints.concat([Qt.point(width, height), Qt.point(0, height)]);
    }


    Shape {
        anchors.fill: parent
        visible: root.drawable
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeWidth: -1 // fill only; the outline is drawn separately
            fillColor: Qt.rgba(root.lineColor.r, root.lineColor.g, root.lineColor.b, 0.25)

            PathPolyline {
                path: root.areaPoints
            }
        }

        ShapePath {
            strokeColor: root.lineColor
            strokeWidth: 1
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            PathPolyline {
                path: root.linePoints
            }
        }
    }
}
