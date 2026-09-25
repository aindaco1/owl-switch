import QtQuick
import Components

FocusScope {
    id: updateRoot

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})
    property var navListState: ({})

    function primaryLabel() {
        if (updateManager.state === "checking") return "Checking..."
        return updateManager.updateAvailable ? "Review Update" : "Check for Updates"
    }

    function activatePrimary() {
        updateManager.checkForUpdates()
    }

    Keys.onReturnPressed: activatePrimary()
    Keys.onEnterPressed: activatePrimary()
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            updateRoot.goBack()
            event.accepted = true
        }
    }

    AppBar {
        iconSource: "../../assets/images/settings.svg"
        title: "Software Update"
        subtitle: "Installed " + updateManager.currentVersion
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
    }

    Column {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.2583333
        anchors.leftMargin: root.sw * 0.125
        width: root.sw * 0.75
        spacing: root.sh * 0.025

        Text {
            width: parent.width
            text: updateManager.statusMessage
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.05
            wrapMode: Text.Wrap
        }

        Text {
            width: parent.width
            text: "Download, installation and restart require your approval in the update window."
            color: root.secondaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.025
            wrapMode: Text.Wrap
        }

        Rectangle {
            width: actionText.width + root.sw * 0.0375
            height: root.sh * 0.0708333
            color: updateManager.state === "checking"
                   ? root.tertiaryColor : root.accentColor

            Text {
                id: actionText
                anchors.centerIn: parent
                text: updateRoot.primaryLabel()
                color: root.surfaceColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.0416667
            }
        }
    }

    Text {
        text: "[ESC]:BACK [ENTER]:SELECT"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
    }
}
