import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtTest

import org.kde.kirigami as Kirigami
import org.kde.ksvg as KSvg
import org.kde.plasma.components as PlasmaComponents
import org.kde.plasma.extras as PlasmaExtras

/*
 * Plasma shows a widget's Configure/Remove menu when a right click reaches the
 * containment underneath it. Several QtQuick Controls accept the press instead
 * and the menu silently never appears — the widget keeps working, nothing is
 * logged, and there is no way to tell from the widget itself.
 *
 * PlasmaExtras.Representation, PlasmoidHeading and PlasmaComponents.ScrollView
 * are all in that group, and all three are the obvious components to build a
 * full representation from. FullRepresentation.qml therefore avoids them.
 *
 * Run with:
 *   QT_QPA_PLATFORM=offscreen /usr/lib/qt6/bin/qmltestrunner -input plasmoid/autotests
 */
TestCase {
    id: testCase
    name: "EventPropagation"
    width: 400
    height: 300
    visible: true
    when: windowShown

    // Everything the full representation is allowed to be built from.
    Component { id: c_item;       Item { anchors.fill: parent } }
    Component { id: c_column;     ColumnLayout { anchors.fill: parent; Item { Layout.fillWidth: true; Layout.fillHeight: true } } }
    Component { id: c_listview;   ListView { anchors.fill: parent; model: 8; delegate: Item { width: 380; height: 30 } } }
    Component { id: c_label;      PlasmaComponents.Label { anchors.fill: parent; text: "x" } }
    Component { id: c_icon;       Kirigami.Icon { anchors.fill: parent; source: "network-wired" } }
    Component { id: c_framesvg;   KSvg.FrameSvgItem { anchors.fill: parent; imagePath: "widgets/plasmoidheading"; prefix: "header" } }
    Component { id: c_placeholder; PlasmaExtras.PlaceholderMessage { anchors.fill: parent; text: "x" } }
    Component {
        id: c_scrollbar
        ListView {
            anchors.fill: parent
            model: 40
            clip: true
            delegate: Item { width: 380; height: 30 }
            PlasmaComponents.ScrollBar.vertical: PlasmaComponents.ScrollBar {}
        }
    }

    // Known to swallow. Reported rather than asserted, so that an upstream fix
    // shows up as a note instead of a failure.
    Component { id: c_representation; PlasmaExtras.Representation { anchors.fill: parent; contentItem: Item {} } }
    Component { id: c_heading;        PlasmaExtras.PlasmoidHeading { anchors.fill: parent; contentItem: Item {} } }
    Component { id: c_scrollview;     PlasmaComponents.ScrollView { anchors.fill: parent; ListView { model: 8; delegate: Item { width: 380; height: 30 } } } }

    // The shape FullRepresentation.qml actually uses.
    Component {
        id: c_structure
        Item {
            anchors.fill: parent
            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Item {
                    Layout.fillWidth: true
                    implicitHeight: 30
                    KSvg.FrameSvgItem {
                        anchors.fill: parent
                        imagePath: "widgets/plasmoidheading"
                        prefix: "header"
                    }
                    RowLayout {
                        anchors.fill: parent
                        PlasmaComponents.Label { Layout.fillWidth: true; text: "Plasma NetHogs" }
                        PlasmaComponents.Label { text: "↓ 1.0 MiB/s" }
                    }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: 8
                    clip: true
                    PlasmaComponents.ScrollBar.vertical: PlasmaComponents.ScrollBar {}
                    delegate: Item {
                        width: 380
                        height: 30
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton
                        }
                    }
                }
            }
        }
    }

    Component {
        id: harness
        Item {
            anchors.fill: parent
            property alias hits: containment.hits
            property Component payload

            // Stands in for the containment waiting to show the widget menu.
            MouseArea {
                id: containment
                property int hits: 0
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                onClicked: hits += 1
            }

            Loader {
                anchors.fill: parent
                sourceComponent: parent.payload
            }
        }
    }

    function reaches(component, label) {
        const item = createTemporaryObject(harness, testCase, { payload: component });
        verify(item, label + ": created");
        mouseClick(item, 200, 200, Qt.RightButton);
        wait(30);
        return item.hits === 1;
    }

    function test_components_in_use_pass_the_click_through() {
        const safe = [
            [c_item, "Item"],
            [c_column, "ColumnLayout"],
            [c_listview, "ListView"],
            [c_label, "PlasmaComponents.Label"],
            [c_icon, "Kirigami.Icon"],
            [c_framesvg, "KSvg.FrameSvgItem"],
            [c_placeholder, "PlasmaExtras.PlaceholderMessage"],
            [c_scrollbar, "ListView + attached ScrollBar"],
        ];
        for (const [component, label] of safe) {
            verify(reaches(component, label), label + " must not swallow the right click");
        }
    }

    function test_the_full_representation_shape_passes_the_click_through() {
        verify(reaches(c_structure, "FullRepresentation shape"),
               "the full representation must let a right click reach the containment");
    }

    function test_report_components_deliberately_avoided() {
        const avoided = [
            [c_representation, "PlasmaExtras.Representation"],
            [c_heading, "PlasmaExtras.PlasmoidHeading"],
            [c_scrollview, "PlasmaComponents.ScrollView"],
        ];
        for (const [component, label] of avoided) {
            if (reaches(component, label)) {
                console.warn("NOTE: " + label + " no longer swallows right clicks; "
                             + "FullRepresentation.qml could use it again");
            }
        }
    }
}
