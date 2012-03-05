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
import "../contents"
Item {
  id:container
  width:360
  height:600

  Column {
    spacing:5
    anchors.fill:parent
    Text { font.pointSize:25; text:"Makes squircle icon with clip"; anchors.horizontalCenter:parent.horizontalCenter}
    Canvas {
      id:canvas
      width:360
      height:360
      property string strokeStyle:"blue"
      property string fillStyle:"steelblue"
      property int lineWidth:2
      property int nSize:nCtrl.value
      property real radius:rCtrl.value
      property bool fill:true
      property bool stroke:false
      property real px:xCtrl.value
      property real py:yCtrl.value
      property real alpha:alphaCtrl.value
      property string imagefile:"../contents/qt-logo.png"
      smooth:true
      renderTarget:Canvas.Image
      renderStrategy: Canvas.Immediate
      Component.onCompleted:loadImage(canvas.imagefile);

    onAlphaChanged:requestPaint();
    onRadiusChanged:requestPaint();
    onLineWidthChanged:requestPaint();
    onNSizeChanged:requestPaint();
    onFillChanged:requestPaint();
    onStrokeChanged:requestPaint();
    onPxChanged:requestPaint();
    onPyChanged:requestPaint();
    
    onImageLoaded : requestPaint();

    onPaint: squcirle();

    function squcirle() {
      var ctx = canvas.getContext("2d");
      var N = canvas.nSize;
      var R = canvas.radius;

      N=Math.abs(N);
      var M=N;
      if (N>100) M=100;
      if (N<0.00000000001) M=0.00000000001;

      ctx.save();
      ctx.globalAlpha =canvas.alpha;
      ctx.fillStyle = "gray";
      ctx.fillRect(0, 0, canvas.width, canvas.height);

      ctx.strokeStyle = canvas.strokeStyle;
      ctx.fillStyle = canvas.fillStyle;
      ctx.lineWidth = canvas.lineWidth;

      ctx.beginPath();
      var i = 0, x, y;
      for (i=0; i<(2*R+1); i++){
         x = Math.round(i-R) + canvas.px;
         y = Math.round(Math.pow(Math.abs(Math.pow(R,M)-Math.pow(Math.abs(i-R),M)),1/M)) + canvas.py;

         if (i == 0)
           ctx.moveTo(x, y);
         else
           ctx.lineTo(x, y);
      }

      for (i=(2*R); i<(4*R+1); i++){
        x =Math.round(3*R-i)+canvas.px;
        y = Math.round(-Math.pow(Math.abs(Math.pow(R,M)-Math.pow(Math.abs(3*R-i),M)),1/M)) + canvas.py;
        ctx.lineTo(x, y);
      }
      ctx.closePath();
      if (canvas.stroke) {
          ctx.stroke();
      }

      if (canvas.fill) {
          ctx.fill();
      }
      ctx.clip();

      ctx.drawImage(canvas.imagefile, 0, 0);
      ctx.restore();
    }
  }

    Rectangle {
        id:controls
        width:360
        height:160
        Column {
          spacing:3
          Slider {id:nCtrl; width:300; height:30; min:1; max:10; init:4; name:"N"}
          Slider {id:rCtrl; width:300; height:30; min:30; max:180; init:100; name:"Radius"}
          Slider {id:xCtrl; width:300; height:30; min:50; max:300; init:180; name:"X"}
          Slider {id:yCtrl; width:300; height:30; min:30; max:300; init:220; name:"Y"}
          Slider {id:alphaCtrl; width:300; height:30; min:0; max:1; init:1; name:"Alpha"}
        }
    }
  }
}
