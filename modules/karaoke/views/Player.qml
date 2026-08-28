import QtQuick
import Components

FocusScope {
    id: playerRoot

    property var navParams: ({})
    signal goBack()

    property string playlistPath: navParams.playlistPath || ""
    property int startIndex: navParams.startIndex || 0
    property var queue: []
    property string currentEntryId: ""
    property int currentQueueIndex: -1
    property string pendingEndedEntryId: ""
    property string pendingOutcome: ""
    property string pendingError: ""
    property bool leaving: false
    property bool playbackErrorVisible: false
    property string playbackErrorText: ""
    property int lastPositionMs: 0
    property int lastDurationMs: 0
    property string readyEntryId: ""
    property string installedPrefetchEntryId: ""
    property string loadedEntryId: ""
    property string transitionFromEntryId: ""
    property string transitionTargetEntryId: ""
    property bool usingTransitionOutput: false
    property bool trackPreviewActive: false
    property string trackPreviewCurrentEntryId: ""
    property string trackPreviewNextEntryId: ""
    readonly property int trackPreviewWindowMs: 15000

    function indexOfEntry(entryId) {
        for (var index = 0; index < queue.length; ++index) {
            if (queue[index].entryId === entryId)
                return index
        }
        return -1
    }

    function currentEntry() {
        var index = indexOfEntry(currentEntryId)
        return index >= 0 ? queue[index] : null
    }

    function nextEntry() {
        var index = indexOfEntry(currentEntryId)
        return index >= 0 && index + 1 < queue.length ? queue[index + 1] : null
    }

    function clearNextSongPreview() {
        if (trackPreviewActive)
            mpvController.clearTrackOverlay()
        trackPreviewActive = false
        trackPreviewCurrentEntryId = ""
        trackPreviewNextEntryId = ""
    }

    function syncNextSongPreview() {
        var next = nextEntry()
        var inWindow = loadedEntryId !== "" && loadedEntryId === currentEntryId &&
                       TrackPreviewPolicy.isActive(lastPositionMs, lastDurationMs,
                                                   trackPreviewWindowMs)
        if (!inWindow || !next) {
            clearNextSongPreview()
            return
        }

        var nextId = next.entryId || ""
        if (trackPreviewActive && trackPreviewCurrentEntryId === currentEntryId &&
            trackPreviewNextEntryId === nextId)
            return

        mpvController.showTrackOverlay({
            displayTitle: next.displayTitle || next.rawTitle || "KARAOKE SONG",
            fallbackArtist: "KARAOKE"
        }, "UP NEXT", 0)
        trackPreviewActive = true
        trackPreviewCurrentEntryId = currentEntryId
        trackPreviewNextEntryId = nextId
    }

    function playbackStatus() {
        var entry = currentEntry()
        var title = entry ? entry.displayTitle : "KARAOKE PLAYBACK"
        var timing = lastDurationMs > 0
                   ? " - " + formatTime(lastPositionMs) + " / " + formatTime(lastDurationMs)
                   : ""
        return "NOW PLAYING: " + title + timing
    }

    function syncQueue(items) {
        queue = (items || []).slice()
        currentQueueIndex = indexOfEntry(currentEntryId)
        if (readyEntryId !== "" && indexOfEntry(readyEntryId) < 0)
            readyEntryId = ""
        if (transitionTargetEntryId !== "" && indexOfEntry(transitionTargetEntryId) < 0) {
            transitionOverlay.reveal()
            transitionTargetEntryId = ""
        }
        if (currentQueueIndex >= 0 && loadedEntryId === currentEntryId)
            prefetchTimer.restart()
        syncNextSongPreview()
    }

    function beginTransitionForIndex(index) {
        if (index < 0 || index >= queue.length)
            return
        var entry = queue[index]
        transitionFromEntryId = currentEntryId
        transitionTargetEntryId = entry.entryId || ""
        transitionOverlay.cover(entry.displayTitle || "KARAOKE")
    }

    function maybeCoverNaturalTransition() {
        if (lastDurationMs <= 0 || lastPositionMs <= 0 ||
            lastDurationMs - lastPositionMs > 850 ||
            transitionFromEntryId === currentEntryId)
            return
        beginTransitionForIndex(currentQueueIndex + 1)
    }

    function requestNextPrefetch() {
        if (loadedEntryId === "" || loadedEntryId !== currentEntryId)
            return
        var index = currentQueueIndex + 1
        if (index <= 0 || index >= queue.length) {
            readyEntryId = ""
            return
        }
        var entry = queue[index]
        if (!entry || entry.entryId === installedPrefetchEntryId) {
            readyEntryId = entry ? entry.entryId : ""
            return
        }
        readyEntryId = ""
        karaokeBackend.prefetchQueueEntry(entry.entryId || "")
    }

    function karaokeInputBindings() {
        return [
            "ESC script-message 240mp-key karaoke-exit",
            "BS script-message 240mp-key karaoke-exit",
            "UP script-message 240mp-key karaoke-up",
            "DOWN script-message 240mp-key karaoke-down",
            "Shift+UP script-message 240mp-key karaoke-move-up",
            "Shift+DOWN script-message 240mp-key karaoke-move-down",
            "m script-message 240mp-key karaoke-move-mode",
            "M script-message 240mp-key karaoke-move-mode",
            "c script-message 240mp-key karaoke-clear",
            "C script-message 240mp-key karaoke-clear",
            "y script-message 240mp-key karaoke-confirm-clear",
            "Y script-message 240mp-key karaoke-confirm-clear",
            "n script-message 240mp-key karaoke-cancel-clear",
            "N script-message 240mp-key karaoke-cancel-clear",
            "DEL script-message 240mp-key karaoke-remove",
            "SPACE cycle pause",
            "ENTER script-message 240mp-key karaoke-play-selected"
        ]
    }

    function startPlayback() {
        syncQueue(karaokeBackend.getQueue())
        if (queue.length === 0 || playlistPath === "") {
            playbackErrorVisible = true
            playbackErrorText = "THE KARAOKE QUEUE IS EMPTY"
            return
        }
        var index = Math.min(Math.max(0, startIndex), queue.length - 1)
        currentEntryId = queue[index].entryId
        currentQueueIndex = index
        clearNextSongPreview()
        karaokeBackend.resetQueueEntry(currentEntryId)
        usingTransitionOutput = root.hasMediaOutputScreen
        mpvController.loadAndPlay(playlistPath, 0.0, 0, -1, [], false,
                                  index, 0.0, "", false, "karaoke", false,
                                  [], "", karaokeInputBindings())
    }

    function stopAndGoBack() {
        if (leaving)
            return
        leaving = true
        clearNextSongPreview()
        mpvController.stop()
        usingTransitionOutput = false
        goBack()
    }

    function finalizePendingOutcome(processStillRunning) {
        if (pendingEndedEntryId === "")
            return
        var entryId = pendingEndedEntryId
        var endedIndex = indexOfEntry(entryId)
        var outcome = pendingOutcome
        var error = pendingError
        pendingEndedEntryId = ""
        pendingOutcome = ""
        pendingError = ""

        if (outcome === "completed") {
            karaokeBackend.completeQueueEntry(entryId)
            if (processStillRunning && endedIndex >= 0)
                mpvController.removePlaylistItem(endedIndex)
        } else if (outcome === "failed") {
            karaokeBackend.failQueueEntry(entryId,
                error !== "" ? error : "YouTube playback failed")
        }
    }

    function handlePlaylistPosition(position) {
        if (position < 0)
            return

        var nextEntryId = position < queue.length ? queue[position].entryId : ""
        if (pendingEndedEntryId !== "" && nextEntryId !== pendingEndedEntryId) {
            finalizePendingOutcome(true)
        }
        if (nextEntryId !== "" && nextEntryId !== currentEntryId) {
            clearNextSongPreview()
            currentEntryId = nextEntryId
            loadedEntryId = ""
            karaokeBackend.resetQueueEntry(nextEntryId)
            lastPositionMs = 0
            lastDurationMs = 0
            if (readyEntryId === nextEntryId)
                readyEntryId = ""
        }
        currentQueueIndex = indexOfEntry(currentEntryId)
        prefetchTimer.restart()
    }

    function handleCommand(command) {
        if (command === "karaoke-exit") {
            stopAndGoBack()
        } else if (command === "karaoke-confirm-clear") {
            browser.confirmClearQueue()
        } else if (command === "karaoke-cancel-clear") {
            browser.cancelClearQueue()
        } else {
            browser.handlePlaybackCommand(command)
        }
    }

    focus: true

    Connections {
        target: karaokeBackend
        function onQueueChanged(items) { syncQueue(items) }
        function onQueueEntryPrefetched(entryId, filePath) {
            var index = indexOfEntry(entryId)
            if (index !== currentQueueIndex + 1 || filePath === "")
                return
            mpvController.replacePlaylistItem(index, filePath)
            installedPrefetchEntryId = entryId
            readyEntryId = entryId
        }
        function onQueueEntryPrefetchFailed(entryId, message) {
            if (readyEntryId === entryId)
                readyEntryId = ""
        }
    }

    Connections {
        target: mpvController

        function onPositionChanged(position) {
            if (position >= 0) {
                lastPositionMs = position
                maybeCoverNaturalTransition()
                syncNextSongPreview()
            }
        }
        function onDurationChanged(duration) {
            if (duration >= 0) {
                lastDurationMs = duration
                syncNextSongPreview()
            }
        }
        function onPlaylistPosChanged(position) {
            handlePlaylistPosition(position)
        }
        function onPlaybackItemLoaded(playlistIndex) {
            if (transitionOverlay.phase !== "idle")
                revealTimer.restart()
            fileLoadedTimer.restart()
        }
        function onPlaybackItemEnded(playlistIndex, reason, error) {
            if (leaving || reason === "redirect" || reason === "stop" || reason === "quit")
                return
            clearNextSongPreview()
            var entry = playlistIndex >= 0 && playlistIndex < queue.length
                      ? queue[playlistIndex] : currentEntry()
            if (!entry)
                return
            if (transitionOverlay.phase === "idle")
                beginTransitionForIndex(playlistIndex + 1)
            pendingEndedEntryId = entry.entryId
            pendingOutcome = reason === "eof" ? "completed" : "failed"
            pendingError = error || ""
        }
        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            if (leaving)
                return
            clearNextSongPreview()
            if (reason === "failed") {
                if (pendingEndedEntryId === "") {
                    pendingEndedEntryId = currentEntryId
                    pendingOutcome = "failed"
                    pendingError = "mpv could not play this queue"
                }
                finalizePendingOutcome(false)
                usingTransitionOutput = false
                playbackErrorVisible = true
                playbackErrorText = "KARAOKE PLAYBACK FAILED - THE SONG REMAINS IN THE QUEUE"
                return
            }
            if (pendingEndedEntryId === "" && currentEntryId !== "") {
                pendingEndedEntryId = currentEntryId
                pendingOutcome = "completed"
            }
            finalizePendingOutcome(false)
            usingTransitionOutput = false
            goBack()
        }
        function onMpvKeyPressed(key) {
            handleCommand(key)
        }
    }

    Items {
        id: browser
        anchors.fill: parent
        navParams: ({
            navListState: {
                filterText: "",
                activePane: 0,
                searchIndex: 0,
                queueIndex: playerRoot.startIndex
            }
        })
        playbackActive: true
        currentEntryId: playerRoot.currentEntryId
        readyEntryId: playerRoot.readyEntryId
        playbackStatusText: playerRoot.playbackStatus()

        onGoBack: playerRoot.stopAndGoBack()
        onSongEnqueued: function(song) {
            mpvController.appendPlaylistItem(song.url || "")
        }
        onQueueEntryMoved: function(fromIndex, toIndex) {
            mpvController.movePlaylistItem(fromIndex, toIndex)
        }
        onQueueEntryRemoved: function(index, entryId) {
            mpvController.removePlaylistItem(index)
        }
        onQueueEntryRequested: function(index, entryId) {
            beginTransitionForIndex(index)
            playerRoot.clearNextSongPreview()
            loadedEntryId = ""
            mpvController.playPlaylistItem(index)
        }
        onUpcomingQueueCleared: mpvController.clearPlaylistExceptCurrent()
    }

    Timer {
        id: prefetchTimer
        interval: 250
        repeat: false
        onTriggered: playerRoot.requestNextPrefetch()
    }

    Timer {
        id: fileLoadedTimer
        interval: 100
        repeat: false
        onTriggered: {
            playerRoot.loadedEntryId = playerRoot.currentEntryId
            prefetchTimer.restart()
            playerRoot.syncNextSongPreview()
        }
    }

    Timer {
        id: revealTimer
        interval: 180
        repeat: false
        onTriggered: transitionOverlay.reveal()
    }

    MediaOutputLease {
        host: root
        enabled: playerRoot.usingTransitionOutput
        requested: transitionOverlay.phase !== "idle"
        opaque: true
        acceptsFocus: false
    }

    PlaybackTransitionOverlay {
        id: transitionOverlay
        parent: playerRoot.usingTransitionOutput ? root.mediaOutputLayer : playerRoot
        width: parent ? parent.width : playerRoot.width
        height: parent ? parent.height : playerRoot.height
        z: 2000
        onRevealed: {
            playerRoot.transitionFromEntryId = ""
            playerRoot.transitionTargetEntryId = ""
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.surfaceColor
        visible: playbackErrorVisible
        z: 90

        Text {
            text: playbackErrorText
            color: root.tertiaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            anchors.centerIn: parent
            width: root.sw * 0.7
            font.pixelSize: root.sh * 0.05
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

    Component.onCompleted: startPlayback()
}
