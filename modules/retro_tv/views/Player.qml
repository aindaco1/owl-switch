import QtQuick
import Components
import "RetroTvData.js" as RetroTvData

FocusScope {
    id: playerRoot

    property var navParams: ({})
    property var feed: navParams.feed || ({})

    property string loadState: "loading"
    property string errorText: ""
    property string statusText: ""
    property string filterMessage: ""
    property var feedData: null
    property var channels: []
    property var categories: RetroTvData.visibleCategories()
    property var filters: RetroTvData.defaultFilters()
    property var draftFilters: RetroTvData.defaultFilters()
    property int filterCursor: 0
    property int currentIndex: -1
    property int currentClipIndex: 0
    property int failedSkips: 0
    property int maxFailureSkips: 12
    property int noiseLevel: 1
    property bool blackAndWhite: false
    property bool glowEnabled: true
    property bool staticTransitions: true
    property bool staticOverlayVisible: false
    property var pendingPreferredClipIndex: undefined
    property bool filterPanelVisible: false
    property bool leaving: false
    property bool suppressFinish: false
    property bool usingExternalOutput: false
    property bool pendingStatusOsd: false
    property bool mpvSessionActive: false

    signal goBack()

    focus: true

    function currentChannel() {
        if (currentIndex < 0 || currentIndex >= channels.length)
            return null
        return channels[currentIndex]
    }

    function currentClip() {
        var channel = currentChannel()
        if (!channel || currentClipIndex < 0 || currentClipIndex >= channel.clips.length)
            return null
        return channel.clips[currentClipIndex]
    }

    function channelMatches(channel, activeFilters) {
        return channel && !channel.failed && activeFilters[channel.categoryCode] && hasPlayableClip(channel)
    }

    function hasPlayableClip(channel) {
        if (!channel || !channel.clips)
            return false
        for (var i = 0; i < channel.clips.length; ++i) {
            if (!channel.clips[i].failed)
                return true
        }
        return false
    }

    function firstPlayableClipIndex(channel) {
        if (!channel || !channel.clips)
            return -1
        for (var i = 0; i < channel.clips.length; ++i) {
            if (!channel.clips[i].failed)
                return i
        }
        return -1
    }

    function nextPlayableClipIndex(channel, startIndex, direction) {
        if (!channel || !channel.clips || channel.clips.length === 0)
            return -1

        var step = direction < 0 ? -1 : 1
        for (var checked = 1; checked <= channel.clips.length; ++checked) {
            var idx = (startIndex + checked * step + channel.clips.length * 2) % channel.clips.length
            if (!channel.clips[idx].failed)
                return idx
        }
        return -1
    }

    function findNextChannel(startIndex, direction, activeFilters) {
        if (channels.length === 0 || RetroTvData.selectedFilterCount(activeFilters) === 0)
            return -1

        var step = direction < 0 ? -1 : 1
        var start = startIndex
        if (start < 0)
            start = Math.floor(Math.random() * channels.length)
        if (direction !== 0)
            start += step

        for (var checked = 0; checked < channels.length; ++checked) {
            var idx = (start + checked * step + channels.length * 2) % channels.length
            if (channelMatches(channels[idx], activeFilters))
                return idx
        }
        return -1
    }

    function findYearChannel(yearDigit) {
        if (channels.length === 0)
            return -1
        var start = currentIndex < 0 ? 0 : currentIndex
        for (var checked = 0; checked < channels.length; ++checked) {
            var idx = (start + checked) % channels.length
            var channel = channels[idx]
            if (channel.yearIndex === yearDigit && channelMatches(channel, filters))
                return idx
        }
        return -1
    }

    function setStatus(text) {
        statusText = text
        statusTimer.restart()
        pendingStatusOsd = true
        if (loadState === "playing" && mpvController.position > 0) {
            mpvController.showText(text, 2500)
            pendingStatusOsd = false
        }
    }

    function videoUrl(clip) {
        return "https://www.youtube.com/watch?v=" + clip.videoId
    }

    function effectFilters() {
        return RetroTvData.buildVideoFilter(noiseLevel, blackAndWhite, glowEnabled)
    }

    function retroInputBindings() {
        var bindings = [
            "UP script-message 240mp-key channel-up",
            "DOWN script-message 240mp-key channel-down",
            "LEFT script-message 240mp-key clip-prev",
            "RIGHT script-message 240mp-key clip-next",
            "ESC script-message 240mp-key exit",
            "BS script-message 240mp-key exit",
            "SPACE cycle pause",
            "ENTER cycle pause",
            "f script-message 240mp-key filters",
            "F script-message 240mp-key filters",
            "v script-message 240mp-key filters",
            "V script-message 240mp-key filters",
            "n script-message 240mp-key noise",
            "N script-message 240mp-key noise",
            "b script-message 240mp-key bw",
            "B script-message 240mp-key bw",
            "g script-message 240mp-key glow",
            "G script-message 240mp-key glow",
            "o script-message 240mp-key static",
            "O script-message 240mp-key static",
            "y script-message 240mp-key youtube",
            "Y script-message 240mp-key youtube",
            "c script-message 240mp-key controls",
            "C script-message 240mp-key controls",
            "h script-message 240mp-key controls",
            "H script-message 240mp-key controls"
        ]
        for (var i = 0; i <= 9; ++i)
            bindings.push(i + " script-message 240mp-key year-" + i)
        return bindings
    }

    function controlsText() {
        return "UP/DOWN: channel surf\n" +
               "LEFT/RIGHT: skip clip\n" +
               "SPACE/ENTER: play/pause\n" +
               "0-9: jump within decade\n" +
               "F/V: filters  M: video controls\n" +
               "N: noise  G: glow  B: B/W\n" +
               "O: static transitions\n" +
               "Y: open on YouTube\n" +
               "ESC: exit"
    }

    function activateExternalOutput() {
        usingExternalOutput = root.hasMediaOutputScreen
    }

    function showStaticOutput() {
        staticOverlayVisible = true
        staticCanvas.requestPaint()
    }

    function hideStaticOutput() {
        staticOverlayVisible = false
    }

    function showControls() {
        mpvController.showText(controlsText(), 6000)
    }

    function startCurrentPlayback(preferredClipIndex) {
        var channel = currentChannel()
        if (!channel) {
            showPlaybackError("No channels matched the selected filters.")
            return
        }

        var clipIndex = -1
        if (preferredClipIndex !== undefined &&
            preferredClipIndex >= 0 &&
            preferredClipIndex < channel.clips.length &&
            !channel.clips[preferredClipIndex].failed) {
            clipIndex = preferredClipIndex
        } else {
            clipIndex = firstPlayableClipIndex(channel)
        }

        if (clipIndex < 0) {
            channel.failed = true
            changeChannel(1, false)
            return
        }

        currentClipIndex = clipIndex
        var clip = currentClip()
        if (!clip) {
            showPlaybackError("No clip is available for this channel.")
            return
        }

        loadState = "playing"
        suppressFinish = false
        if (mpvSessionActive && mpvController.replaceCurrentMedia(videoUrl(clip))) {
            mpvController.setVideoFilters(effectFilters())
        } else {
            mpvSessionActive = true
            mpvController.loadAndPlayWithOptions(videoUrl(clip), {
                loop: true,
                oscMode: "retro",
                youtubeProfile: "video720p",
                videoFilters: effectFilters(),
                inputBindings: retroInputBindings()
            })
        }

        var label = channel.year + " " + channel.categoryLabel + " CH " + (currentIndex + 1)
        if (channel.clips.length > 1)
            label += " [" + (currentClipIndex + 1) + "/" + channel.clips.length + "]"
        setStatus(label)
    }

    function playCurrentSelection(preferredClipIndex, withStatic) {
        if (withStatic && staticTransitions && loadState === "playing") {
            pendingPreferredClipIndex = preferredClipIndex
            showStaticOutput()
            staticOverlayHideTimer.stop()
            staticTransitionTimer.restart()
            return
        }

        startCurrentPlayback(preferredClipIndex)
    }

    function startRandomPlayback() {
        currentIndex = findNextChannel(-1, 0, filters)
        failedSkips = 0
        startCurrentPlayback()
    }

    function changeChannel(direction, withStatic) {
        var next = findNextChannel(currentIndex, direction, filters)
        if (next < 0) {
            showPlaybackError("No channels matched the selected filters.")
            return
        }
        currentIndex = next
        currentClipIndex = 0
        failedSkips = 0
        playCurrentSelection(undefined, withStatic !== false)
    }

    function skipCurrentClip(direction) {
        var channel = currentChannel()
        if (!channel) {
            changeChannel(direction)
            return
        }

        var nextClip = nextPlayableClipIndex(channel, currentClipIndex, direction)
        if (nextClip >= 0 && nextClip !== currentClipIndex) {
            currentClipIndex = nextClip
            failedSkips = 0
            playCurrentSelection(currentClipIndex, true)
            return
        }

        var clip = currentClip()
        if (clip)
            clip.failed = true
        if (!hasPlayableClip(channel))
            channel.failed = true
        changeChannel(direction)
    }

    function jumpToYear(yearDigit) {
        var next = findYearChannel(yearDigit)
        if (next < 0) {
            setStatus("NO MATCHING CHANNELS FOR " + (feed.startYear + yearDigit))
            return
        }
        currentIndex = next
        currentClipIndex = 0
        failedSkips = 0
        playCurrentSelection(undefined, true)
    }

    function handlePlaybackFailure() {
        if (leaving || filterPanelVisible)
            return

        var channel = currentChannel()
        var clip = currentClip()
        if (clip)
            clip.failed = true
        if (channel && !hasPlayableClip(channel))
            channel.failed = true

        ++failedSkips
        if (failedSkips > maxFailureSkips) {
            showPlaybackError("Could not start a playable YouTube clip. Update yt-dlp and try again.")
            return
        }

        if (channel && hasPlayableClip(channel)) {
            startCurrentPlayback(nextPlayableClipIndex(channel, currentClipIndex, 1))
            return
        }

        var next = findNextChannel(currentIndex, 1, filters)
        if (next < 0) {
            showPlaybackError("No playable clips remain for the selected filters.")
            return
        }
        currentIndex = next
        currentClipIndex = 0
        playCurrentSelection(undefined, true)
    }

    function showPlaybackError(message) {
        hideStaticOutput()
        staticTransitionTimer.stop()
        staticOverlayHideTimer.stop()
        suppressFinish = true
        mpvController.stop()
        mpvSessionActive = false
        loadState = "error"
        errorText = message
    }

    function exitPlayback() {
        leaving = true
        hideStaticOutput()
        staticTransitionTimer.stop()
        staticOverlayHideTimer.stop()
        suppressFinish = true
        mpvController.stop()
        mpvSessionActive = false
        goBack()
    }

    function retryPlayback() {
        for (var c = 0; c < channels.length; ++c) {
            channels[c].failed = false
            for (var p = 0; p < channels[c].clips.length; ++p)
                channels[c].clips[p].failed = false
        }
        failedSkips = 0
        startRandomPlayback()
    }

    function openFilters() {
        draftFilters = RetroTvData.cloneFilters(filters)
        filterCursor = 0
        filterMessage = ""
        filterPanelVisible = true
        hideStaticOutput()
        staticTransitionTimer.stop()
        staticOverlayHideTimer.stop()
        suppressFinish = true
        mpvController.stop()
        mpvSessionActive = false
        filterList.forceActiveFocus()
    }

    function applyFiltersAndResume() {
        if (RetroTvData.selectedFilterCount(draftFilters) === 0) {
            filterMessage = "SELECT AT LEAST ONE CATEGORY"
            return
        }

        filters = RetroTvData.cloneFilters(draftFilters)
        filterPanelVisible = false
        filterMessage = ""
        currentIndex = findNextChannel(currentIndex, 0, filters)
        currentClipIndex = 0
        failedSkips = 0
        startCurrentPlayback()
        playerRoot.forceActiveFocus()
    }

    function cancelFilters() {
        filterPanelVisible = false
        filterMessage = ""
        startCurrentPlayback()
        playerRoot.forceActiveFocus()
    }

    function toggleDraftFilter(code) {
        var next = RetroTvData.cloneFilters(draftFilters)
        next[code] = !next[code]
        draftFilters = next
    }

    function setAllDraftFilters(enabled) {
        var next = {}
        for (var i = 0; i < categories.length; ++i)
            next[categories[i].code] = enabled
        draftFilters = next
    }

    function applyEffects() {
        mpvController.setVideoFilters(effectFilters())
        var parts = []
        if (noiseLevel > 0)
            parts.push("NOISE " + noiseLevel + "X")
        if (blackAndWhite)
            parts.push("B/W")
        if (glowEnabled)
            parts.push("GLOW")
        setStatus(parts.length === 0 ? "EFFECTS OFF" : parts.join(" "))
    }

    function handlePlaybackCommand(command) {
        if (command === "exit") {
            exitPlayback()
        } else if (command === "channel-up") {
            changeChannel(1)
        } else if (command === "channel-down") {
            changeChannel(-1)
        } else if (command === "clip-next") {
            skipCurrentClip(1)
        } else if (command === "clip-prev") {
            skipCurrentClip(-1)
        } else if (command.indexOf("year-") === 0) {
            jumpToYear(parseInt(command.substring(5), 10))
        } else if (command === "filters") {
            openFilters()
        } else if (command === "noise") {
            noiseLevel = (noiseLevel + 1) % 5
            applyEffects()
        } else if (command === "bw") {
            blackAndWhite = !blackAndWhite
            applyEffects()
        } else if (command === "glow") {
            glowEnabled = !glowEnabled
            applyEffects()
        } else if (command === "static") {
            staticTransitions = !staticTransitions
            setStatus(staticTransitions ? "STATIC TRANSITIONS ON" : "STATIC TRANSITIONS OFF")
        } else if (command === "youtube") {
            var clip = currentClip()
            if (clip)
                Qt.openUrlExternally(videoUrl(clip))
        } else if (command === "controls") {
            showControls()
        }
    }

    function handleFilterKey(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back || event.key === Qt.Key_Backspace) {
            cancelFilters()
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            filterCursor = Math.max(0, filterCursor - 1)
            filterList.currentIndex = filterCursor
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            filterCursor = Math.min(categories.length - 1, filterCursor + 1)
            filterList.currentIndex = filterCursor
            event.accepted = true
        } else if (event.key === Qt.Key_Space) {
            toggleDraftFilter(categories[filterCursor].code)
            event.accepted = true
        } else if (event.key === Qt.Key_A) {
            setAllDraftFilters(true)
            event.accepted = true
        } else if (event.key === Qt.Key_N) {
            setAllDraftFilters(false)
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            applyFiltersAndResume()
            event.accepted = true
        }
    }

    Keys.onPressed: function(event) {
        if (filterPanelVisible) {
            handleFilterKey(event)
            return
        }

        if (loadState === "loading") {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back || event.key === Qt.Key_Backspace) {
                goBack()
                event.accepted = true
            }
            return
        }

        if (loadState === "error") {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back || event.key === Qt.Key_Backspace) {
                goBack()
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                retryPlayback()
                event.accepted = true
            }
            return
        }

        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back || event.key === Qt.Key_Backspace) {
            handlePlaybackCommand("exit")
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            handlePlaybackCommand("channel-up")
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            handlePlaybackCommand("channel-down")
            event.accepted = true
        } else if (event.key === Qt.Key_Right) {
            handlePlaybackCommand("clip-next")
            event.accepted = true
        } else if (event.key === Qt.Key_Left) {
            handlePlaybackCommand("clip-prev")
            event.accepted = true
        } else if (event.key === Qt.Key_Space) {
            mpvController.sendKey("SPACE")
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            mpvController.sendKey("ENTER")
            event.accepted = true
        } else if (event.key >= Qt.Key_0 && event.key <= Qt.Key_9) {
            handlePlaybackCommand("year-" + (event.key - Qt.Key_0))
            event.accepted = true
        } else if (event.key === Qt.Key_F || event.key === Qt.Key_V) {
            handlePlaybackCommand("filters")
            event.accepted = true
        } else if (event.key === Qt.Key_N) {
            handlePlaybackCommand("noise")
            event.accepted = true
        } else if (event.key === Qt.Key_B) {
            handlePlaybackCommand("bw")
            event.accepted = true
        } else if (event.key === Qt.Key_G) {
            handlePlaybackCommand("glow")
            event.accepted = true
        } else if (event.key === Qt.Key_O) {
            handlePlaybackCommand("static")
            event.accepted = true
        } else if (event.key === Qt.Key_Y) {
            handlePlaybackCommand("youtube")
            event.accepted = true
        } else if (event.key === Qt.Key_C || event.key === Qt.Key_H) {
            handlePlaybackCommand("controls")
            event.accepted = true
        }
    }

    Connections {
        target: mpvController

        function onPositionChanged(ms) {
            if (ms > 0) {
                if (staticOverlayVisible)
                    hideStaticOutput()
                if (pendingStatusOsd && statusText !== "") {
                    mpvController.showText(statusText, 2500)
                    pendingStatusOsd = false
                }
            }
            if (ms > 3000)
                failedSkips = 0
        }

        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            mpvSessionActive = false
            if (suppressFinish) {
                suppressFinish = false
                return
            }
            if (reason === "failed") handlePlaybackFailure()
            else goBack()
        }

        function onMpvKeyPressed(key) {
            if (!filterPanelVisible && loadState === "playing")
                handlePlaybackCommand(key)
        }
    }

    Timer {
        id: statusTimer
        interval: 3500
        repeat: false
        onTriggered: {
            statusText = ""
            pendingStatusOsd = false
        }
    }

    Timer {
        id: staticNoiseTimer
        interval: 45
        repeat: true
        running: staticOverlayVisible
        onTriggered: staticCanvas.requestPaint()
    }

    Timer {
        id: staticTransitionTimer
        interval: 650
        repeat: false
        onTriggered: {
            var preferredClipIndex = pendingPreferredClipIndex
            pendingPreferredClipIndex = undefined
            hideStaticOutput()
            startCurrentPlayback(preferredClipIndex)
            playerRoot.forceActiveFocus()
        }
    }

    Timer {
        id: staticOverlayHideTimer
        interval: 2500
        repeat: false
        onTriggered: hideStaticOutput()
    }

    MediaOutputLease {
        host: root
        enabled: playerRoot.usingExternalOutput
        requested: playerRoot.staticOverlayVisible && playerRoot.staticTransitions
        opaque: true
        acceptsFocus: false
    }

    PlaybackControlPanel {
        anchors.fill: parent
        visible: !filterPanelVisible && loadState === "playing"
        title: feed.decadeName ? ("RETRO " + feed.decadeName) : "RETRO PLAYBACK"
        subtitle: root.hasMediaOutputScreen ? "PLAYING ON MEDIA DISPLAY" : "PLAYING"
        stateText: statusText !== "" ? statusText : "CHANNEL " + (currentIndex + 1)
        footerText: "[ESC]:EXIT [F]:FILTERS [C]:CONTROLS"
        controls: [
            { key: "UP / DOWN", action: "Channel surf" },
            { key: "LEFT / RIGHT", action: "Skip clip" },
            { key: "SPACE / ENTER", action: "Pause or resume" },
            { key: "0-9", action: "Jump within decade" },
            { key: "F / V", action: "Filter content" },
            { key: "N / G / B / O", action: "Toggle effects" }
        ]
    }

    Rectangle {
        anchors.fill: parent
        color: root.surfaceColor
        visible: loadState === "loading" || loadState === "error"

        Column {
            anchors.centerIn: parent
            width: root.sw * 0.75
            spacing: root.sh * 0.035

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: loadState === "loading" ? "LOADING " + (feed.decadeName || "FEED") : "PLAYBACK ERROR"
                color: root.secondaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.05
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: loadState === "loading" ? "Preparing channels" : errorText
                color: root.primaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0333333
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                visible: loadState === "error"
                text: "[ENTER]:RETRY [ESC]:BACK"
                color: root.tertiaryColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.0333333
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.surfaceColor
        visible: filterPanelVisible

        Text {
            id: filterTitle
            text: "FILTER CONTENT"
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.topMargin: root.sh * 0.125
            anchors.leftMargin: root.sw * 0.125
            font.pixelSize: root.sh * 0.05
        }

        Text {
            text: feed.decadeName + "  " + RetroTvData.filterSummary(draftFilters)
            color: root.tertiaryColor
            font.family: root.globalFont
            anchors.top: filterTitle.bottom
            anchors.left: filterTitle.left
            anchors.topMargin: root.sh * 0.025
            width: root.sw * 0.75
            elide: Text.ElideRight
            font.pixelSize: root.sh * 0.0291667
        }

        ListView {
            id: filterList
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.topMargin: root.sh * 0.25
            anchors.leftMargin: root.sw * 0.115625
            width: root.sw * 0.76875
            height: root.sh * 0.5
            model: categories
            currentIndex: filterCursor
            keyNavigationEnabled: false
            clip: true
            focus: filterPanelVisible

            delegate: Item {
                width: filterList.width
                height: root.sh * 0.05

                Rectangle {
                    color: root.accentColor
                    anchors.fill: rowText
                    visible: filterList.currentIndex === index
                }

                Text {
                    id: rowText
                    anchors.verticalCenter: parent.verticalCenter
                    text: (playerRoot.draftFilters[modelData.code] ? "ON  " : "OFF ") +
                          modelData.name + " (" +
                          (playerRoot.feedData && playerRoot.feedData.counts ? playerRoot.feedData.counts[modelData.code] : 0) + ")"
                    color: filterList.currentIndex === index ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    topPadding: root.sh * 0.0041667
                    leftPadding: root.sw * 0.009375
                    rightPadding: root.sw * 0.009375
                    bottomPadding: root.sh * 0.00625
                    font.pixelSize: root.sh * 0.0375
                }
            }
        }

        Text {
            text: filterMessage
            color: root.secondaryColor
            font.family: root.globalFont
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.leftMargin: root.sw * 0.125
            anchors.bottomMargin: root.sh * 0.15
            font.pixelSize: root.sh * 0.0333333
        }

        Text {
            text: "[SPACE]:TOGGLE [A]:ALL [N]:NONE [ENTER]:PLAY [ESC]:CANCEL"
            color: root.tertiaryColor
            font.family: root.globalFont
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.bottomMargin: root.sh * 0.1041667
            anchors.leftMargin: root.sw * 0.125
            font.pixelSize: root.sh * 0.0333333
        }
    }

    Item {
        id: outputSurface
        parent: playerRoot.usingExternalOutput ? root.mediaOutputLayer : playerRoot
        width: parent ? parent.width : playerRoot.width
        height: parent ? parent.height : playerRoot.height
        z: 1000

        Rectangle {
            visible: statusText !== "" && !filterPanelVisible && loadState === "playing"
            color: "#cc000000"
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.leftMargin: outputSurface.width * 0.025
            anchors.bottomMargin: outputSurface.height * 0.025
            width: Math.min(statusLabel.implicitWidth + outputSurface.width * 0.025,
                            outputSurface.width * 0.9)
            height: statusLabel.implicitHeight + outputSurface.height * 0.016

            Text {
                id: statusLabel
                anchors.centerIn: parent
                width: parent.width - outputSurface.width * 0.018
                text: statusText
                color: "white"
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                elide: Text.ElideRight
                font.pixelSize: outputSurface.height * 0.0275
            }
        }

        Item {
            anchors.fill: parent
            z: 1000
            visible: staticOverlayVisible && staticTransitions

            Rectangle {
                anchors.fill: parent
                color: "black"
            }

            Canvas {
                id: staticCanvas
                anchors.fill: parent
                opacity: 0.95
                onPaint: {
                    var ctx = getContext("2d")
                    var block = Math.max(3, Math.floor(width / 180))

                    ctx.fillStyle = "black"
                    ctx.fillRect(0, 0, width, height)

                    for (var y = 0; y < height; y += block) {
                        for (var x = 0; x < width; x += block) {
                            var level = Math.floor(60 + Math.random() * 175)
                            ctx.fillStyle = "rgb(" + level + "," + level + "," + level + ")"
                            ctx.fillRect(x, y, block, block)
                        }
                    }

                    for (var line = 0; line < height; line += Math.max(2, block * 2)) {
                        ctx.fillStyle = "rgba(0,0,0,0.45)"
                        ctx.fillRect(0, line, width, 1)
                    }

                    for (var band = 0; band < 5; ++band) {
                        var bandY = Math.random() * height
                        ctx.fillStyle = "rgba(255,255,255,0.18)"
                        ctx.fillRect(0, bandY, width, Math.max(1, block))
                    }
                }
            }
        }
    }

    Component.onCompleted: {
        activateExternalOutput()

        if (!feed || !feed.origin) {
            goBack()
            return
        }

        RetroTvData.loadFeed(feed, function(data) {
            feedData = data
            channels = data.channels
            if (channels.length === 0) {
                showPlaybackError("No channels were found in the selected feed.")
                return
            }
            startRandomPlayback()
        }, function(message) {
            loadState = "error"
            errorText = message
        })
    }
}
