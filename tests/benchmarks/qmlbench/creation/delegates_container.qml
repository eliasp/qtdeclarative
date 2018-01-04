import QtQuick 2.0
import QmlBench 1.0
import QtQuick.Controls 2.0

CreationBenchmark {
    id: root
    count: 20
    staticCount: 2000
    delegate: Container {
        x: QmlBench.getRandom() * root.width - width
        y: QmlBench.getRandom() * root.height - height
        width: 100
        height: 100
    }
}
