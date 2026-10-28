import QtQuick

Item {
    id: popup
    width: 370
    height: 118
    property string heading: "Replay saved"
    property string message: "Ready in your Library"
    property bool isWarning: false
    property url previewSource: ""
    property int sequence: 0
    property color accent: isWarning ? "#f3c16d" : "#b5ec72"

    onSequenceChanged: {
        entrance.restart()
        lifetime.restart()
    }

    Rectangle {
        id: card
        x: 4; y: 4
        width: parent.width - 8; height: parent.height - 8
        radius: 17
        color: "#151e28"
        border.width: 1
        border.color: "#354151"

        Rectangle { x: 0; y: 22; width: 4; height: parent.height - 44; radius: 2; color: popup.accent }

        Rectangle {
            x: 18; y: 23; width: 48; height: 48; radius: 14
            color: popup.isWarning ? "#423527" : "#253628"
            Text {
                anchors.centerIn: parent
                text: popup.isWarning ? "!" : "m"
                color: popup.accent; font.pixelSize: 32; font.weight: Font.DemiBold
            }
        }

        Column {
            x: 80; y: 19
            width: popup.previewSource.toString().length ? 204 : 262
            spacing: 5
            Text { text: "MOMENT"; color: popup.accent; font.pixelSize: 9; font.weight: Font.Bold; font.letterSpacing: 2 }
            Text { text: popup.heading; width: parent.width; color: "#f2f5f8"; font.pixelSize: 17; font.weight: Font.DemiBold; elide: Text.ElideRight }
            Text { text: popup.message; width: parent.width; color: "#a5b1bf"; font.pixelSize: 11; elide: Text.ElideRight }
        }

        Image {
            visible: popup.previewSource.toString().length > 0
            source: popup.previewSource
            x: parent.width - 68; y: 28; width: 50; height: 50
            fillMode: Image.PreserveAspectCrop; clip: true; asynchronous: true
        }

        Rectangle {
            id: progress
            x: 18; y: parent.height - 11
            width: parent.width - 36; height: 2; radius: 1
            color: popup.accent; opacity: 0.7
        }
    }

    ParallelAnimation {
        id: entrance
        NumberAnimation { target: card; property: "opacity"; from: 0; to: 1; duration: 160 }
        NumberAnimation { target: card; property: "y"; from: 16; to: 4; duration: 180; easing.type: Easing.OutCubic }
    }
    NumberAnimation {
        id: lifetime
        target: progress; property: "width"
        from: card.width - 36; to: 0; duration: 4200
    }
}
