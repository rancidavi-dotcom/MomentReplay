import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtMultimedia

ApplicationWindow {
    id: window
    width: 1280
    height: 820
    minimumWidth: 980
    minimumHeight: 660
    visible: true
    title: "Moment"
    color: "#10151b"
    font.family: "Noto Sans"
    font.pixelSize: 13
    palette.window: panel
    palette.windowText: ink
    palette.base: "#101b25"
    palette.alternateBase: panelRaised
    palette.text: ink
    palette.button: "#21313d"
    palette.buttonText: ink
    palette.highlight: accent
    palette.highlightedText: "#162211"
    palette.light: "#354858"
    palette.midlight: "#293c4b"
    palette.mid: "#233442"
    palette.dark: "#101820"
    palette.placeholderText: muted

    onClosing: function(close) {
        if (trayController.hideWindowToTray())
            close.accepted = false
    }

    property color panel: "#171e26"
    property color panelRaised: "#1a2935"
    property color line: "#2a343f"
    property color ink: "#f2f5f8"
    property color muted: "#94a6b7"
    property color accent: "#b5ec72"
    property int page: 0
    property url activeSource: ""
    property bool activeIsImage: false
    property url activeImageSource: ""
    property string selectedGameProcess: ""
    property string manualGameProcess: ""
    property bool shareSettingsHighlighted: false

    Timer {
        id: shareSettingsHighlightTimer
        interval: 2200
        onTriggered: window.shareSettingsHighlighted = false
    }

    function openShareSettings() {
        page = 2
        Qt.callLater(function() {
            const cardY = shareSettingsCard.mapToItem(preferencesScroll.contentItem, Qt.point(0, 0)).y
            preferencesScroll.contentY = Math.max(0, Math.min(cardY - 20,
                preferencesScroll.contentHeight - preferencesScroll.height))
            window.shareSettingsHighlighted = true
            shareSettingsHighlightTimer.restart()
        })
    }

    function actionButtonFill(button, normalColor, disabledColor) {
        if (!button.enabled)
            return disabledColor
        if (button.down)
            return Qt.darker(normalColor, 1.12)
        if (button.hovered)
            return Qt.lighter(normalColor, 1.24)
        return normalColor
    }

    function formatTime(milliseconds) {
        const totalMs = Math.max(0, Math.floor(milliseconds))
        const minutes = Math.floor(totalMs / 60000)
        const seconds = Math.floor(totalMs / 1000) % 60
        const centiseconds = Math.floor((totalMs % 1000) / 10)
        return minutes + ":" + String(seconds).padStart(2, "0") + "." + String(centiseconds).padStart(2, "0")
    }

    function openEditor() {
        if (activeSource.toString().length === 0 || activeIsImage)
            return
        player.stop()
        player.source = activeSource
        videoEditor.openClip(localPathFromUrl(activeSource))
        page = 3
        Qt.callLater(function() {
            videoEditor.setDurationMs(player.duration)
            player.position = videoEditor.inMs
            player.play()
        })
    }

    function navigateToEditor() {
        if (activeSource.toString().length > 0 && !activeIsImage)
            openEditor()
        else {
            player.stop()
            player.source = ""
            videoEditor.openClip("")
            page = 3
        }
    }

    function localPathFromUrl(fileUrl) {
        return decodeURIComponent(fileUrl.toString().replace(/^file:\/\//, ""))
    }

    Component.onCompleted: Qt.callLater(function() {
        if (!screenRecorder.buffering && !screenRecorder.busy)
            screenRecorder.startBuffering()
    })

    FileDialog {
        id: importDialog
        title: "Import media to Moment"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Videos and images (*.mp4 *.m4v *.mkv *.mov *.webm *.avi *.mpg *.mpeg *.ts *.mts *.ogv *.png *.jpg *.jpeg)", "All files (*)"]
        onAccepted: {
            for (const file of selectedFiles)
                clipLibrary.importFile(file.toLocalFile())
        }
    }

    FileDialog {
        id: editorVideoDialog
        title: "Choose a video to edit"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Videos (*.mp4 *.m4v *.mkv *.mov *.webm *.avi *.mpg *.mpeg *.ts *.mts *.ogv)", "All files (*)"]
        onAccepted: {
            const videoUrl = selectedFile
            const videoPath = window.localPathFromUrl(videoUrl)
            clipLibrary.importFile(videoPath)
            window.activeSource = videoUrl
            window.activeIsImage = false
            window.activeImageSource = ""
            player.stop()
            player.source = videoUrl
            videoEditor.openClip(videoPath)
            window.page = 3
            Qt.callLater(function() {
                videoEditor.setDurationMs(player.duration)
                player.position = videoEditor.inMs
                player.play()
            })
        }
    }

    Dialog {
        id: messageDialog
        property string message: ""
        modal: true
        title: "Moment"
        standardButtons: Dialog.Ok
        anchors.centerIn: parent
        width: 420
        contentItem: Label {
            text: messageDialog.message
            color: window.ink
            wrapMode: Text.Wrap
            width: 360
        }
    }

    Dialog {
        id: removeDialog
        property int clipId: 0
        modal: true
        title: "Remove from library?"
        standardButtons: Dialog.Cancel | Dialog.Ok
        anchors.centerIn: parent
        width: 420
        contentItem: Label {
            text: "This removes the entry from Moment. The media file remains on disk."
            color: window.ink; wrapMode: Text.Wrap; width: 360
        }
        onAccepted: clipLibrary.removeFromLibrary(clipId)
    }

    Connections {
        target: clipLibrary
        function onOperationFailed(message) { messageDialog.message = message; messageDialog.open() }
    }

    MediaPlayer {
        id: player
        audioOutput: AudioOutput { volume: volumeSlider.value }
        videoOutput: window.page === 3 ? editorVideoOutput : libraryVideoOutput
        onErrorOccurred: function(error, errorString) {
            if (error !== MediaPlayer.NoError) {
                messageDialog.message = errorString.length ? errorString : "This video could not be played by the available media plugins."
                messageDialog.open()
            }
        }
    }

    Connections {
        target: player
        function onDurationChanged() {
            if (window.page === 3)
                videoEditor.setDurationMs(player.duration)
        }
        function onPositionChanged() {
            if (window.page === 3 && player.playbackState === MediaPlayer.PlayingState
                    && player.position >= videoEditor.outMs)
                player.pause()
        }
    }

    Connections {
        target: videoEditor
        function onExportSucceeded(path) {
            window.activeSource = videoEditor.sourceUrl
            window.activeIsImage = false
            window.activeImageSource = ""
            player.stop()
            player.source = videoEditor.sourceUrl
            window.page = 3
            Qt.callLater(function() {
                videoEditor.setDurationMs(player.duration)
                player.position = 0
                player.play()
            })
        }
    }

    component MomentIcon: Canvas {
        property string name: "library"
        property color ink: window.muted
        implicitWidth: 20; implicitHeight: 20
        onNameChanged: requestPaint()
        onInkChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.scale(width / 24, height / 24)
            ctx.strokeStyle = ink
            ctx.fillStyle = ink
            ctx.lineWidth = 1.7
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            if (name === "library") {
                ctx.strokeRect(3, 3, 7, 7); ctx.strokeRect(14, 3, 7, 7)
                ctx.strokeRect(3, 14, 7, 7); ctx.strokeRect(14, 14, 7, 7)
            } else if (name === "capture") {
                ctx.beginPath(); ctx.arc(12, 12, 9, 0, Math.PI * 2); ctx.stroke()
                ctx.beginPath(); ctx.arc(12, 12, 3.5, 0, Math.PI * 2); ctx.fill()
            } else if (name === "settings") {
                for (let i = 0; i < 3; ++i) {
                    const y = 5 + i * 7, x = i === 1 ? 15 : 8
                    ctx.beginPath(); ctx.moveTo(3, y); ctx.lineTo(x - 3, y); ctx.moveTo(x + 3, y); ctx.lineTo(21, y); ctx.stroke()
                    ctx.beginPath(); ctx.arc(x, y, 2.6, 0, Math.PI * 2); ctx.stroke()
                }
            } else if (name === "search") {
                ctx.beginPath(); ctx.arc(10, 10, 6, 0, Math.PI * 2); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(15, 15); ctx.lineTo(21, 21); ctx.stroke()
            } else if (name === "play") {
                ctx.beginPath(); ctx.moveTo(8, 5); ctx.lineTo(19, 12); ctx.lineTo(8, 19); ctx.closePath(); ctx.stroke()
            } else if (name === "chevron") {
                ctx.beginPath(); ctx.moveTo(6, 9); ctx.lineTo(12, 15); ctx.lineTo(18, 9); ctx.stroke()
            } else if (name === "shield") {
                ctx.beginPath(); ctx.moveTo(12, 3); ctx.lineTo(20, 6); ctx.lineTo(19, 14); ctx.quadraticCurveTo(17, 19, 12, 22); ctx.quadraticCurveTo(7, 19, 5, 14); ctx.lineTo(4, 6); ctx.closePath(); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(8, 12); ctx.lineTo(11, 15); ctx.lineTo(16, 9); ctx.stroke()
            }
        }
    }

    component SettingCombo: ComboBox {
        id: select
        implicitWidth: 174; implicitHeight: 40
        leftPadding: 14; rightPadding: 34
        contentItem: Text { text: select.displayText; color: select.enabled ? window.ink : window.muted; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        indicator: MomentIcon { name: "chevron"; x: select.width - width - 12; y: (select.height - height) / 2; width: 14; height: 14 }
        background: Rectangle { radius: 9; color: select.hovered ? "#243644" : "#1a2a36"; border.color: select.activeFocus ? window.accent : "#334959"; opacity: select.enabled ? 1 : 0.5 }
        delegate: ItemDelegate {
            required property var modelData
            required property int index
            width: select.width - 12; height: 36
            highlighted: select.highlightedIndex === index
            contentItem: Text { text: String(modelData); color: parent.highlighted ? window.accent : window.ink; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
            background: Rectangle { radius: 6; color: parent.highlighted ? "#2b3e34" : "transparent" }
        }
        popup: Popup {
            y: select.height + 6; width: select.width; padding: 6
            implicitHeight: Math.min(contentItem.implicitHeight + 12, 280)
            contentItem: ListView { clip: true; implicitHeight: contentHeight; model: select.popup.visible ? select.delegateModel : null; currentIndex: select.highlightedIndex; ScrollIndicator.vertical: ScrollIndicator {} }
            background: Rectangle { radius: 11; color: "#192833"; border.color: "#3a5060" }
        }
    }

    component MomentSlider: Slider {
        id: slider
        implicitHeight: 28
        background: Rectangle {
            x: slider.leftPadding; y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth; height: 4; radius: 2; color: "#2a3e4e"
            Rectangle { width: slider.visualPosition * parent.width; height: parent.height; radius: 2; color: window.accent; opacity: slider.enabled ? 1 : 0.35 }
        }
        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: 10; height: 10; radius: 5; color: slider.pressed ? window.ink : window.accent; opacity: slider.enabled ? 1 : 0.35
        }
    }

    component EditorTrimTimeline: Item {
        id: trimTimeline
        property real durationMs: 0
        property real inMs: 0
        property real outMs: 0
        property real playheadMs: 0
        property bool editingEnabled: false
        signal trimStartRequested(int positionMs)
        signal trimEndRequested(int positionMs)
        signal seekRequested(int positionMs)
        implicitHeight: 70

        function positionAt(xPosition) {
            if (durationMs <= 0 || track.width <= 0)
                return 0
            return Math.round(Math.max(0, Math.min(1, (xPosition - track.x) / track.width)) * durationMs)
        }
        function xFor(positionMs) {
            return track.x + (durationMs > 0 ? Math.max(0, Math.min(1, positionMs / durationMs)) * track.width : 0)
        }

        Rectangle {
            id: track
            x: 16; y: 31; width: Math.max(1, trimTimeline.width - 32); height: 10; radius: 5
            color: "#2c3c48"
            Rectangle {
                x: trimTimeline.durationMs > 0 ? trimTimeline.inMs / trimTimeline.durationMs * parent.width : 0
                width: trimTimeline.durationMs > 0 ? Math.max(0, (trimTimeline.outMs - trimTimeline.inMs) / trimTimeline.durationMs * parent.width) : 0
                height: parent.height; radius: 5; color: "#587a3c"
            }
            MouseArea {
                anchors.fill: parent
                enabled: trimTimeline.editingEnabled
                cursorShape: Qt.PointingHandCursor
                onClicked: trimTimeline.seekRequested(trimTimeline.positionAt(mapToItem(trimTimeline, mouse.x, mouse.y).x))
            }
        }
        Rectangle {
            x: trimTimeline.xFor(trimTimeline.playheadMs) - 1; y: 19; width: 2; height: 34
            color: window.ink; opacity: trimTimeline.durationMs > 0 ? 0.9 : 0.25; z: 2
        }
        Rectangle {
            x: trimTimeline.xFor(trimTimeline.inMs) - width / 2; y: 20; width: 14; height: 32; radius: 5
            color: trimTimeline.editingEnabled ? window.accent : "#70816c"; z: 3
            Rectangle { anchors.horizontalCenter: parent.horizontalCenter; anchors.verticalCenter: parent.verticalCenter; width: 2; height: 16; radius: 1; color: "#203018" }
            MouseArea {
                anchors.fill: parent; enabled: trimTimeline.editingEnabled; cursorShape: Qt.SizeHorCursor
                onPositionChanged: if (pressed) trimTimeline.trimStartRequested(Math.min(trimTimeline.positionAt(mapToItem(trimTimeline, mouse.x, mouse.y).x), trimTimeline.outMs - 1))
            }
        }
        Rectangle {
            x: trimTimeline.xFor(trimTimeline.outMs) - width / 2; y: 20; width: 14; height: 32; radius: 5
            color: trimTimeline.editingEnabled ? window.accent : "#70816c"; z: 3
            Rectangle { anchors.horizontalCenter: parent.horizontalCenter; anchors.verticalCenter: parent.verticalCenter; width: 2; height: 16; radius: 1; color: "#203018" }
            MouseArea {
                anchors.fill: parent; enabled: trimTimeline.editingEnabled; cursorShape: Qt.SizeHorCursor
                onPositionChanged: if (pressed) trimTimeline.trimEndRequested(Math.max(trimTimeline.positionAt(mapToItem(trimTimeline, mouse.x, mouse.y).x), trimTimeline.inMs + 1))
            }
        }
        Label { x: 16; y: 0; text: trimTimeline.durationMs > 0 ? "00:00.00" : "—"; color: window.muted; font.pixelSize: 9 }
        Label { x: Math.max(16, trimTimeline.width - width - 16); y: 0; text: window.formatTime(trimTimeline.durationMs); color: window.muted; font.pixelSize: 9 }
        Label { x: 16; y: 55; text: "IN  " + window.formatTime(trimTimeline.inMs); color: window.accent; font.pixelSize: 9; font.bold: true }
        Label { x: Math.max(16, trimTimeline.width - width - 16); y: 55; text: "OUT  " + window.formatTime(trimTimeline.outMs); color: window.accent; font.pixelSize: 9; font.bold: true }
    }

    component MomentProgress: ProgressBar {
        id: progress
        implicitHeight: 5
        background: Rectangle { color: "#293d42"; radius: 3 }
        contentItem: Item { Rectangle { width: progress.visualPosition * parent.width; height: parent.height; radius: 3; color: window.accent } }
    }

    component MomentSwitch: Switch {
        id: toggle
        implicitHeight: 32
        spacing: 10
        indicator: Rectangle {
            x: toggle.leftPadding; y: (toggle.height - height) / 2
            width: 36; height: 21; radius: 11
            color: toggle.checked ? "#6c9848" : "#293d4b"
            border.color: toggle.checked ? "#a3d16d" : "#405563"
            opacity: toggle.enabled ? 1 : 0.5
            Rectangle {
                x: toggle.checked ? 18 : 3; y: 3; width: 15; height: 15; radius: 8
                color: toggle.checked ? "#e1f9c6" : "#93a7b7"
                Behavior on x { NumberAnimation { duration: 120 } }
            }
        }
        contentItem: Text {
            text: toggle.text; color: window.ink; font.pixelSize: 12
            leftPadding: toggle.indicator.width + toggle.spacing
            verticalAlignment: Text.AlignVCenter
        }
    }

    component NavButton: Button {
        id: nav
        property bool selected: false
        property string label: ""
        property string iconText: ""
        Layout.fillWidth: true
        implicitHeight: 47
        leftPadding: 15; rightPadding: 12
        contentItem: RowLayout {
            spacing: 12
            MomentIcon { name: nav.iconText; ink: nav.selected ? window.accent : window.muted; Layout.preferredWidth: 18; Layout.preferredHeight: 18 }
            Label { text: nav.label; color: nav.selected ? window.ink : window.muted; font.pixelSize: 13; font.weight: nav.selected ? Font.DemiBold : Font.Normal }
            Item { Layout.fillWidth: true }
            
        }
        background: Rectangle { radius: 7; color: nav.selected ? "#22352c" : nav.hovered ? "#2a3c49" : "transparent"; border.color: nav.selected ? "#385440" : nav.hovered ? "#334957" : "transparent"; Behavior on color { ColorAnimation { duration: 120 } } }
    }

    component SectionTitle: Label {
        color: window.muted
        font.pixelSize: 9
        font.bold: true
        font.letterSpacing: 1.4
    }

    component Surface: Rectangle {
        color: window.panel
        radius: 10
        border.color: window.line
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 214
            color: "#0e1822"
            border.color: "#21303c"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 11
                    Layout.topMargin: 10
                    Layout.bottomMargin: 22
                    Image {
                        Layout.preferredWidth: 42; Layout.preferredHeight: 42
                        source: "qrc:/icons/io.moment.Moment.svg"
                        sourceSize: Qt.size(84, 84)
                        smooth: true
                    }
                    Column {
                        spacing: 1
                        Label { text: "moment"; color: window.ink; font.weight: Font.DemiBold; font.pixelSize: 21; font.letterSpacing: -0.6 }
                        Label { text: "Desktop capture"; color: window.muted; font.pixelSize: 9 }
                    }
                }
                SectionTitle { text: "WORKSPACE"; Layout.leftMargin: 12; Layout.bottomMargin: 4 }
                NavButton { label: "Library"; iconText: "library"; selected: window.page === 0; onClicked: window.page = 0 }
                NavButton { label: "Capture"; iconText: "capture"; selected: window.page === 1; onClicked: window.page = 1 }
                NavButton { objectName: "editorNavButton"; label: "Editor"; iconText: "play"; selected: window.page === 3; onClicked: window.navigateToEditor() }
                NavButton { label: "Preferences"; iconText: "settings"; selected: window.page === 2; onClicked: window.page = 2 }
                Item { Layout.fillHeight: true }
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: 100
                    color: screenRecorder.buffering ? "#172b25" : "#17232e"; radius: 13
                    border.color: screenRecorder.buffering ? "#324f3b" : window.line
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 14; spacing: 9
                        SectionTitle { text: "CAPTURE STATUS"; color: "#91a99a" }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 7
                            Rectangle { width: 6; height: 6; radius: 3; color: screenRecorder.buffering ? window.accent : "#efbd68" }
                            Label { text: screenRecorder.buffering ? "Replay active" : screenRecorder.busy ? "Awaiting permission" : "Capture idle"; color: window.ink; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                            Label { text: screenRecorder.buffering ? screenRecorder.bufferedSeconds + "s" : ""; color: window.accent; font.pixelSize: 11; font.bold: true }
                        }
                        MomentProgress { Layout.fillWidth: true; from: 0; to: Math.max(1, screenRecorder.bufferSeconds); value: Math.min(screenRecorder.bufferedSeconds, to) }
                        Label { text: "Up to " + screenRecorder.bufferSeconds + " seconds in memory"; color: "#8da397"; font.pixelSize: 9 }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; Layout.topMargin: 7; Layout.bottomMargin: 4; spacing: 9
                    MomentIcon { name: "shield"; ink: "#718899"; Layout.preferredWidth: 18; Layout.preferredHeight: 18 }
                    Label { Layout.fillWidth: true; text: "Local files"; color: "#8296a7"; wrapMode: Text.Wrap; font.pixelSize: 9 }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: window.width < 1100 ? 22 : 30
            spacing: 18

            StackLayout {
                id: pages
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: window.page

                // Library
                Item {
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 18
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                spacing: 5
                                Label { text: "Library"; color: window.ink; font.pixelSize: 26; font.weight: Font.DemiBold; font.letterSpacing: -0.7 }
                                Label { text: clipLibrary.clipCount + (clipLibrary.clipCount === 1 ? " item" : " items") + " · local library"; color: window.muted; font.pixelSize: 11 }
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                text: "＋  Import media"
                                implicitHeight: 38; leftPadding: 16; rightPadding: 16
                                onClicked: importDialog.open()
                                contentItem: Label { text: parent.text; color: window.ink; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle { radius: 7; color: window.actionButtonFill(parent, "#23313b", "#1b252e"); border.color: parent.hovered ? "#718b99" : "#3d4f5a"; Behavior on color { ColorAnimation { duration: 100 } } }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 15
                            Surface {
                                Layout.preferredWidth: window.width < 1100 ? 290 : 328; Layout.fillHeight: true
                                ColumnLayout {
                                    anchors.fill: parent; anchors.margins: 16; spacing: 14
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label { text: "Media"; color: window.ink; font.pixelSize: 13; font.weight: Font.DemiBold }
                                        Item { Layout.fillWidth: true }
                                        Rectangle {
                                            implicitWidth: 28; implicitHeight: 23; radius: 5; color: "#26343e"
                                            Label { anchors.centerIn: parent; text: String(clipLibrary.clipCount).padStart(2, "0"); color: window.accent; font.pixelSize: 10; font.bold: true }
                                        }
                                    }
                                    TextField {
                                        objectName: "librarySearch"
                                        Layout.fillWidth: true
                                        implicitHeight: 39; leftPadding: 35
                                        placeholderText: "Search media"
                                        color: window.ink; placeholderTextColor: "#748091"
                                        onTextChanged: clipLibrary.searchQuery = text
                                        MomentIcon { name: "search"; width: 15; height: 15; anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter; ink: "#7d93a5" }
                                        background: Rectangle { radius: 9; color: parent.hovered ? "#152430" : "#0e1822"; border.color: parent.activeFocus ? "#6f9254" : parent.hovered ? "#4b6574" : window.line; Behavior on color { ColorAnimation { duration: 100 } } }
                                    }
                                    ListView {
                                        id: clipList
                                        Layout.fillWidth: true; Layout.fillHeight: true
                                        clip: true; spacing: 6; model: clipLibrary
                                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                        delegate: Rectangle {
                                            required property int clipId
                                            required property string path
                                            required property string title
                                            required property string importedAt
                                            required property double sizeBytes
                                            required property bool favorite
                                            required property bool available
                                            required property url sourceUrl
                                            required property bool isImage
                                            width: ListView.view.width; height: 79; radius: 11
                                            color: window.activeSource.toString() === sourceUrl.toString() ? "#23372f" : clipMouse.containsMouse ? "#1c2d39" : "#17242f"
                                            border.color: window.activeSource.toString() === sourceUrl.toString() ? "#4a674b" : "transparent"
                                            RowLayout {
                                                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 8; spacing: 10
                                                Rectangle {
                                                    Layout.preferredWidth: 49; Layout.preferredHeight: 49; radius: 9; color: "#263c48"
                                                    Image {
                                                        anchors.fill: parent; anchors.margins: 2
                                                        source: isImage && available ? sourceUrl : ""
                                                        fillMode: Image.PreserveAspectCrop
                                                        visible: isImage && available
                                                        asynchronous: true
                                                    }
                                                    Label { anchors.centerIn: parent; text: isImage ? "▧" : "▶"; color: window.accent; font.pixelSize: 14; visible: !isImage || !available }
                                                }
                                                ColumnLayout {
                                                    Layout.fillWidth: true; spacing: 4
                                                    Label { Layout.fillWidth: true; text: title; color: available ? window.ink : "#e2a57b"; elide: Text.ElideRight; font.pixelSize: 12; font.weight: Font.DemiBold }
                                                    Label { text: (sizeBytes / 1048576).toFixed(1) + " MB  ·  " + importedAt; color: window.muted; font.pixelSize: 9 }
                                                }
                                                ToolButton {
                                                    implicitWidth: 24; implicitHeight: 30; padding: 0
                                                    text: favorite ? "★" : "☆"; onClicked: clipLibrary.toggleFavorite(clipId)
                                                    contentItem: Label { text: parent.text; color: favorite ? window.accent : "#90a6b7"; font.pixelSize: 16; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                                }
                                                ToolButton {
                                                    implicitWidth: 22; implicitHeight: 30; padding: 0
                                                    text: "···"
                                                    onClicked: { removeDialog.clipId = clipId; removeDialog.open() }
                                                    contentItem: Label { text: parent.text; color: "#90a6b7"; font.pixelSize: 15; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                                }
                                            }
                                            MouseArea {
                                                id: clipMouse; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.LeftButton; z: -1
                                                onClicked: {
                                                    window.activeSource = sourceUrl
                                                    window.activeIsImage = isImage
                                                    if (isImage) {
                                                        player.stop()
                                                        player.source = ""
                                                        window.activeImageSource = sourceUrl
                                                    } else {
                                                        window.activeImageSource = ""
                                                        player.source = sourceUrl
                                                    }
                                                }
                                                onDoubleClicked: {
                                                    window.activeSource = sourceUrl
                                                    window.activeIsImage = isImage
                                                    if (isImage) {
                                                        player.stop()
                                                        player.source = ""
                                                        window.activeImageSource = sourceUrl
                                                    } else {
                                                        window.activeImageSource = ""
                                                        player.source = sourceUrl
                                                        player.play()
                                                    }
                                                }
                                            }
                                        }
                                        Label {
                                            anchors.centerIn: parent; width: parent.width - 36
                                            visible: clipList.count === 0
                                            text: clipLibrary.searchQuery.length ? "No matching media." : "No media yet.\nImport a file or save a replay."
                                            color: window.muted; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; font.pixelSize: 12
                                        }
                                    }
                                }
                            }
                            Surface {
                                Layout.fillWidth: true; Layout.fillHeight: true
                                ColumnLayout {
                                    anchors.fill: parent; anchors.margins: 18; spacing: 12
                                    RowLayout {
                                        Layout.fillWidth: true
                                        SectionTitle { text: "PREVIEW" }
                                        Item { Layout.fillWidth: true }
                                        Button {
                                            text: "Edit clip"
                                            enabled: window.activeSource.toString().length > 0 && !window.activeIsImage
                                            implicitHeight: 30; leftPadding: 12; rightPadding: 12
                                            onClicked: window.openEditor()
                                            contentItem: Label { text: parent.text; color: parent.enabled ? window.ink : window.muted; font.pixelSize: 10; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                            background: Rectangle { radius: 7; color: window.actionButtonFill(parent, "#22352c", "#1b252e"); border.color: parent.hovered ? "#8dbb62" : parent.enabled ? "#405d45" : window.line; Behavior on color { ColorAnimation { duration: 100 } } }
                                        }
                                        Button {
                                            objectName: "createClipShareLinkButton"
                                            text: driveShare.busy ? "Sharing…" : driveShare.connected ? "Create link" : "Share clip"
                                            enabled: window.activeSource.toString().length > 0 && !window.activeIsImage && !driveShare.busy
                                            implicitHeight: 30; leftPadding: 12; rightPadding: 12
                                            ToolTip.visible: hovered && !driveShare.configured
                                            ToolTip.text: "Drive sharing will be available when Moment's OAuth app is configured."
                            onClicked: {
                                if (!driveShare.configured)
                                    window.openShareSettings()
                                else
                                    driveShare.shareClipUrl(window.activeSource)
                            }
                                            contentItem: Label { text: parent.text; color: parent.enabled ? window.ink : window.muted; font.pixelSize: 10; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                            background: Rectangle { radius: 7; color: window.actionButtonFill(parent, "#22352c", "#1b252e"); border.color: parent.hovered ? "#9dd565" : parent.enabled ? "#405d45" : window.line; Behavior on color { ColorAnimation { duration: 100 } } }
                                        }
                                        Label { text: player.playbackState === MediaPlayer.PlayingState ? "PLAYING" : window.activeSource.toString().length ? (window.activeIsImage ? "IMAGE" : "VIDEO") : "READY WHEN YOU ARE"; color: player.playbackState === MediaPlayer.PlayingState ? window.accent : "#8097a8"; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
                                    }
                                    Rectangle {
                                        Layout.fillWidth: true; Layout.fillHeight: true; radius: 12; color: "#0c141d"; clip: true
                                        VideoOutput { id: libraryVideoOutput; anchors.fill: parent; fillMode: VideoOutput.PreserveAspectFit; visible: !window.activeIsImage }
                                        Image { anchors.fill: parent; source: window.activeImageSource; fillMode: Image.PreserveAspectFit; visible: window.activeIsImage; asynchronous: true }
                                        Column {
                                            anchors.centerIn: parent; width: parent.width - 36; spacing: 15; visible: window.activeSource.toString().length === 0
                                            Rectangle {
                                                anchors.horizontalCenter: parent.horizontalCenter; width: 64; height: 64; radius: 20; color: "#1d302c"; border.color: "#3b5543"
                                                MomentIcon { anchors.centerIn: parent; width: 25; height: 25; name: "play"; ink: window.accent }
                                            }
                                            Label { width: parent.width; text: "Select media to preview"; color: window.ink; font.pixelSize: 15; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap }
                                            Label { width: parent.width; text: "Choose a video or image from the list."; color: "#8198aa"; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter; lineHeight: 1.4 }
                                        }
                                    }
                                    Label { Layout.fillWidth: true; text: window.activeSource.toString().length ? window.activeSource.toString().split("/").pop() : "Nothing selected yet"; color: window.activeSource.toString().length ? window.ink : window.muted; elide: Text.ElideMiddle; font.pixelSize: 12; font.weight: Font.DemiBold }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        ToolButton { text: player.playbackState === MediaPlayer.PlayingState ? "Ⅱ" : "▶"; enabled: window.activeSource.toString().length > 0 && !window.activeIsImage; onClicked: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play(); palette.buttonText: window.accent }
                                        MomentSlider { id: seekSlider; Layout.fillWidth: true; from: 0; to: Math.max(player.duration, 1); value: player.position; enabled: player.duration > 0 && !window.activeIsImage; onMoved: player.position = value }
                                        Label { text: Math.floor(player.position / 60000) + ":" + String(Math.floor(player.position / 1000) % 60).padStart(2, "0") + " / " + Math.floor(player.duration / 60000) + ":" + String(Math.floor(player.duration / 1000) % 60).padStart(2, "0"); color: window.muted; font.pixelSize: 10 }
                                        MomentSlider { id: volumeSlider; Layout.preferredWidth: 72; from: 0; to: 1; value: 0.75 }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 5
                                        Label {
                                            objectName: "clipShareStatus"
                                            Layout.fillWidth: true
                                            visible: window.activeSource.toString().length > 0
                                            text: driveShare.statusMessage
                                            color: driveShare.lastShareUrl.length > 0 ? window.accent : window.muted
                                            font.pixelSize: 11; wrapMode: Text.Wrap
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            visible: driveShare.lastShareUrl.length > 0
                                            Label {
                                                objectName: "clipShareLink"
                                                Layout.fillWidth: true
                                                text: driveShare.lastShareUrl
                                                color: window.accent
                                                font.pixelSize: 10
                                                wrapMode: Text.WrapAnywhere
                                                maximumLineCount: 2
                                                elide: Text.ElideMiddle
                                            }
                                            Button {
                                                objectName: "copyClipShareLinkButton"
                                                text: "Copy link"
                                                onClicked: driveShare.copyLastShareUrl()
                                                contentItem: Label {
                                                    text: parent.text
                                                    color: parent.enabled ? window.ink : window.muted
                                                    font.pixelSize: 10
                                                    font.weight: Font.DemiBold
                                                    horizontalAlignment: Text.AlignHCenter
                                                    verticalAlignment: Text.AlignVCenter
                                                }
                                                background: Rectangle {
                                                    radius: 7
                                                    color: window.actionButtonFill(parent, "#22352c", "#1b252e")
                                                    border.color: parent.hovered ? "#9dd565" : window.line
                                                    Behavior on color { ColorAnimation { duration: 100 } }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Capture
                Item {
                    Flickable {
                        id: captureScroll
                        objectName: "captureScroll"
                        anchors.fill: parent; contentWidth: width; contentHeight: captureColumn.implicitHeight; clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        ColumnLayout {
                            id: captureColumn; width: parent.width; spacing: 18
                            RowLayout {
                                Layout.fillWidth: true
                                ColumnLayout {
                                    spacing: 3
                                    Label { text: "Capture"; color: window.ink; font.pixelSize: 26; font.weight: Font.DemiBold; font.letterSpacing: -0.3 }
                                    Label { text: "Replay buffer and continuous recording."; color: window.muted; font.pixelSize: 12 }
                                }
                                Item { Layout.fillWidth: true }
                                Rectangle {
                                    radius: 14; color: screenRecorder.buffering ? "#1b2b20" : "#1a2029"; border.color: screenRecorder.buffering ? "#445d37" : window.line
                                    implicitWidth: statusRow.implicitWidth + 24; implicitHeight: 34
                                    RowLayout {
                                        id: statusRow; anchors.centerIn: parent; spacing: 8
                                        Rectangle { width: 7; height: 7; radius: 4; color: screenRecorder.buffering ? window.accent : "#778393" }
                                        Label {
                                            text: screenRecorder.recording ? "Recording session"
                                                  : screenRecorder.buffering ? "Replay window active"
                                                  : screenRecorder.busy ? "Waiting for screen permission"
                                                  : "Capture stopped"
                                            color: window.ink; font.pixelSize: 10
                                        }
                                    }
                                }
                            }
                            Surface {
                                Layout.fillWidth: true; implicitHeight: 320
                                RowLayout {
                                    anchors.fill: parent; anchors.margins: 24; spacing: 25
                                    ColumnLayout {
                                        Layout.fillWidth: true; Layout.fillHeight: true; spacing: 12
                                        SectionTitle { text: "INSTANT REPLAY" }
                                        Label { text: "Replay buffer"; color: window.ink; font.pixelSize: 23; font.weight: Font.DemiBold }
                                        Label {
                                            Layout.fillWidth: true
                                            text: "Save the recent " + recordingPreferences.effectiveReplayClipSeconds + " seconds without interrupting the current session."
                                            color: window.muted; wrapMode: Text.Wrap; font.pixelSize: 12
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true; spacing: 8
                                            Label { text: "Window"; color: window.muted; font.pixelSize: 11 }
                                            SettingCombo {
                                                id: captureDuration
                                                model: [15, 30, 60, 90, 120]
                                                currentIndex: Math.max(0, model.indexOf(screenRecorder.bufferSeconds))
                                                onActivated: screenRecorder.bufferSeconds = Number(currentValue)
                                            }
                                            Item { Layout.fillWidth: true }
                                            Label { text: screenRecorder.bufferedSeconds + " / " + screenRecorder.bufferSeconds + " sec"; color: window.accent; font.pixelSize: 11; font.bold: true }
                                        }
                                        MomentProgress {
                                            Layout.fillWidth: true
                                            from: 0; to: Math.max(1, screenRecorder.bufferSeconds); value: Math.min(screenRecorder.bufferedSeconds, screenRecorder.bufferSeconds)
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true; spacing: 10
                                            Button {
                                                implicitHeight: 40; leftPadding: 14; rightPadding: 14
                                                visible: !screenRecorder.buffering && !screenRecorder.busy
                                                enabled: visible
                                                text: "Retry screen capture"
                                                onClicked: screenRecorder.startBuffering()
                                                contentItem: Label { text: parent.text; color: window.ink; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                                background: Rectangle { radius: 9; color: window.actionButtonFill(parent, "#202833", "#202833"); border.color: parent.hovered ? "#627282" : window.line; Behavior on color { ColorAnimation { duration: 100 } } }
                                            }
                                            Button {
                                                implicitHeight: 40; leftPadding: 14; rightPadding: 14
                                                Layout.fillWidth: true
                                                enabled: screenRecorder.buffering && screenRecorder.bufferedSeconds > 0 && !screenRecorder.replayExporting
                                                text: screenRecorder.replayExporting ? "Saving…" : "Save last " + Math.min(recordingPreferences.effectiveReplayClipSeconds, screenRecorder.bufferedSeconds) + " sec"
                                                onClicked: screenRecorder.saveReplayClip(recordingPreferences.effectiveReplayClipSeconds)
                                                contentItem: Label { text: parent.text; color: window.ink; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                                background: Rectangle { radius: 10; color: window.actionButtonFill(parent, "#2e4934", "#202d36"); border.color: parent.hovered ? "#8dbb62" : parent.enabled ? "#55734a" : window.line; Behavior on color { ColorAnimation { duration: 100 } } }
                                            }
                                            Button {
                                                implicitHeight: 40; leftPadding: 14; rightPadding: 14
                                                enabled: screenRecorder.frameAvailable
                                                text: "Screenshot"
                                                onClicked: screenRecorder.saveScreenshot()
                                                contentItem: Label { text: parent.text; color: window.ink; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                                background: Rectangle { radius: 9; color: window.actionButtonFill(parent, "#202833", "#1a2029"); border.color: parent.hovered ? "#627282" : window.line; Behavior on color { ColorAnimation { duration: 100 } } }
                                            }
                                            Button {
                                                implicitHeight: 40; leftPadding: 14; rightPadding: 14
                                                text: "Stop"
                                                enabled: screenRecorder.buffering
                                                onClicked: screenRecorder.stopCapture()
                                                contentItem: Label { text: parent.text; color: window.ink; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                                background: Rectangle { radius: 9; color: window.actionButtonFill(parent, "#202833", "#202833"); border.color: parent.hovered ? "#627282" : window.line; Behavior on color { ColorAnimation { duration: 100 } } }
                                            }
                                        }
                                    }
                                    Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: window.line }
                                    ColumnLayout {
                                        Layout.preferredWidth: 205; Layout.fillHeight: true; spacing: 11
                                        SectionTitle { text: "BUFFER STATUS" }
                                        Label { text: screenRecorder.buffering ? "Live" : "Off"; color: screenRecorder.buffering ? window.accent : window.muted; font.pixelSize: 26; font.bold: true }
                                        Label { Layout.fillWidth: true; text: screenRecorder.statusMessage; color: window.muted; wrapMode: Text.Wrap; font.pixelSize: 11 }
                                        Item { Layout.fillHeight: true }
                                        Label {
                                            Layout.fillWidth: true
                                            text: globalShortcuts.registered
                                                ? screenRecorder.buffering
                                                    ? "Press " + globalShortcuts.triggerDescription + " to save the replay" + (globalShortcuts.screenshotRegistered ? "; " + globalShortcuts.screenshotTriggerDescription + " saves a screenshot." : ".")
                                                    : "Moment starts replay capture when it opens. Retry screen capture here if the system permission was closed."
                                                : "Enable capture hotkeys in Preferences. Screenshot is also available here."
                                            color: "#82909f"; wrapMode: Text.Wrap; font.pixelSize: 10
                                        }
                                    }
                                }
                            }
                            Surface {
                                Layout.fillWidth: true; implicitHeight: 190
                                RowLayout {
                                    anchors.fill: parent; anchors.margins: 24; spacing: 24
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 10
                                        SectionTitle { text: "FULL SESSION" }
                                        Label { text: "Record from now"; color: window.ink; font.pixelSize: 19; font.bold: true }
                                        Label { Layout.fillWidth: true; text: "Capture a continuous screen recording. Finished files are added to your local library."; color: window.muted; wrapMode: Text.Wrap; font.pixelSize: 12 }
                                    }
                                    ColumnLayout {
                                        Layout.preferredWidth: 220; spacing: 9
                                        Button {
                                            implicitHeight: 40; leftPadding: 14; rightPadding: 14
                                            Layout.fillWidth: true; enabled: !screenRecorder.recording && !screenRecorder.replayExporting && (!screenRecorder.busy || screenRecorder.buffering)
                                            text: "Start session recording"
                                            onClicked: screenRecorder.startRecording()
                                            contentItem: Label { text: parent.text; color: "#172111"; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                            background: Rectangle { radius: 9; color: window.actionButtonFill(parent, window.accent, "#53604b"); Behavior on color { ColorAnimation { duration: 100 } } }
                                        }
                                        Button {
                                            implicitHeight: 40; leftPadding: 14; rightPadding: 14
                                            Layout.fillWidth: true; enabled: screenRecorder.recording
                                            text: "Finish and save"
                                            onClicked: screenRecorder.stopRecording()
                                            contentItem: Label { text: parent.text; color: window.ink; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                            background: Rectangle { radius: 9; color: window.actionButtonFill(parent, "#293846", "#293846"); border.color: parent.hovered ? "#627889" : window.line; Behavior on color { ColorAnimation { duration: 100 } } }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Preferences
                Item {
                    Flickable {
                        id: preferencesScroll
                        objectName: "preferencesScroll"
                        anchors.fill: parent
                        contentWidth: width
                        contentHeight: preferencesColumn.implicitHeight
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        flickableDirection: Flickable.VerticalFlick
                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                            active: preferencesScroll.moving || hovered
                        }

                    ColumnLayout {
                        id: preferencesColumn
                        width: preferencesScroll.width
                        spacing: 18
                        ColumnLayout {
                            spacing: 3
                            Label { text: "Preferences"; color: window.ink; font.pixelSize: 26; font.weight: Font.DemiBold; font.letterSpacing: -0.3 }
                            Label { text: "Recording, audio and keyboard shortcuts."; color: window.muted; font.pixelSize: 12 }
                        }
                        Surface {
                            Layout.fillWidth: true
                            id: gameProfilesCard
                            implicitHeight: gameProfilesColumn.implicitHeight + 44
                            ColumnLayout {
                                id: gameProfilesColumn
                                anchors.fill: parent; anchors.margins: 22; spacing: 14
                                RowLayout {
                                    Layout.fillWidth: true
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 4
                                        SectionTitle { text: "GAME PROFILES" }
                                        Label {
                                            Layout.fillWidth: true
                                            text: gameMonitor.activeGameName.length
                                                ? "Detected: " + gameMonitor.activeGameName + " (" + gameMonitor.activeProcessName + ")"
                                                : "No configured game is running. Moment checks local game processes."
                                            color: gameMonitor.activeGameName.length ? window.accent : window.muted
                                            font.pixelSize: 11; wrapMode: Text.Wrap
                                        }
                                    }
                                    SettingCombo {
                                        id: runningGamesCombo
                                        Layout.preferredWidth: 220
                                        model: gameMonitor.runningGameCandidates
                                        currentIndex: selectedGameProcess.length ? model.indexOf(selectedGameProcess) : (model.length ? 0 : -1)
                                        enabled: model.length > 0
                                        displayText: model.length ? currentText : "No game processes found"
                                        onActivated: selectedGameProcess = String(currentValue)
                                    }
                                    Button {
                                        text: "Add detected"
                                        enabled: gameMonitor.runningGameCandidates.length > 0
                                        onClicked: {
                                            const process = selectedGameProcess.length ? selectedGameProcess : String(runningGamesCombo.currentValue)
                                            if (gameMonitor.addProfile(process)) selectedGameProcess = ""
                                        }
                                        contentItem: Label { text: parent.text; color: parent.enabled ? "#172111" : window.muted; font.pixelSize: 10; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                        background: Rectangle { radius: 7; color: window.actionButtonFill(parent, window.accent, "#26313e"); border.color: parent.hovered ? "#ddffb2" : parent.enabled ? "#9ecb61" : window.line; Behavior on color { ColorAnimation { duration: 100 } } }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    TextField {
                                        Layout.fillWidth: true
                                        placeholderText: "Or enter executable name (for example: game.exe or game-bin)"
                                        text: manualGameProcess
                                        onTextChanged: manualGameProcess = text
                                    }
                                    Button {
                                        text: "Add manually"
                                        enabled: manualGameProcess.trim().length > 0
                                        onClicked: {
                                            if (gameMonitor.addProfile(manualGameProcess.trim())) manualGameProcess = ""
                                        }
                                        contentItem: Label { text: parent.text; color: parent.enabled ? "#172111" : window.muted; font.pixelSize: 10; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                        background: Rectangle { radius: 7; color: window.actionButtonFill(parent, window.accent, "#26313e"); border.color: parent.hovered ? "#ddffb2" : parent.enabled ? "#9ecb61" : window.line; Behavior on color { ColorAnimation { duration: 100 } } }
                                    }
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: "Profiles match the executable name. Automatic session recording starts and stops with a matching process only when enabled below."
                                    color: window.muted; font.pixelSize: 10; wrapMode: Text.Wrap
                                }
                                Repeater {
                                    model: gameMonitor
                                    delegate: Rectangle {
                                        required property int index
                                        required property string gameName
                                        required property string processName
                                        required property bool autoRecord
                                        required property int frameRate
                                        required property int quality
                                        required property int replaySeconds
                                        required property int replayClipSeconds
                                        Layout.fillWidth: true
                                        implicitHeight: 150
                                        radius: 10; color: "#101a23"; border.color: gameMonitor.activeProcessName.toLowerCase() === processName.toLowerCase() ? "#55754b" : window.line
                                        ColumnLayout {
                                            anchors.fill: parent; anchors.margins: 13; spacing: 9
                                            RowLayout {
                                                Layout.fillWidth: true
                                                ColumnLayout {
                                                    Layout.fillWidth: true; spacing: 2
                                                    Label { text: gameName; color: window.ink; font.pixelSize: 12; font.bold: true }
                                                    Label { text: processName; color: window.muted; font.pixelSize: 10 }
                                                }
                                                MomentSwitch {
                                                    text: checked ? "Auto record" : "Manual capture"
                                                    checked: autoRecord
                                                    onToggled: gameMonitor.setProfileAutoRecord(index, checked)
                                                }
                                                ToolButton {
                                                    text: "Remove"
                                                    onClicked: gameMonitor.removeProfile(index)
                                                    contentItem: Label { text: parent.text; color: "#d39481"; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                                    background: Rectangle { radius: 6; color: parent.hovered ? "#382823" : "transparent" }
                                                }
                                            }
                                            RowLayout {
                                                Layout.fillWidth: true
                                                SettingCombo {
                                                    id: profileFps
                                                    Layout.fillWidth: true; model: [30, 60]; currentIndex: frameRate === 60 ? 1 : 0
                                                    displayText: "FPS · " + currentText
                                                    onActivated: gameMonitor.setProfileSettings(index, Number(currentValue), profileQuality.currentIndex, Number(profileReplay.currentValue), Number(profileClip.currentValue))
                                                }
                                                SettingCombo {
                                                    id: profileQuality
                                                    Layout.fillWidth: true; model: ["Very low", "Low", "Standard", "High", "Very high"]; currentIndex: quality
                                                    displayText: "Quality · " + currentText
                                                    onActivated: gameMonitor.setProfileSettings(index, Number(profileFps.currentValue), currentIndex, Number(profileReplay.currentValue), Number(profileClip.currentValue))
                                                }
                                                SettingCombo {
                                                    id: profileReplay
                                                    Layout.fillWidth: true; model: [15, 30, 60, 90, 120]; currentIndex: Math.max(0, model.indexOf(replaySeconds))
                                                    displayText: "Buffer · " + currentText + "s"
                                                    onActivated: gameMonitor.setProfileSettings(index, Number(profileFps.currentValue), profileQuality.currentIndex, Number(currentValue), Number(profileClip.currentValue))
                                                }
                                                SettingCombo {
                                                    id: profileClip
                                                    Layout.fillWidth: true; model: [15, 30, 60, 90, 120]; currentIndex: Math.max(0, model.indexOf(replayClipSeconds))
                                                    displayText: "Clip · " + currentText + "s"
                                                    onActivated: gameMonitor.setProfileSettings(index, Number(profileFps.currentValue), profileQuality.currentIndex, Number(profileReplay.currentValue), Number(currentValue))
                                                }
                                            }
                                        }
                                    }
                                }
                                Label {
                                    visible: gameMonitor.profileCount === 0
                                    Layout.fillWidth: true
                                    text: "Add a running game above to create its first profile."
                                    color: window.muted; font.pixelSize: 11
                                }
                            }
                        }
                        Surface {
                            Layout.fillWidth: true
                            implicitHeight: discordPresenceColumn.implicitHeight + 44
                            ColumnLayout {
                                id: discordPresenceColumn
                                anchors.fill: parent; anchors.margins: 22; spacing: 12
                                RowLayout {
                                    Layout.fillWidth: true
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 4
                                        SectionTitle { text: "DISCORD" }
                                        Label { text: "Game presence runs automatically"; color: window.ink; font.pixelSize: 14; font.bold: true }
                                    }
                                    MomentSwitch {
                                        objectName: "discordPresenceToggle"
                                        text: checked ? "Sharing on" : "Sharing off"
                                        checked: discordPresence.enabled
                                        onToggled: discordPresence.setEnabled(checked)
                                    }
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: discordPresence.activeGameName.length
                                        ? "Discord activity: Jogando " + discordPresence.activeGameName + " com Moment"
                                        : "Discord activity: Moment aberto"
                                    color: discordPresence.activeGameName.length ? window.accent : window.muted
                                    font.pixelSize: 11; wrapMode: Text.Wrap
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: discordPresence.status
                                    color: discordPresence.connected ? window.accent : window.muted
                                    font.pixelSize: 10; wrapMode: Text.Wrap
                                }
                            }
                        }
                        Surface {
                            id: shareSettingsCard
                            objectName: "shareSettingsCard"
                            Layout.fillWidth: true
                            implicitHeight: shareSettingsColumn.implicitHeight + 44
                            color: window.shareSettingsHighlighted ? "#1c2a2b" : window.panel
                            border.width: window.shareSettingsHighlighted ? 2 : 1
                            border.color: window.shareSettingsHighlighted ? window.accent : window.line
                            Behavior on border.color { ColorAnimation { duration: 160 } }
                            Behavior on color { ColorAnimation { duration: 160 } }
                            ColumnLayout {
                                id: shareSettingsColumn
                                anchors.fill: parent; anchors.margins: 22; spacing: 10
                                RowLayout {
                                    Layout.fillWidth: true
                                    SectionTitle { text: "CLIP SHARING" }
                                    Item { Layout.fillWidth: true }
                                    Button {
                                        visible: driveShare.connected
                                        text: "Disconnect"
                                        onClicked: driveShare.disconnectAccount()
                                        contentItem: Label { text: parent.text; color: window.ink; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                        background: Rectangle { radius: 7; color: window.actionButtonFill(parent, "#1b252e", "#1b252e"); border.color: parent.hovered ? "#627889" : window.line; Behavior on color { ColorAnimation { duration: 100 } } }
                                    }
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: driveShare.connected
                                        ? "Google Drive is connected. Sharing uploads a clip to your account and creates a link anyone with it can open."
                                        : driveShare.configured
                                            ? "Your first share opens Google sign-in once. After approval, Moment uploads clips and copies each link automatically."
                                            : "Automatic sharing is ready in the app; the Moment Google OAuth registration must be configured before sign-in can start."
                                    color: window.muted; font.pixelSize: 11; wrapMode: Text.Wrap
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: driveShare.statusMessage
                                    color: driveShare.connected ? window.accent : window.muted
                                    font.pixelSize: 10; wrapMode: Text.Wrap
                                }
                            }
                        }
                        Surface {
                            Layout.fillWidth: true; implicitHeight: 420
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 22; spacing: 18
                                SectionTitle { text: "RECORDING" }
                                RowLayout {
                                    Layout.fillWidth: true
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 3
                                        Label { text: "Frame rate"; color: window.ink; font.pixelSize: 13; font.bold: true }
                                        Label { Layout.fillWidth: true; text: "Target for new captures and replay exports."; color: window.muted; font.pixelSize: 11; wrapMode: Text.Wrap }
                                    }
                                    SettingCombo { objectName: "frameRateSetting"; model: [30, 60]; currentIndex: recordingPreferences.frameRate === 60 ? 1 : 0; onActivated: recordingPreferences.frameRate = Number(currentValue) }
                                }
                                Rectangle { Layout.fillWidth: true; height: 1; color: window.line }
                                RowLayout {
                                    Layout.fillWidth: true
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 3
                                        Label { text: "Encoding quality"; color: window.ink; font.pixelSize: 13; font.bold: true }
                                        Label { Layout.fillWidth: true; text: "Balance image detail and file size."; color: window.muted; font.pixelSize: 11; wrapMode: Text.Wrap }
                                    }
                                    SettingCombo { model: ["Very low", "Low", "Standard", "High", "Very high"]; currentIndex: recordingPreferences.quality; onActivated: recordingPreferences.quality = currentIndex }
                                }
                                Rectangle { Layout.fillWidth: true; height: 1; color: window.line }
                                RowLayout {
                                    Layout.fillWidth: true
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 3
                                        Label { text: "Saved clip length"; color: window.ink; font.pixelSize: 13; font.bold: true }
                                        Label { Layout.fillWidth: true; text: "How much of the recent action to save when you press the shortcut."; color: window.muted; font.pixelSize: 11; wrapMode: Text.Wrap }
                                    }
                                    SettingCombo {
                                        model: [15, 30, 60, 90, 120]
                                        currentIndex: Math.max(0, model.indexOf(recordingPreferences.replayClipSeconds))
                                        onActivated: recordingPreferences.replayClipSeconds = Number(currentValue)
                                    }
                                }
                                Rectangle { Layout.fillWidth: true; height: 1; color: window.line }
                                RowLayout {
                                    Layout.fillWidth: true
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 3
                                        Label { text: "Replay window"; color: window.ink; font.pixelSize: 13; font.bold: true }
                                        Label { Layout.fillWidth: true; text: "How far back Moment can go when you save a replay."; color: window.muted; font.pixelSize: 11; wrapMode: Text.Wrap }
                                    }
                                    SettingCombo { model: [15, 30, 60, 90, 120]; currentIndex: Math.max(0, model.indexOf(screenRecorder.bufferSeconds)); onActivated: screenRecorder.bufferSeconds = Number(currentValue) }
                                }
                            }
                        }
                        Surface {
                            Layout.fillWidth: true; implicitHeight: 176
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 22; spacing: 20
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 6
                                    SectionTitle { text: "AUDIO INPUT" }
                                    Label { text: "Include audio in captures"; color: window.ink; font.pixelSize: 15; font.bold: true }
                                    Label {
                                        Layout.fillWidth: true
                                        text: "Choose a microphone or an available system-audio monitor. Audio is off until you enable it."
                                        color: window.muted; wrapMode: Text.Wrap; font.pixelSize: 11
                                    }
                                    Label { Layout.fillWidth: true; text: screenRecorder.audioCaptureStatus; color: window.muted; wrapMode: Text.Wrap; font.pixelSize: 10 }
                                }
                                ColumnLayout {
                                    Layout.preferredWidth: 260; spacing: 10
                                    MomentSwitch {
                                        text: checked ? "Audio enabled" : "Audio off"
                                        checked: recordingPreferences.captureAudio
                                        enabled: !screenRecorder.recording
                                        onToggled: recordingPreferences.captureAudio = checked
                                    }
                                    SettingCombo {
                                        Layout.fillWidth: true
                                        model: screenRecorder.audioInputNames
                                        currentIndex: screenRecorder.selectedAudioInputIndex
                                        enabled: recordingPreferences.captureAudio && !screenRecorder.recording && model.length > 0
                                        onActivated: screenRecorder.setAudioInputIndex(currentIndex)
                                        displayText: model.length > 0 ? currentText : "No audio inputs found"
                                    }
                                }
                            }
                        }
                        Surface {
                            Layout.fillWidth: true; implicitHeight: 120
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 22; spacing: 8
                                SectionTitle { text: "LOCAL DATA" }
                                Label { text: "Your library is stored locally. Imported files remain at their original paths; recorded sessions are saved in the Moment folder under Videos."; color: window.muted; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 11 }
                            }
                        }
                        Surface {
                            Layout.fillWidth: true; implicitHeight: 158
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 22; spacing: 24
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 7
                                    SectionTitle { text: "IN-GAME CONTROL" }
                                    Label { text: "Capture with global keys"; color: window.ink; font.pixelSize: 16; font.bold: true }
                                    Label {
                                        Layout.fillWidth: true
                                        text: globalShortcuts.portalAvailable
                                            ? globalShortcuts.statusMessage + ". The desktop remembers approved shortcuts for the next launch."
                                            : "This desktop session does not expose the XDG Global Shortcuts portal."
                                        color: window.muted; wrapMode: Text.Wrap; font.pixelSize: 11
                                    }
                                }
                                Button {
                                    implicitHeight: 40; leftPadding: 14; rightPadding: 14
                                    text: globalShortcuts.requesting ? "Restoring shortcuts…" : globalShortcuts.allRegistered ? "Replay " + globalShortcuts.triggerDescription + " · Screenshot " + globalShortcuts.screenshotTriggerDescription : globalShortcuts.registered || globalShortcuts.screenshotRegistered ? "Finish shortcut setup" : "Enable capture shortcuts"
                                    enabled: globalShortcuts.portalAvailable && !globalShortcuts.requesting && !globalShortcuts.allRegistered
                                    onClicked: globalShortcuts.requestCaptureShortcuts()
                                    contentItem: Label { text: parent.text; color: parent.enabled ? "#172111" : window.muted; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                    background: Rectangle { radius: 9; color: window.actionButtonFill(parent, window.accent, "#26313e"); Behavior on color { ColorAnimation { duration: 100 } } }
                                }
                            }
                        }
                        Label { text: "Your preferences are saved automatically on this device."; color: "#748091"; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 10 }
                    }
                    }
                }

                // Editor
                Item {
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 16
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                spacing: 4
                                Label { text: "Clip editor"; color: window.ink; font.pixelSize: 25; font.weight: Font.DemiBold; font.letterSpacing: -0.6 }
                                Label { text: "Trim a copy of your video. The original file stays untouched."; color: window.muted; font.pixelSize: 11 }
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                text: "Back to Library"; implicitHeight: 36; leftPadding: 12; rightPadding: 12
                                onClicked: window.page = 0
                                contentItem: Label { text: parent.text; color: window.ink; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle { radius: 8; color: window.actionButtonFill(parent, "#1b2934", "#1b2934"); border.color: parent.hovered ? "#627889" : window.line; Behavior on color { ColorAnimation { duration: 100 } } }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 280; spacing: 15
                            Surface {
                                Layout.fillWidth: true; Layout.fillHeight: true
                                ColumnLayout {
                                    anchors.fill: parent; anchors.margins: 16; spacing: 12
                                    RowLayout {
                                        Layout.fillWidth: true
                                        SectionTitle { text: "PREVIEW" }
                                        Item { Layout.fillWidth: true }
                                        Label { text: player.playbackState === MediaPlayer.PlayingState ? "PLAYING" : "EDITING"; color: player.playbackState === MediaPlayer.PlayingState ? window.accent : window.muted; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
                                    }
                                    Rectangle {
                                        Layout.fillWidth: true; Layout.fillHeight: true; radius: 10; color: "#0b1219"; clip: true
                                        VideoOutput { id: editorVideoOutput; anchors.fill: parent; fillMode: VideoOutput.PreserveAspectFit }
                                        Column {
                                            anchors.centerIn: parent; width: parent.width - 36; spacing: 10
                                            visible: videoEditor.sourcePath.length === 0
                                            Label { width: parent.width; text: "Choose a video from Library"; color: window.ink; font.pixelSize: 15; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter }
                                            Label { width: parent.width; text: "Select a clip, then use Edit clip to set the section you want to keep."; color: window.muted; font.pixelSize: 11; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter }
                                            Button {
                                                objectName: "editorEmptyChooseButton"
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                text: "Choose a video"
                                                implicitHeight: 36; leftPadding: 15; rightPadding: 15
                                                onClicked: editorVideoDialog.open()
                                                contentItem: Label { text: parent.text; color: "#182111"; font.pixelSize: 11; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                                background: Rectangle { radius: 8; color: window.actionButtonFill(parent, window.accent, window.accent); Behavior on color { ColorAnimation { duration: 100 } } }
                                            }
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true; spacing: 10
                                        ToolButton {
                                            text: player.playbackState === MediaPlayer.PlayingState ? "Ⅱ" : "▶"
                                            enabled: videoEditor.durationMs > 0
                                            onClicked: {
                                                if (player.playbackState === MediaPlayer.PlayingState) player.pause()
                                                else {
                                                    if (player.position < videoEditor.inMs || player.position >= videoEditor.outMs)
                                                        player.position = videoEditor.inMs
                                                    player.play()
                                                }
                                            }
                                            palette.buttonText: window.accent
                                        }
                                        MomentSlider { Layout.fillWidth: true; from: 0; to: Math.max(videoEditor.durationMs, 1); value: Math.min(player.position, to); enabled: videoEditor.durationMs > 0; onMoved: player.position = value }
                                        Label { text: window.formatTime(videoEditor.durationMs > 0 ? player.position : 0) + " / " + window.formatTime(videoEditor.durationMs); color: window.muted; font.pixelSize: 10 }
                                    }
                                }
                            }
                            Surface {
                                Layout.preferredWidth: window.width < 1100 ? 250 : 285; Layout.fillHeight: true
                                ColumnLayout {
                                    anchors.fill: parent; anchors.margins: 18; spacing: 14
                                    SectionTitle { text: "CURRENT CUT" }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        ColumnLayout {
                                            spacing: 3
                                            Label { text: "IN"; color: window.muted; font.pixelSize: 9; font.bold: true }
                                            Label { text: window.formatTime(videoEditor.inMs); color: window.accent; font.pixelSize: 15; font.bold: true }
                                        }
                                        Item { Layout.fillWidth: true }
                                        ColumnLayout {
                                            spacing: 3
                                            Label { text: "OUT"; color: window.muted; font.pixelSize: 9; font.bold: true }
                                            Label { text: window.formatTime(videoEditor.outMs); color: window.accent; font.pixelSize: 15; font.bold: true }
                                        }
                                    }
                                    SectionTitle { text: "EXPORT PROFILE" }
                                    SettingCombo {
                                        objectName: "editorExportPreset"
                                        Layout.fillWidth: true
                                        model: ["Compact · smallest file", "Balanced", "High quality", "Maximum · largest file"]
                                        currentIndex: videoEditor.exportPreset
                                        onActivated: videoEditor.setExportPreset(currentIndex)
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: videoEditor.exportPresetDescription
                                        color: window.muted; font.pixelSize: 9; wrapMode: Text.Wrap
                                    }
                                    Rectangle { Layout.fillWidth: true; height: 1; color: window.line }
                                    Label { text: "Kept section"; color: window.muted; font.pixelSize: 10 }
                                    Label { text: window.formatTime(Math.max(0, videoEditor.outMs - videoEditor.inMs)); color: window.ink; font.pixelSize: 24; font.bold: true }
                                    Label { Layout.fillWidth: true; text: "Drag either handle on the timeline. Save changes replaces the Library entry; the original stays on disk."; color: window.muted; font.pixelSize: 10; wrapMode: Text.Wrap }
                                    Label { Layout.fillWidth: true; text: videoEditor.statusMessage; color: window.muted; font.pixelSize: 10; wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight }
                                    MomentProgress { Layout.fillWidth: true; visible: videoEditor.exporting; from: 0; to: 100; value: videoEditor.exportProgress }
                                    Item { Layout.fillHeight: true }
                                    Button {
                                        Layout.fillWidth: true; implicitHeight: 46
                                        enabled: videoEditor.sourcePath.length > 0 && videoEditor.durationMs > 0 && videoEditor.outMs > videoEditor.inMs && !videoEditor.exporting
                                        text: videoEditor.exporting ? "Saving " + videoEditor.exportProgress + "%…" : "Save changes"
                                        onClicked: videoEditor.exportClip()
                                        contentItem: Label { text: parent.text; color: parent.enabled ? "#182111" : window.muted; font.pixelSize: 12; font.weight: Font.Bold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                        background: Rectangle { radius: 8; color: window.actionButtonFill(parent, window.accent, "#26313e"); Behavior on color { ColorAnimation { duration: 100 } } }
                                    }
                                    Button {
                                        Layout.fillWidth: true; visible: videoEditor.exporting; implicitHeight: 34
                                        text: "Cancel save"; onClicked: videoEditor.cancelExport()
                                        contentItem: Label { text: parent.text; color: "#e7aa9b"; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                        background: Rectangle { radius: 8; color: window.actionButtonFill(parent, "#2e2425", "#2e2425"); Behavior on color { ColorAnimation { duration: 100 } } }
                                    }
                                }
                            }
                        }
                        Surface {
                            Layout.fillWidth: true; Layout.preferredHeight: 112
                            visible: videoEditor.sourcePath.length > 0
                            ColumnLayout {
                                anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14; anchors.topMargin: 9; anchors.bottomMargin: 7; spacing: 0
                                RowLayout {
                                    Layout.fillWidth: true
                                    SectionTitle { text: "TIMELINE" }
                                    Item { Layout.fillWidth: true }
                                    Label { text: "One video track"; color: window.muted; font.pixelSize: 9 }
                                }
                                EditorTrimTimeline {
                                    Layout.fillWidth: true
                                    durationMs: videoEditor.durationMs
                                    inMs: videoEditor.inMs
                                    outMs: videoEditor.outMs
                                    playheadMs: player.position
                                    editingEnabled: videoEditor.durationMs > 0 && !videoEditor.exporting
                                    onTrimStartRequested: function(positionMs) { videoEditor.inMs = positionMs }
                                    onTrimEndRequested: function(positionMs) { videoEditor.outMs = positionMs }
                                    onSeekRequested: function(positionMs) { player.position = positionMs }
                                }
                            }
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                text: window.page === 3 ? videoEditor.statusMessage : screenRecorder.statusMessage.length ? screenRecorder.statusMessage : clipLibrary.statusMessage
                color: window.muted; font.pixelSize: 10; elide: Text.ElideRight
            }
        }
    }
}
