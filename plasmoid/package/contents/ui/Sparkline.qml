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

    readonly property bool drawable: count >= 2 && peak > 0

    readonly property var linePoints: {
        if (!drawable) {
            return [];
        }
        const points = [];
        const step = width / (count - 1);
        for (let i = 0; i < count; ++i) {
            points.push(Qt.point(i * step, height - (values[i] / peak) * height));
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
