import QtQuick
import Components

FocusScope {
    id: remapRoot

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})
    property var navListState: ({})
    property var rows: []
    property string captureMessage: ""

    function rebuildRows() {
        var updated = []
        var actions = inputManager.remappableActions
        for (var i = 0; i < actions.length; i++)
            updated.push(actions[i])
        updated.push({ id: "reset", label: "Reset to Defaults", isReset: true })
        rows = updated
        if (controlsList.currentIndex < 0 || controlsList.currentIndex >= rows.length)
            controlsList.currentIndex = 0
    }

    function selectedLabel() {
        var row = rows[controlsList.currentIndex]
        return row && !row.isReset ? row.label.toUpperCase() : "CONTROL"
    }

    Component.onCompleted: {
        rebuildRows()
        controlsList.forceActiveFocus()
    }
    Component.onDestruction: inputManager.cancelRemapCapture()

    Connections {
        target: inputManager
        function onRemappingsChanged() { remapRoot.rebuildRows() }
        function onRemapCaptureActiveChanged() {
            if (!inputManager.remapCaptureActive) {
                remapRoot.captureMessage = ""
                controlsList.forceActiveFocus()
            }
        }
        function onRemapCaptureRejected(reason) { remapRoot.captureMessage = reason }
    }

    AppBar {
        iconSource: "../../assets/images/keyboard.svg"
        title: "Controls"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
    }

    ListView {
        id: controlsList
        objectName: "controlsList"
        model: remapRoot.rows
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.76875
        height: root.sh * 0.525
        clip: true
        focus: !inputManager.remapCaptureActive

        Keys.onUpPressed: currentIndex = (currentIndex - 1 + rows.length) % rows.length
        Keys.onDownPressed: currentIndex = (currentIndex + 1) % rows.length
        Keys.onReturnPressed: {
            var row = remapRoot.rows[currentIndex]
            if (!row)
                return
            if (row.isReset) {
                inputManager.resetRemappings()
            } else {
                remapRoot.captureMessage = ""
                inputManager.startRemapCapture(row.id)
            }
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace ||
                    event.key === Qt.Key_Back) {
                remapRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            width: controlsList.width
            height: root.sh * 0.0583333

            Rectangle {
                anchors.fill: parent
                color: controlsList.currentIndex === index ? root.accentColor : "transparent"

                Text {
                    text: modelData.label || ""
                    color: controlsList.currentIndex === index ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    anchors.verticalCenter: parent.verticalCenter
                    leftPadding: root.sw * 0.009375
                    font.pixelSize: root.sh * 0.05
                }

                Text {
                    visible: !modelData.isReset
                    text: modelData.value || ""
                    color: controlsList.currentIndex === index ? root.surfaceColor : root.tertiaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: root.sw * 0.009375
                    font.pixelSize: root.sh * 0.0375
                }
            }
        }
    }

    Text {
        visible: !inputManager.remapCaptureActive
        text: "ADD ONE KEY OR REMOTE BUTTON TO EACH ACTION\nBUILT-IN CONTROLS ALWAYS KEEP WORKING"
        color: root.secondaryColor
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.0291667
        horizontalAlignment: Text.AlignHCenter
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: root.sh * 0.1583333
    }

    Text {
        visible: !inputManager.remapCaptureActive
        text: root.hints.back + ":BACK " + root.hints.navigate + ":NAVIGATE " + root.hints.select + ":SET"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
    }

    Rectangle {
        objectName: "captureOverlay"
        anchors.fill: parent
        color: root.surfaceColor
        visible: inputManager.remapCaptureActive
        focus: inputManager.remapCaptureActive

        Keys.onPressed: function(event) {
            if (!event.isAutoRepeat && (event.key === Qt.Key_Escape ||
                    event.key === Qt.Key_Backspace || event.key === Qt.Key_Back))
                inputManager.cancelRemapCapture()
            event.accepted = true
        }

        Column {
            anchors.centerIn: parent
            spacing: root.sh * 0.025

            Text {
                text: "PRESS A KEY OR REMOTE BUTTON FOR [" + remapRoot.selectedLabel() + "]"
                color: root.accentColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.05
                anchors.horizontalCenter: parent.horizontalCenter
            }
            Text {
                visible: remapRoot.captureMessage !== ""
                text: remapRoot.captureMessage
                color: root.primaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0291667
                anchors.horizontalCenter: parent.horizontalCenter
            }
            Text {
                text: root.hints.back + ":CANCEL"
                color: root.tertiaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0333333
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
    }
}
