// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

import QtQuick
import QtQuick.Window
import QtQuick.Controls
import TestModel

ApplicationWindow {
    width: 800
    height: 600
    visible: true

    property alias treeView: treeView
    property var selectedIndex: undefined

    UICallback { id: callback }

    Item {
        anchors.fill: parent
        anchors.margins: 10

        Column {
            id: leftMenu
            width: childrenRect.width
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom

            CheckBox {
                id: useCustomDelegate
                text: "Use custom delegate"
            }

            CheckBox {
                id: useFileSystemModel
                text: "Use file system model"
            }

            Button {
                text: "Show QTreeView"
                onClicked: callback.showQTreeView(treeView.model)
            }

            Button {
                text: "Insert row"
                onClicked: {
                    let index = treeView.modelIndex(1, 0)
                    treeView.model.insertRows(index.row, 1, index.parent);
                }
            }

            Button {
                text: "Remove row"
                onClicked: {
                    let index = treeView.modelIndex(1, 0)
                    treeView.model.removeRows(index.row, 1, index.parent);
                }
            }
            Button {
                text: "Expand to"
                enabled: selectedIndex != undefined
                onClicked: {
                    treeView.expandToIndex(selectedIndex);
                    treeView.forceLayout()
                    let row = treeView.rowAtIndex(selectedIndex)
                    treeView.positionViewAtRow(row, Qt.AlignVCenter)
                }
            }
        }

        TreeView {
            id: treeView
            width: parent.width
            anchors.left: leftMenu.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.leftMargin: 100
            anchors.topMargin: 100
            clip: true

            model: useFileSystemModel.checked ? fileSystemModel : testModel
            delegate: useCustomDelegate.checked ? customDelegate : treeViewDelegate
        }
    }

    TestModel {
        id: testModel
    }

    Component {
        id: treeViewDelegate
        TreeViewDelegate {
            TapHandler {
                acceptedModifiers: Qt.ControlModifier
                onTapped: {
                    if (treeView.isExpanded(row))
                        treeView.collapseRecursively(row)
                    else
                        treeView.expandRecursively(row)
                }
            }
            TapHandler {
                acceptedModifiers: Qt.ShiftModifier
                onTapped: selectedIndex = treeView.modelIndex(row, 0)
            }
            Rectangle {
                anchors.fill: parent
                border.color: "red"
                border.width: 1
                visible: treeView.modelIndex(row, column) === selectedIndex
            }
        }
    }

    Component {
        id: customDelegate
        Item {
            id: root

            implicitWidth: padding + label.x + label.implicitWidth + padding
            implicitHeight: label.implicitHeight * 1.5

            readonly property real indentation: 20
            readonly property real padding: 5

            // Assigned to by TreeView:
            required property TreeView treeView
            required property bool isTreeNode
            required property bool expanded
            required property int hasChildren
            required property int depth

            TapHandler {
                onTapped: treeView.toggleExpanded(row)
            }

            Text {
                id: indicator
                visible: root.isTreeNode && root.hasChildren
                x: padding + (root.depth * root.indentation)
                text: root.expanded ? "▼" : "▶"
            }

            Text {
                id: label
                x: padding + (root.isTreeNode ? (root.depth + 1) * root.indentation : 0)
                width: root.width - root.padding - x
                clip: true
                text: model.display
            }
        }
    }
}
