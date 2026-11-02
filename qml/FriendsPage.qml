import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: root
    property var client
    property color ink: "#f2f5f8"
    property color muted: "#94a6b7"
    property color panel: "#171e26"
    property color line: "#2a343f"
    property color accent: "#b5ec72"
    property var translate: function(text) { return text }

    function tr(text) { return translate(text) }

    FileDialog {
        id: videoDialog
        title: root.tr("Choose a video to send")
        fileMode: FileDialog.OpenFile
        nameFilters: ["Videos (*.mp4 *.webm *.mov *.mkv)", "All files (*)"]
        onAccepted: root.client.sendClip(selectedFile)
    }

    component Card: Rectangle {
        color: root.panel
        radius: 10
        border.color: root.line
    }
    component ActionButton: Button {
        implicitHeight: 38
        contentItem: Label {
            text: parent.text
            color: parent.enabled ? root.ink : root.muted
            font.pixelSize: 11
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 7
            color: parent.hovered ? "#2b3f36" : "#22352c"
            border.color: parent.hovered ? root.accent : "#405d45"
            Behavior on color { ColorAnimation { duration: 100 } }
        }
    }

    Dialog {
        id: recoveryDialog
        title: root.tr("Save your recovery key")
        modal: true
        anchors.centerIn: parent
        width: Math.min(root.width - 30, 520)
        visible: root.client && root.client.recoveryKey.length > 0
        onClosed: root.client.dismissRecoveryKey()
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: root.tr("This key is shown once. Store it somewhere safe; it is required to sign in again.")
                color: root.muted
                wrapMode: Text.Wrap
            }
            TextField {
                Layout.fillWidth: true
                readOnly: true
                selectByMouse: true
                text: root.client ? root.client.recoveryKey : ""
                font.family: "monospace"
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                ActionButton { text: root.tr("Copy key"); onClicked: root.client.copyRecoveryKey() }
                ActionButton { text: root.tr("I saved it"); onClicked: recoveryDialog.close() }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 18

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 5
                Label { text: root.tr("Friends"); color: root.ink; font.pixelSize: 26; font.weight: Font.DemiBold }
                Label { text: root.tr("Share videos while friends are offline · 20 MiB max · deleted after 24 hours"); color: root.muted; font.pixelSize: 11 }
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                visible: root.client && root.client.signedIn
                text: root.tr("Refresh")
                onClicked: root.client.refresh()
            }
        }

        Card {
            visible: !root.client || !root.client.configured
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 60, 520)
                spacing: 12
                Label { Layout.fillWidth: true; text: root.tr("Moment Friends is not online yet"); color: root.ink; font.pixelSize: 20; font.bold: true; horizontalAlignment: Text.AlignHCenter }
                Label {
                    Layout.fillWidth: true
                    text: root.tr("The friends service is not configured in this build. Accounts, friend requests and video sharing will activate after the Moment service is deployed.")
                    color: root.muted; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
                }
                Label { Layout.fillWidth: true; text: root.client ? root.tr(root.client.statusMessage) : ""; color: root.muted; font.pixelSize: 10; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter }
            }
        }

        Card {
            visible: root.client && root.client.configured && !root.client.signedIn
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 60, 440)
                spacing: 12
                Label { text: root.tr("Your Moment account"); color: root.ink; font.pixelSize: 20; font.bold: true }
                Label { Layout.fillWidth: true; text: root.tr("Create an account or sign in with your recovery key. Your key is stored in the system keychain on this device."); color: root.muted; wrapMode: Text.Wrap }
                TextField { id: username; Layout.fillWidth: true; placeholderText: root.tr("Username") }
                TextField { id: recoveryKey; Layout.fillWidth: true; placeholderText: root.tr("64-character recovery key"); echoMode: TextInput.Password; visible: signInMode.checked }
                CheckBox { id: signInMode; text: root.tr("I already have an account"); palette.text: root.ink }
                RowLayout {
                    Layout.fillWidth: true
                    ActionButton {
                        Layout.fillWidth: true
                        enabled: !root.client.busy && username.text.trim().length >= 3
                        text: root.tr(signInMode.checked ? "Sign in" : "Create account")
                        onClicked: signInMode.checked
                            ? root.client.signIn(username.text, recoveryKey.text)
                            : root.client.createAccount(username.text)
                    }
                }
                Label { Layout.fillWidth: true; text: root.tr(root.client.statusMessage); color: root.muted; wrapMode: Text.Wrap; font.pixelSize: 10 }
            }
        }

        RowLayout {
            visible: root.client && root.client.configured && root.client.signedIn
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            Card {
                Layout.preferredWidth: 330
                Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 3
                            Label { text: root.client.username; color: root.ink; font.pixelSize: 15; font.bold: true }
                            Label { text: root.tr("Friend code") + ": " + root.client.friendCode; color: root.accent; font.pixelSize: 10; font.family: "monospace"; wrapMode: Text.WrapAnywhere }
                        }
                        ToolButton { text: "⎘"; onClicked: root.client.copyFriendCode() }
                        ToolButton { text: "↪"; enabled: !root.client.busy; ToolTip.visible: hovered; ToolTip.text: root.tr("Sign out"); onClicked: root.client.signOut() }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField { id: addFriendCode; Layout.fillWidth: true; placeholderText: root.tr("Friend code"); maximumLength: 8; onAccepted: { root.client.addFriend(text); text = "" } }
                        ActionButton { text: root.tr("Add"); enabled: addFriendCode.text.length === 8; onClicked: { root.client.addFriend(addFriendCode.text); addFriendCode.text = "" } }
                    }
                    Label { text: root.tr("FRIEND REQUESTS"); color: root.muted; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
                    ListView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(150, contentHeight)
                        clip: true; spacing: 5
                        model: root.client.incomingRequests
                        delegate: Rectangle {
                            required property var modelData
                            width: ListView.view.width; height: 58; radius: 8; color: "#1b2934"
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 8; spacing: 5
                                Label { Layout.fillWidth: true; text: modelData.user.username; color: root.ink; elide: Text.ElideRight }
                                ToolButton { text: "✓"; onClicked: root.client.acceptRequest(modelData.requestId) }
                                ToolButton { text: "×"; onClicked: root.client.declineRequest(modelData.requestId) }
                            }
                        }
                        Label { anchors.centerIn: parent; visible: root.client.incomingRequests.length === 0; text: root.tr("No pending requests"); color: root.muted; font.pixelSize: 10 }
                    }
                    Label { text: root.tr("FRIENDS"); color: root.muted; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
                    ListView {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true; spacing: 5; model: root.client.friends
                        delegate: Rectangle {
                            required property var modelData
                            width: ListView.view.width; height: 56; radius: 8
                            color: root.client.selectedFriendId === modelData.id ? "#22352c" : friendMouse.containsMouse ? "#202f39" : "#17242f"
                            border.color: root.client.selectedFriendId === modelData.id ? "#496747" : "transparent"
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 10; spacing: 9
                                Rectangle { width: 8; height: 8; radius: 4; color: modelData.online ? root.accent : "#617383" }
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 2
                                    Label { Layout.fillWidth: true; text: modelData.username; color: root.ink; elide: Text.ElideRight }
                                    Label { text: root.tr(modelData.online ? "Online" : "Offline"); color: root.muted; font.pixelSize: 9 }
                                }
                            }
                            MouseArea { id: friendMouse; anchors.fill: parent; hoverEnabled: true; onClicked: root.client.selectFriend(modelData.id) }
                        }
                        Label { anchors.centerIn: parent; visible: root.client.friends.length === 0; text: root.tr("Add a friend with their code to share videos."); color: root.muted; font.pixelSize: 10; wrapMode: Text.Wrap; width: parent.width - 24; horizontalAlignment: Text.AlignHCenter }
                    }
                }
            }

            Card {
                Layout.fillWidth: true; Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        Label { Layout.fillWidth: true; text: root.client.selectedFriendName.length ? root.client.selectedFriendName : root.tr("Select a friend"); color: root.ink; font.pixelSize: 16; font.bold: true }
                        Label { text: root.tr("Videos expire after 24 hours"); color: root.muted; font.pixelSize: 9 }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.line }
                    ListView {
                        id: conversation
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true; spacing: 8; model: root.client.clips
                        onCountChanged: Qt.callLater(function() { conversation.positionViewAtEnd() })
                        delegate: Rectangle {
                            required property var modelData
                            width: Math.min(conversation.width, clipLabel.implicitWidth + 28)
                            height: clipColumn.implicitHeight + 18
                            anchors.left: modelData.sender_id === root.client.selectedFriendId ? parent.left : undefined
                            anchors.right: modelData.sender_id !== root.client.selectedFriendId ? parent.right : undefined
                            radius: 9; color: "#263a2d"
                            ColumnLayout {
                                id: clipColumn
                                anchors.fill: parent; anchors.margins: 10; spacing: 7
                                Label {
                                    id: clipLabel
                                    Layout.fillWidth: true
                                    text: root.tr("Video") + ": " + modelData.clip_name + " · " + Math.ceil(modelData.clip_size / 1048576) + " MiB"
                                    color: root.ink; wrapMode: Text.Wrap; font.pixelSize: 11
                                }
                                RowLayout {
                                    visible: true
                                    Layout.fillWidth: true
                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.delivery_status === "available"
                                            ? root.tr("Available until") + " " + modelData.expires_at
                                            : root.tr("Video expired or removed")
                                        color: root.muted; font.pixelSize: 9; elide: Text.ElideMiddle
                                    }
                                    ActionButton {
                                        visible: modelData.delivery_status === "available"
                                        text: root.tr("Download")
                                        implicitHeight: 29
                                        onClicked: root.client.downloadClip(modelData.id)
                                    }
                                }
                            }
                        }
                        Label { anchors.centerIn: parent; visible: root.client.selectedFriendId.length === 0; text: root.tr("Choose a friend to open the conversation."); color: root.muted; font.pixelSize: 11 }
                        Label { anchors.centerIn: parent; visible: root.client.selectedFriendId.length > 0 && root.client.clips.length === 0; text: root.tr("No videos shared with this friend yet."); color: root.muted; font.pixelSize: 11 }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        enabled: root.client.selectedFriendId.length > 0
                        ActionButton {
                            text: root.tr("Send video")
                            enabled: root.client.selectedFriendId.length > 0 && !root.client.busy
                            onClicked: videoDialog.open()
                        }
                    }
                    Label { Layout.fillWidth: true; visible: root.client.transferStatus.length > 0; text: root.tr(root.client.transferStatus) + (root.client.transferProgress > 0 && root.client.transferProgress < 100 ? " · " + root.client.transferProgress + "%" : ""); color: root.accent; font.pixelSize: 9; elide: Text.ElideRight }
                    Label { Layout.fillWidth: true; text: root.tr(root.client.statusMessage); color: root.muted; font.pixelSize: 9; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight }
                }
            }
        }
    }
}
