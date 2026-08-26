import QtQuick
import Components

FocusScope {
    id: playerRoot

    property var navParams: ({})
    property string playlistPath: navParams.playlistPath || ""
    property var entries: navParams.entries || []
    property int startIndex: navParams.startIndex || 0
    property string repeatMode: navParams.repeatMode || "off"
    property bool shuffled: navParams.shuffled || false
    property var soundtrackPaths: navParams.soundtrackPaths || []
    property bool hasSoundtrack: navParams.muteMainAudio !== undefined
                                ? navParams.muteMainAudio === true
                                : soundtrackPaths.length > 0

    property int currentIndex: Math.min(Math.max(0, startIndex), Math.max(0, entries.length - 1))
    property int lastPositionMs: 0
    property int lastDurationMs: 0
    property bool stopping: false
    property bool playbackStarted: false
    property bool waitingForSoundtrack: false
    property bool mainItemLoaded: false
    property int pendingStartMs: 0
    property bool overlayVisible: false
    property int choiceIndex: 0
    property var choices: []
    property string resumeSetting: "ask"
    property string subtitleMode: "forced"
    property var subtitleLanguages: []
    property int imageDurationSeconds: 5
    property int automaticSubtitleTrack: subtitleMode === "on" ? 0
                                           : subtitleMode === "forced" ? -1 : -2

    signal goBack()

    function currentEntry() {
        return currentIndex >= 0 && currentIndex < entries.length ? entries[currentIndex] : null
    }

    function currentEntryPath() {
        var entry = currentEntry()
        return entry ? (entry.filePath || "") : ""
    }

    function repeatLabel() {
        return repeatMode === "queue" ? "REPEAT QUEUE"
             : repeatMode === "one" ? "REPEAT ONE" : "REPEAT OFF"
    }

    function applyTracks(index) {
        if (index < 0 || index >= entries.length)
            return
        var entry = entries[index]
        localFilesBackend.resetQueueEntry("media", entry.entryId || "")
        var subtitleTrack = entry.subtitleExplicit ? entry.subtitleTrack : automaticSubtitleTrack
        var subtitleFiles = entry.subtitleExplicit ? (entry.subtitleFiles || []) : []
        mpvController.selectPlaybackTracks(entry.audioTrack || 0, subtitleTrack, subtitleFiles)
    }

    function startMainPlayback(startMs) {
        if (playbackStarted || stopping)
            return
        playbackStarted = true
        mainItemLoaded = false
        waitingForSoundtrack = false
        soundtrackStartupTimer.stop()
        overlayVisible = false
        mpvController.loadAndPlayWithOptions(playlistPath, {
            startSeconds: startMs > 0 ? startMs / 1000.0 : 0,
            playlistStart: currentIndex,
            repeatMode: repeatMode,
            subtitleTrack: automaticSubtitleTrack,
            subtitleLanguages: subtitleLanguages,
            imageDurationSeconds: imageDurationSeconds,
            muteAudio: hasSoundtrack
        })
    }

    function launchPlayback(startMs) {
        if (playbackStarted || waitingForSoundtrack)
            return
        overlayVisible = false
        if (soundtrackPaths.length > 0) {
            pendingStartMs = startMs
            waitingForSoundtrack = true
            soundtrackStartupTimer.restart()
            localFilesBackend.startAudio(soundtrackPaths, false)
            return
        }
        startMainPlayback(startMs)
    }

    function chooseResumeBehavior() {
        var entry = currentEntry()
        if (!entry) {
            goBack()
            return
        }
        var saved = localFilesBackend.getSavedPosition(entry.filePath || "")
        var savedPosition = saved.pos || 0
        if (resumeSetting === "yes" && savedPosition > 0) {
            launchPlayback(savedPosition)
        } else if (resumeSetting === "ask" && savedPosition > 0) {
            choices = [
                { label: "Resume from " + formatTime(savedPosition), startMs: savedPosition },
                { label: "Start from the beginning", startMs: 0 }
            ]
            choiceIndex = 0
            overlayVisible = true
        } else {
            launchPlayback(0)
        }
    }

    function requestStop() {
        if (stopping)
            return
        stopping = true
        mainItemLoaded = false
        mpvController.clearSoundtrackOverlay()
        if (waitingForSoundtrack) {
            soundtrackStartupTimer.stop()
            localFilesBackend.stopAudio()
            goBack()
            return
        }
        mpvController.stop()
    }

    function saveCurrentPosition(reason) {
        var entry = currentEntry()
        if (!entry)
            return
        if (reason === "eof" || (lastDurationMs > 0 && lastPositionMs >= lastDurationMs * 0.95))
            localFilesBackend.clearPosition(entry.filePath || "")
        else if (lastPositionMs > 5000)
            localFilesBackend.savePosition(entry.filePath || "", lastPositionMs, -1)
    }

    focus: true
    Keys.onPressed: function(event) {
        if (overlayVisible) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                goBack()
            } else if (event.key === Qt.Key_Up) {
                choiceIndex = Math.max(0, choiceIndex - 1)
            } else if (event.key === Qt.Key_Down) {
                choiceIndex = Math.min(choices.length - 1, choiceIndex + 1)
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                launchPlayback(choices[choiceIndex].startMs)
            } else {
                return
            }
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back || event.key === Qt.Key_Backspace) {
            requestStop()
        } else if (event.key === Qt.Key_Up) {
            mpvController.sendKey("UP")
        } else if (event.key === Qt.Key_Down) {
            mpvController.sendKey("DOWN")
        } else if (event.key === Qt.Key_Left) {
            mpvController.sendKey("LEFT")
        } else if (event.key === Qt.Key_Right) {
            mpvController.sendKey("RIGHT")
        } else if (event.key === Qt.Key_Space) {
            mpvController.sendKey("SPACE")
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            mpvController.sendKey("ENTER")
        } else {
            return
        }
        event.accepted = true
    }

    Connections {
        target: mpvController

        function onPositionChanged(position) {
            if (position >= 0)
                lastPositionMs = position
        }
        function onDurationChanged(duration) {
            if (duration >= 0)
                lastDurationMs = duration
        }
        function onPlaylistPosChanged(position) {
            if (position >= 0 && position < entries.length && position !== currentIndex) {
                currentIndex = position
                mainItemLoaded = false
                mpvController.clearSoundtrackOverlay()
                lastPositionMs = 0
                lastDurationMs = 0
            }
        }
        function onPlaybackItemLoaded(playlistIndex) {
            if (playlistIndex >= 0 && playlistIndex < entries.length)
                currentIndex = playlistIndex
            mainItemLoaded = true
            applyTracks(currentIndex)
            moduleRoot.showSoundtrackOverlay(currentEntryPath(),
                                             localFilesBackend.currentSoundtrack())
        }
        function onPlaybackItemEnded(playlistIndex, reason, error) {
            mainItemLoaded = false
            mpvController.clearSoundtrackOverlay()
            if (playlistIndex < 0 || playlistIndex >= entries.length || stopping)
                return
            var entry = entries[playlistIndex]
            if (reason === "eof") {
                localFilesBackend.clearPosition(entry.filePath || "")
            } else if (reason !== "redirect" && reason !== "stop" && reason !== "quit") {
                localFilesBackend.failQueueEntry("media", entry.entryId || "",
                    error || "mpv could not play this local file")
            }
        }
        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            mainItemLoaded = false
            mpvController.clearSoundtrackOverlay()
            if (finalPositionMs > 0)
                lastPositionMs = finalPositionMs
            if (finalDurationMs > 0)
                lastDurationMs = finalDurationMs
            saveCurrentPosition(reason)
            localFilesBackend.stopAudio()
            goBack()
        }
    }

    Connections {
        target: localFilesBackend

        function onAudioPlaybackStarted() {
            if (waitingForSoundtrack)
                startMainPlayback(pendingStartMs)
        }

        function onAudioPlaybackFailed() {
            if (waitingForSoundtrack)
                startMainPlayback(pendingStartMs)
        }

        function onAudioTrackStarted(track) {
            if (mainItemLoaded)
                moduleRoot.showSoundtrackOverlay(currentEntryPath(), track)
        }
    }

    Timer {
        id: soundtrackStartupTimer
        interval: 45000
        repeat: false
        onTriggered: {
            if (waitingForSoundtrack)
                startMainPlayback(pendingStartMs)
        }
    }

    PlaybackControlPanel {
        anchors.fill: parent
        title: currentEntry() ? (currentEntry().displayTitle || "LOCAL QUEUE") : "LOCAL QUEUE"
        subtitle: root.hasMediaOutputScreen ? "PLAYING QUEUE ON MEDIA DISPLAY" : "PLAYING LOCAL QUEUE"
        stateText: (waitingForSoundtrack ? "LOADING SOUNDTRACK - " : "") +
                   (currentIndex + 1) + " / " + entries.length + " - " + repeatLabel() +
                   (shuffled ? " - SHUFFLED" : "") +
                   (hasSoundtrack ? " - SOUNDTRACK" : "") +
                   (lastDurationMs > 0 ? " - " + formatTime(lastPositionMs) + " / " +
                                         formatTime(lastDurationMs) : "")
        footerText: "[ESC]:STOP [SPACE]:PAUSE [ARROWS]:SEEK"
        controls: [
            { key: "SPACE / ENTER", action: "Pause or resume" },
            { key: "LEFT / RIGHT", action: "Seek" },
            { key: "UP / DOWN", action: "Adjust playback" },
            { key: "ESC / BACK", action: "Stop and save position" }
        ]
    }

    Rectangle {
        anchors.fill: parent
        color: root.surfaceColor
        visible: overlayVisible

        Column {
            anchors.centerIn: parent
            width: root.sw * 0.76875
            spacing: root.sh * 0.0333333

            Text {
                text: "RESUME QUEUE PLAYBACK?"
                color: root.secondaryColor
                font.family: root.globalFont
                anchors.horizontalCenter: parent.horizontalCenter
                font.pixelSize: root.sh * 0.0333333
            }

            Repeater {
                model: choices
                delegate: Item {
                    width: parent.width
                    height: root.sh * 0.0583333
                    Rectangle {
                        anchors.fill: choiceText
                        color: root.accentColor
                        visible: index === choiceIndex
                    }
                    Text {
                        id: choiceText
                        anchors.centerIn: parent
                        text: modelData.label
                        color: index === choiceIndex ? root.surfaceColor : root.primaryColor
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        leftPadding: root.sw * 0.009375
                        rightPadding: root.sw * 0.009375
                        font.pixelSize: root.sh * 0.0416667
                    }
                }
            }
        }
    }

    function formatTime(milliseconds) {
        var totalSeconds = Math.floor(milliseconds / 1000)
        var hours = Math.floor(totalSeconds / 3600)
        var minutes = Math.floor((totalSeconds % 3600) / 60)
        var seconds = totalSeconds % 60
        if (hours > 0)
            return hours + ":" + pad(minutes) + ":" + pad(seconds)
        return minutes + ":" + pad(seconds)
    }

    function pad(value) { return value < 10 ? "0" + value : "" + value }

    Component.onCompleted: {
        if (playlistPath === "" || entries.length === 0) {
            goBack()
            return
        }
        resumeSetting = appCore.get_setting(moduleRoot.moduleId, "resume_playback") || "ask"
        var subtitleRaw = appCore.get_setting(moduleRoot.moduleId, "auto_subtitles")
        subtitleMode = typeof subtitleRaw === "boolean"
                     ? (subtitleRaw ? "on" : "forced") : (subtitleRaw || "forced")
        var language = appCore.get_setting(moduleRoot.moduleId, "sub_lang") || "-"
        subtitleLanguages = language === "-" ? [] : [language]
        var duration = parseInt(appCore.get_setting(moduleRoot.moduleId, "image_duration") || "5")
        imageDurationSeconds = isNaN(duration) ? 5 : duration
        chooseResumeBehavior()
    }
}
