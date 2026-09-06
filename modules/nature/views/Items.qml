import QtQuick
import Components

FocusScope {
    id: itemsRoot

    property var navParams: ({})
    signal navigateTo(string path, var params, var listState)
    signal goBack()

    focus: true

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back || event.key === Qt.Key_Backspace) {
            goBack()
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter ||
                   event.key === Qt.Key_Space) {
            navigateTo("Player.qml", {}, {})
            event.accepted = true
        }
    }

    AppBar {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
    }

    Text {
        id: description
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.225
        anchors.leftMargin: root.sw * 0.125
        width: root.sw * 0.72
        text: "RECENT RESEARCH-GRADE WILDLIFE OBSERVATIONS"
        color: root.primaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        font.pixelSize: root.sh * 0.0458333
        wrapMode: Text.WordWrap
    }

    Text {
        anchors.top: description.bottom
        anchors.left: description.left
        anchors.topMargin: root.sh * 0.025
        width: description.width
        text: "CC0 PHOTOS FROM INATURALIST, WITH NATURE SOUNDS CURATED BY EARTH GARDEN FROM FREESOUND. RECORDINGS BLEND GENTLY AS THE SLIDESHOW PLAYS."
        color: root.tertiaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        font.pixelSize: root.sh * 0.0291667
        lineHeight: 1.2
        wrapMode: Text.WordWrap
    }

    Rectangle {
        id: startButton
        anchors.left: description.left
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.sh * 0.1833333
        width: root.sw * 0.37
        height: root.sh * 0.0666667
        color: root.accentColor

        Text {
            anchors.centerIn: parent
            text: "START NATURE"
            color: root.surfaceColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.0416667
        }
    }

    Text {
        anchors.left: startButton.left
        anchors.top: startButton.bottom
        anchors.topMargin: root.sh * 0.025
        text: "[ENTER]:START  [ESC]:BACK"
        color: root.tertiaryColor
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.0291667
    }
}
