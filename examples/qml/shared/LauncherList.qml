/****************************************************************************
**
** Copyright (C) 2012 Nokia Corporation and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/
**
** This file is part of the examples of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:BSD$
** You may use this file under the terms of the BSD license as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of Nokia Corporation and its Subsidiary(-ies) nor
**     the names of its contributors may be used to endorse or promote
**     products derived from this software without specific prior written
**     permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
**
** $QT_END_LICENSE$
**
****************************************************************************/
import QtQuick 2.0

ListView {
    //model is a list of {"name":"somename", "url":"file:///some/url/mainfile.qml"}
    //function used to add to model A) to enforce scheme B) to allow Qt.resolveUrl in url assignments
    function addExample(name, desc, url)
    {
        myModel.append({"name":name, "description":desc, "url":url})
    }
    function hideExample()
    {
        ei.visible = false;
    }

    clip: true
    delegate: SimpleLauncherDelegate{exampleItem: ei}
    model: ListModel {id:myModel}
    Item {
        id: ei
        visible: false
        clip: true
        property url exampleUrl
        onExampleUrlChanged: visible = (exampleUrl == '' ? false : true);//Setting exampleUrl automatically shows example
        anchors.fill: parent
        anchors.bottomMargin: 40
        Rectangle {
            id: bg
            anchors.fill: parent
            color: "white"
        }
        MouseArea{
            anchors.fill: parent
            enabled: ei.visible
            //Eats mouse events
        }
        Loader{
            source: ei.exampleUrl
            anchors.fill: parent
        }
    }
    Rectangle {
        id: bar
        visible: ei.visible
        anchors.bottom: parent.bottom
        width: parent.width
        height: 40
        MouseArea{
            anchors.fill: parent
            enabled: ei.visible
            //Eats mouse events
        }
        Image {
            source: "back.png"
            anchors.verticalCenter: parent.verticalCenter
            x: 4
            MouseArea {
                anchors.fill: parent
                onClicked: ei.exampleUrl = "";
            }
        }
    }
}
