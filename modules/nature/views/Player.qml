import QtQuick
import Components

FocusScope {
    id: playerRoot

    property var navParams: ({})
    property string loadState: "loading"
    property string errorText: ""
    property string statusText: ""
    property var observations: []
    property bool paused: false
    property bool showingCachedData: false
    property bool showingStaleData: false
    readonly property var soundtrack: natureBackend.soundtrack
    readonly property bool usingExternalOutput: outputLease.active

    signal goBack()

    focus: true
    onPausedChanged: if (soundtrack) soundtrack.setPaused(paused)

    function currentTitle() {
        var item = montage.currentItem
        return item && item.title ? item.title : "NATURE"
    }

    function locationLine() {
        var item = montage.currentItem
        if (!item)
            return "LOCATION WITHHELD"
        var parts = []
        if (item.city)
            parts.push(item.city)
        if (item.region)
            parts.push(item.region)
        if (item.country)
            parts.push(item.country)
        return parts.length > 0 ? parts.join(", ") : "LOCATION WITHHELD"
    }

    function cacheLabel() {
        if (showingStaleData)
            return "SAVED OBSERVATIONS / REFRESHING"
        if (showingCachedData)
            return "SAVED OBSERVATIONS"
        return "LATEST OBSERVATIONS"
    }

    function setStatus(message) {
        statusText = message
        statusTimer.restart()
    }

    function showError(message) {
        loadState = "error"
        errorText = message
        paused = false
        montage.stop()
        if (soundtrack) soundtrack.stop()
    }

    function refresh() {
        if (observations.length === 0)
            loadState = "loading"
        errorText = ""
        natureBackend.refreshObservations()
    }

    function togglePause() {
        if (loadState !== "playing")
            return
        paused = !paused
        setStatus(paused ? "PAUSED" : currentTitle())
    }

    function openCurrentObservation() {
        var item = montage.currentItem
        if (item && item.observationUrl)
            Qt.openUrlExternally(item.observationUrl)
    }

    function handlePlayerKey(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back || event.key === Qt.Key_Backspace) {
            montage.stop()
            if (soundtrack) soundtrack.stop()
            goBack()
            event.accepted = true
        } else if (loadState === "error" &&
                   (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
            refresh()
            event.accepted = true
        } else if (loadState === "playing" &&
                   (event.key === Qt.Key_Right || event.key === Qt.Key_Down)) {
            montage.next()
            event.accepted = true
        } else if (loadState === "playing" && (event.key === Qt.Key_Space ||
                   event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
            togglePause()
            event.accepted = true
        } else if (loadState === "playing" && event.key === Qt.Key_R) {
            refresh()
            event.accepted = true
        } else if (loadState === "playing" && event.key === Qt.Key_I) {
            openCurrentObservation()
            event.accepted = true
        } else if (loadState === "playing" && event.key === Qt.Key_A) {
            if (soundtrack && soundtrack.sourceUrl)
                Qt.openUrlExternally(soundtrack.sourceUrl)
            event.accepted = true
        }
    }

    Keys.onPressed: function(event) { handlePlayerKey(event) }

    MediaOutputLease {
        id: outputLease
        host: root
        enabled: root.hasMediaOutputScreen
        requested: true
        opaque: true
        acceptsFocus: true
    }

    Connections {
        target: playerRoot.soundtrack
        function onStatusChanged(message) { playerRoot.setStatus(message) }
        function onInputKey(key) {
            var keys = { "UP": Qt.Key_Up, "DOWN": Qt.Key_Down,
                         "LEFT": Qt.Key_Left, "RIGHT": Qt.Key_Right,
                         "ENTER": Qt.Key_Return, "SPACE": Qt.Key_Space,
                         "ESC": Qt.Key_Escape, "q": Qt.Key_Escape }
            if (keys[key] !== undefined)
                playerRoot.handlePlayerKey({ key: keys[key], accepted: false })
        }
    }

    Connections {
        target: natureBackend

        function onRefreshStarted(hasCachedObservations) {
            if (!hasCachedObservations) {
                playerRoot.loadState = "loading"
                playerRoot.errorText = ""
            } else {
                playerRoot.setStatus("REFRESHING INATURALIST")
            }
        }

        function onObservationsLoaded(loadedObservations, fromCache, stale) {
            playerRoot.observations = loadedObservations || []
            playerRoot.showingCachedData = fromCache
            playerRoot.showingStaleData = stale
            if (playerRoot.observations.length === 0) {
                playerRoot.showError("No usable Nature observations were returned.")
                return
            }
            montage.items = playerRoot.observations
            montage.start()
            if (stale)
                playerRoot.setStatus("SHOWING SAVED OBSERVATIONS / REFRESHING")
        }

        function onLoadFailed(message, hasCachedObservations) {
            if (hasCachedObservations && playerRoot.observations.length > 0) {
                playerRoot.showingCachedData = true
                playerRoot.showingStaleData = false
                playerRoot.setStatus("REFRESH FAILED / USING SAVED OBSERVATIONS")
            } else {
                playerRoot.showError(message)
            }
        }
    }

    Connections {
        target: root
        function onMediaOutputKeyPressed(event) {
            if (playerRoot.usingExternalOutput)
                playerRoot.handlePlayerKey(event)
        }
    }

    Timer {
        id: statusTimer
        interval: 3800
        repeat: false
        onTriggered: playerRoot.statusText = ""
    }

    PlaybackControlPanel {
        anchors.fill: parent
        visible: playerRoot.usingExternalOutput
        title: "NATURE"
        subtitle: "SHOWING ON MEDIA DISPLAY"
        stateText: playerRoot.loadState === "loading" ? "LOADING INATURALIST"
                   : playerRoot.loadState === "error" ? playerRoot.errorText
                   : (playerRoot.paused ? "PAUSED / " : "") + playerRoot.currentTitle()
        footerText: "[ESC]:BACK [SPACE]:PAUSE [RIGHT]:NEXT [R]:REFRESH [I]:PHOTO [A]:SOUND"
        controls: [
            { key: "RIGHT / DOWN", action: "Next observation" },
            { key: "SPACE / ENTER", action: "Pause or resume" },
            { key: "R", action: "Refresh observations" },
            { key: "I", action: "Open on iNaturalist" },
            { key: "A", action: "Open sound on Freesound" },
            { key: "ESC / BACK", action: "Stop Nature" }
        ]
    }

    Item {
        id: outputSurface
        parent: playerRoot.usingExternalOutput ? root.mediaOutputLayer : playerRoot
        width: parent ? parent.width : playerRoot.width
        height: parent ? parent.height : playerRoot.height

        ImageMontage {
            id: montage
            anchors.fill: parent
            paused: playerRoot.paused
            showDurationMs: 7200
            transitionDurationMs: 1250

            onStarted: {
                playerRoot.loadState = "playing"
                playerRoot.errorText = ""
                if (playerRoot.soundtrack) {
                    playerRoot.soundtrack.setPaused(playerRoot.paused)
                    playerRoot.soundtrack.start()
                }
                if (!playerRoot.showingStaleData)
                    playerRoot.setStatus(playerRoot.cacheLabel())
            }
            onExhausted: playerRoot.showError("No Nature images could be displayed.")
        }

        Rectangle {
            anchors.fill: parent
            color: root.surfaceColor
            visible: playerRoot.loadState === "loading" || playerRoot.loadState === "error"
            z: 1100

            Column {
                anchors.centerIn: parent
                width: outputSurface.width * 0.76
                spacing: outputSurface.height * 0.035

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: playerRoot.loadState === "loading" ? "LOADING NATURE" : "NATURE ERROR"
                    color: root.secondaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: outputSurface.height * 0.05
                    wrapMode: Text.WordWrap
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: playerRoot.loadState === "loading"
                          ? "FETCHING OPEN-LICENSED INATURALIST OBSERVATIONS"
                          : playerRoot.errorText
                    color: root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: outputSurface.height * 0.03
                    wrapMode: Text.WordWrap
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    visible: playerRoot.loadState === "error"
                    text: "[ENTER]:RETRY [ESC]:BACK"
                    color: root.tertiaryColor
                    font.family: root.globalFont
                    font.pixelSize: outputSurface.height * 0.03
                }
            }
        }

        Rectangle {
            id: metadataPanel
            visible: playerRoot.loadState === "playing" && montage.currentIndex >= 0
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Math.max(outputSurface.height * 0.135,
                             metadataColumn.implicitHeight + outputSurface.height * 0.025)
            color: "#d9000000"
            z: 1200

            Column {
                id: metadataColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: outputSurface.width * 0.035
                anchors.rightMargin: outputSurface.width * 0.035
                spacing: outputSurface.height * 0.003

                Text {
                    width: parent.width
                    text: montage.currentItem.title || "UNKNOWN SPECIES"
                    color: "white"
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: outputSurface.height * 0.031
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: montage.currentItem.scientificName || ""
                    color: "#d9ffffff"
                    font.family: root.globalFont
                    font.italic: true
                    font.pixelSize: outputSurface.height * 0.022
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: playerRoot.locationLine()
                    color: "#f2ffffff"
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: outputSurface.height * 0.022
                    elide: Text.ElideRight
                }
            }
        }

        Rectangle {
            visible: playerRoot.statusText !== "" && playerRoot.loadState === "playing"
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.topMargin: outputSurface.height * 0.025
            anchors.rightMargin: outputSurface.width * 0.025
            width: Math.min(statusLabel.implicitWidth + outputSurface.width * 0.03,
                            outputSurface.width * 0.8)
            height: statusLabel.implicitHeight + outputSurface.height * 0.018
            color: "#d9000000"
            z: 1250

            Text {
                id: statusLabel
                anchors.centerIn: parent
                width: parent.width - outputSurface.width * 0.018
                text: playerRoot.statusText
                color: "white"
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: outputSurface.height * 0.024
                elide: Text.ElideRight
            }
        }
    }

    Component.onCompleted: {
        if (soundtrack) soundtrack.prepare()
        natureBackend.loadLatestObservations()
    }
    Component.onDestruction: {
        if (soundtrack) soundtrack.stop()
        montage.stop()
    }
}
