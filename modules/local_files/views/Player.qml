import QtQuick
import Components

FocusScope {
    id: playerRoot

    property var navParams: ({})

    signal goBack()

    property string filePath:    navParams.filePath || ""
    property string itemTitle:   navParams.title    || ""
    property int    audioTrack:  navParams.audioTrack || 0
    property int    subtitleTrack: (navParams.subtitleTrack !== undefined && navParams.subtitleTrack !== null)
                                  ? navParams.subtitleTrack : -1
    property var    subtitleFiles: navParams.subtitleFiles || []
    property bool   subtitleExplicit: navParams.subtitleExplicit || false

    property bool   overlayVisible:      false
    property int    choiceIndex:         0
    property var    choices:             []
    property string repeatMode:          "off"
    property string shuffleSetting:      "ask"
    property string resumeSetting:       "ask"
    property string subtitleMode:        "forced"
    property var    subtitleLanguages:   []
    property int    imageDurationSeconds: 5
    property bool   imageContent:        false
    property var    soundtrackPaths:     []
    property bool   hasSoundtrack:       soundtrackPaths.length > 0 && !localFilesBackend.isAudio(filePath)
    property bool   mainItemLoaded:      false
    property bool   overlayShownForItem: false
    property string currentItemPath:     ""

    property int automaticSubtitleTrack: subtitleMode === "on" ? 0
                                            : subtitleMode === "forced" ? -1 : -2

    // Track last non-null values during playback for robust save on exit
    property int    lastKnownPositionMs:  0
    property int    lastKnownDurationMs:  0
    property int    lastKnownPlaylistPos: -1

    focus: true

    function launchPlayback(startMs, playlistStart, useShuffle) {
        mainItemLoaded = false
        overlayShownForItem = false
        currentItemPath = ""
        mpvController.clearTrackOverlay()
        mpvController.loadAndPlayWithOptions(filePath, {
            startSeconds: startMs > 0 ? startMs / 1000.0 : 0,
            audioTrack: audioTrack,
            subtitleTrack: subtitleExplicit ? subtitleTrack : automaticSubtitleTrack,
            subtitleFiles: subtitleExplicit ? subtitleFiles : [],
            subtitleLanguages: subtitleLanguages,
            repeatMode: repeatMode,
            playlistStart: playlistStart,
            shuffle: useShuffle || false,
            imageDurationSeconds: imageDurationSeconds,
            muteAudio: hasSoundtrack
        })
        if (hasSoundtrack)
            localFilesBackend.startAudio(soundtrackPaths, false)
    }

    Keys.onPressed: function(event) {
        if (overlayVisible) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                goBack()
                event.accepted = true
            } else if (event.key === Qt.Key_Up) {
                if (choiceIndex > 0) choiceIndex--
                event.accepted = true
            } else if (event.key === Qt.Key_Down) {
                if (choiceIndex < choices.length - 1) choiceIndex++
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                var choice = choices[choiceIndex]
                overlayVisible = false
                launchPlayback(choice.startMs, choice.playlistPos, choice.shuffle)
                event.accepted = true
            }
        } else {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
                mpvController.sendKey("ESC")
                event.accepted = true
            } else if (event.key === Qt.Key_Backspace) {
                mpvController.sendKey("BS")
                event.accepted = true
            } else if (event.key === Qt.Key_Up) {
                mpvController.sendKey("UP")
                event.accepted = true
            } else if (event.key === Qt.Key_Down) {
                mpvController.sendKey("DOWN")
                event.accepted = true
            } else if (event.key === Qt.Key_Left) {
                mpvController.sendKey("LEFT")
                event.accepted = true
            } else if (event.key === Qt.Key_Right) {
                mpvController.sendKey("RIGHT")
                event.accepted = true
            } else if (event.key === Qt.Key_Space) {
                mpvController.sendKey("SPACE")
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                mpvController.sendKey("ENTER")
                event.accepted = true
            }
        }
    }

    Connections {
        target: mpvController

        function onPositionChanged(ms) {
            if (ms > 0)
                playerRoot.lastKnownPositionMs = ms
        }
        function onDurationChanged(ms) {
            if (ms > 0) playerRoot.lastKnownDurationMs = ms
        }
        function onPlaylistPosChanged(pos) {
            if (pos >= 0) {
                playerRoot.lastKnownPlaylistPos = pos
                playerRoot.lastKnownPositionMs  = 0
                playerRoot.mainItemLoaded = false
                playerRoot.overlayShownForItem = false
                mpvController.clearTrackOverlay()
            }
        }

        function onCurrentPathChanged(path) {
            playerRoot.currentItemPath = path || ""
            if (playerRoot.mainItemLoaded && !playerRoot.overlayShownForItem)
                playerRoot.overlayShownForItem = moduleRoot.showSoundtrackOverlay(
                    playerRoot.currentItemPath, localFilesBackend.currentSoundtrack())
        }
        function onPlaybackItemLoaded(playlistIndex) {
            playerRoot.mainItemLoaded = true
            playerRoot.overlayShownForItem = false
            playerRoot.currentItemPath = mpvController.currentPath ||
                                         (isPlaylist(filePath) ? "" : filePath)
            playerRoot.overlayShownForItem = moduleRoot.showSoundtrackOverlay(
                playerRoot.currentItemPath, localFilesBackend.currentSoundtrack())
        }
        function onPlaybackItemEnded(playlistIndex, reason, error) {
            playerRoot.mainItemLoaded = false
            playerRoot.overlayShownForItem = false
            mpvController.clearTrackOverlay()
        }

        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            playerRoot.mainItemLoaded = false
            mpvController.clearTrackOverlay()
            localFilesBackend.stopAudio()
            var pos   = lastKnownPositionMs  || finalPositionMs
            var dur   = lastKnownDurationMs  || finalDurationMs
            var plPos = lastKnownPlaylistPos

            if (isPlaylist(filePath)) {
                // Always save playlist state — skip completion detection
                if (pos > 0 || plPos >= 0)
                    localFilesBackend.savePosition(filePath, pos, plPos)
            } else {
                // Single file: clear if near completion, save otherwise
                if (dur > 0 && pos >= dur * 0.95)
                    localFilesBackend.clearPosition(filePath)
                else if (pos > 5000)
                    localFilesBackend.savePosition(filePath, pos, -1)
            }
            goBack()
        }
    }

    Connections {
        target: localFilesBackend

        function onAudioTrackStarted(track) {
            if (playerRoot.mainItemLoaded)
                moduleRoot.showSoundtrackOverlay(playerRoot.currentItemPath, track)
        }
    }

    Component.onCompleted: {
        if (filePath === "") return
        repeatMode = appCore.get_setting(moduleRoot.moduleId, "repeat_mode") ||
                     (appCore.get_setting(moduleRoot.moduleId, "loop_playback") ? "queue" : "off")
        if (["off", "queue", "one"].indexOf(repeatMode) < 0)
            repeatMode = "off"
        var shuffleRaw = appCore.get_setting(moduleRoot.moduleId, "shuffle_playback")
        shuffleSetting = typeof shuffleRaw === "boolean"
                       ? (shuffleRaw ? "yes" : "no") : (shuffleRaw || "ask")
        resumeSetting = appCore.get_setting(moduleRoot.moduleId, "resume_playback") || "ask"
        var subtitleRaw = appCore.get_setting(moduleRoot.moduleId, "auto_subtitles")
        subtitleMode = typeof subtitleRaw === "boolean"
                     ? (subtitleRaw ? "on" : "forced") : (subtitleRaw || "forced")
        var language = appCore.get_setting(moduleRoot.moduleId, "sub_lang") || "-"
        subtitleLanguages = language === "-" ? [] : [language]
        var duration = parseInt(appCore.get_setting(moduleRoot.moduleId, "image_duration") || "5")
        imageDurationSeconds = isNaN(duration) ? 5 : duration
        var soundtrackShuffleRaw = appCore.get_setting(moduleRoot.moduleId, "soundtrack_shuffle")
        var soundtrackShuffle = soundtrackShuffleRaw === true || soundtrackShuffleRaw === "ON"
        soundtrackPaths = localFilesBackend.soundtrackPaths(soundtrackShuffle)
        imageContent = isImage(filePath) ||
                       (isPlaylist(filePath) && localFilesBackend.playlistContainsImages(filePath))

        var playlist = isPlaylist(filePath)
        if (playlist && shuffleSetting === "yes") {
            launchPlayback(0, -1, true)
            return
        }

        if (!playlist && isImage(filePath)) {
            launchPlayback(0, -1, false)
            return
        }

        if (resumeSetting === "no") {
            if (playlist && shuffleSetting === "ask") {
                choices = [
                    { label: "Play in order", startMs: 0, playlistPos: -1, shuffle: false },
                    { label: "Shuffle", startMs: 0, playlistPos: -1, shuffle: true }
                ]
                overlayVisible = true
            } else {
                launchPlayback(0, -1, false)
            }
            return
        }

        var saved    = localFilesBackend.getSavedPosition(filePath)
        var savedPos = saved.pos   || 0
        var savedPl  = (saved.plPos !== undefined && saved.plPos !== null) ? saved.plPos : -1

        var askShuffle = playlist && shuffleSetting === "ask"
        if (resumeSetting === "yes" && !askShuffle) {
            launchPlayback(savedPos, savedPos > 0 ? savedPl : -1, false)
        } else {
            var opts = []
            if (savedPos > 0) {
                opts.push({
                    label: savedPl >= 0
                         ? "Resume video " + (savedPl + 1) + " at " + formatTime(savedPos)
                         : "Resume from " + formatTime(savedPos),
                    startMs: savedPos, playlistPos: savedPl, shuffle: false
                })
                if (resumeSetting === "ask")
                    opts.push({ label: "Start from the beginning", startMs: 0, playlistPos: -1, shuffle: false })
            } else if (askShuffle) {
                opts.push({ label: "Play in order", startMs: 0, playlistPos: -1, shuffle: false })
            }
            if (askShuffle)
                opts.push({ label: "Shuffle", startMs: 0, playlistPos: -1, shuffle: true })

            if (opts.length > 1) {
                choices = opts
                choiceIndex = 0
                overlayVisible = true
            } else if (opts.length === 1) {
                launchPlayback(opts[0].startMs, opts[0].playlistPos, opts[0].shuffle)
            } else {
                launchPlayback(0, -1, false)
            }
        }
    }

    PlaybackControlPanel {
        anchors.fill: parent
        title: itemTitle !== "" ? itemTitle : "LOCAL PLAYBACK"
        subtitle: root.hasMediaOutputScreen ? "PLAYING ON MEDIA DISPLAY" : "PLAYING"
        stateText: lastKnownDurationMs > 0 ? (formatTime(lastKnownPositionMs) + " / " + formatTime(lastKnownDurationMs)) : ""
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

        Rectangle {
            id: dialogRect
            color: root.surfaceColor
            anchors.centerIn: parent
            width: root.sw * 0.76875 //492
            height: root.sh * (0.2833333 + Math.max(0, choices.length - 2) * 0.0583333)

            Column {
                id: dialogColumn
                anchors.fill: parent
                spacing: root.sh * 0.05 //24

                Text {
                    text: choices.some(function(choice) { return choice.shuffle })
                          ? "START PLAYBACK?" : "RESUME PLAYBACK?"
                    color: root.secondaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0333333 //16
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Column {
                    Repeater {
                        model: choices
                        delegate: Item {
                            width: dialogColumn.width
                            height: root.sh * 0.0583333 //28

                            Rectangle {
                                anchors.fill: delegateText
                                color: root.accentColor
                                visible: index === choiceIndex
                            }

                            Text {
                                id: delegateText
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData.label
                                color: index === choiceIndex ? root.surfaceColor : root.primaryColor
                                font.family: root.globalFont
                                font.capitalization: Font.AllUppercase
                                topPadding: root.sh * 0.0041667 //2
                                leftPadding: root.sw * 0.009375 //6
                                rightPadding: root.sw * 0.009375 //6
                                bottomPadding: root.sh * 0.00625 //3
                                font.pixelSize: root.sh * 0.0416667 //20
                            }
                        }
                    }
                }

                Text {
                    text: "[ESC]:BACK [▲▼]:NAVIGATE [ENTER]:SELECT"
                    color: root.tertiaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0333333 //16
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }
        }
    }

    function isPlaylist(path) {
        return localFilesBackend.isPlaylist(path)
    }

    function isImage(path) { return localFilesBackend.isImage(path) }

    function formatTime(ms) {
        var s   = Math.floor(ms / 1000)
        var h   = Math.floor(s / 3600)
        var m   = Math.floor((s % 3600) / 60)
        var sec = s % 60
        if (h > 0)
            return h + ":" + pad(m) + ":" + pad(sec)
        return m + ":" + pad(sec)
    }

    function pad(n) { return n < 10 ? "0" + n : "" + n }
}
