import QtQuick
import Components

FocusScope {
    id: diagnosticsRoot

    signal goBack()
    property var navParams: ({})
    property var navListState: ({})
    property int actionIndex: 0
    property string previewText: ""

    function refreshPreview() {
        previewText = diagnosticsManager.reportPreview()
    }

    Component.onCompleted: refreshPreview()

    Connections {
        target: diagnosticsManager
        function onStatusChanged() { diagnosticsRoot.refreshPreview() }
    }

    AppBar {
        title: "Diagnostics"
        subtitle: appCore.appVersion
        iconSource: "../../assets/images/settings.svg"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.10
        anchors.leftMargin: root.sw * 0.10
    }

    Text {
        text: "REVIEW THE SANITIZED EVENTS INCLUDED WITH A REPORT"
        color: root.secondaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        font.pixelSize: root.sh * 0.025
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: root.sw * 0.10
        anchors.topMargin: root.sh * 0.19
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: root.sw * 0.10
        anchors.topMargin: root.sh * 0.235
        width: root.sw * 0.80
        height: root.sh * 0.39
        color: "transparent"
        border.color: root.tertiaryColor
        border.width: 1
        clip: true

        Text {
            anchors.fill: parent
            anchors.margins: root.sw * 0.0125
            text: diagnosticsRoot.previewText
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0208
            wrapMode: Text.WrapAnywhere
            elide: Text.ElideRight
            maximumLineCount: 13
        }
    }

    Row {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: root.sw * 0.10
        anchors.topMargin: root.sh * 0.66
        spacing: root.sw * 0.025

        Repeater {
            model: ["SEND REPORT", "CLEAR LOCAL LOG"]
            delegate: Rectangle {
                width: root.sw * 0.31
                height: root.sh * 0.065
                color: diagnosticsRoot.actionIndex === index ? root.accentColor : "transparent"
                border.color: diagnosticsRoot.actionIndex === index ? root.accentColor : root.tertiaryColor
                Text {
                    anchors.centerIn: parent
                    text: modelData
                    color: diagnosticsRoot.actionIndex === index ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.029
                }
            }
        }
    }

    Text {
        text: diagnosticsManager.status
        color: root.secondaryColor
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.027
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: root.sw * 0.10
        anchors.topMargin: root.sh * 0.76
    }

    Text {
        text: "REPORTS ARE SENT ONLY WHEN YOU SELECT SEND. MEDIA, TOKENS, URLS AND PATHS ARE EXCLUDED."
        color: root.tertiaryColor
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.021
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.sw * 0.10
        anchors.bottomMargin: root.sh * 0.12
    }

    Text {
        text: "[ESC]:BACK [LEFT/RIGHT]:SELECT [ENTER]:ACTIVATE"
        color: root.tertiaryColor
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.027
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.sw * 0.10
        anchors.bottomMargin: root.sh * 0.065
    }

    Keys.onLeftPressed: actionIndex = 0
    Keys.onRightPressed: actionIndex = 1
    Keys.onReturnPressed: {
        if (actionIndex === 0)
            diagnosticsManager.submitReport()
        else
            diagnosticsManager.clearLogs()
    }
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            diagnosticsRoot.goBack()
            event.accepted = true
        }
    }
}
