import QtQuick
import Components

FocusScope {
    id: itemsRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()
    signal songEnqueued(var song)
    signal queueEntryMoved(int fromIndex, int toIndex)
    signal queueEntryRemoved(int index, string entryId)
    signal queueEntryRequested(int index, string entryId)
    signal upcomingQueueCleared()

    property var songs: []
    property int catalogTotalCount: 0
    property int resultTotalCount: 0
    property int resultPageOffset: 0
    readonly property int resultPageSize: 250
    property var queue: []
    property string filterText: navListState.filterText || ""
    property int activePane: navListState.activePane !== undefined ? navListState.activePane : 0
    property bool catalogLoading: true
    property bool catalogRefreshing: false
    property string catalogError: ""
    property string statusMessage: ""
    property bool moveMode: false
    property bool clearConfirmationVisible: false
    property bool playbackActive: false
    property string currentEntryId: ""
    property string readyEntryId: ""
    property string playbackStatusText: ""

    readonly property int currentQueueIndex: indexOfQueueEntry(currentEntryId)

    function indexOfQueueEntry(entryId) {
        for (var index = 0; index < queue.length; ++index) {
            if (queue[index].entryId === entryId)
                return index
        }
        return -1
    }

    function globalSearchIndex() {
        return searchList.currentIndex >= 0 ? resultPageOffset + searchList.currentIndex : -1
    }

    function applyFilter(preferredIndex) {
        var wanted = preferredIndex !== undefined ? Math.max(0, preferredIndex) : 0
        var requestedOffset = Math.floor(wanted / resultPageSize) * resultPageSize
        var result = karaokeBackend.searchCatalog(filterText, requestedOffset, resultPageSize)
        songs = (result.items || []).slice()
        resultTotalCount = result.total || 0
        catalogTotalCount = result.catalogTotal || 0
        resultPageOffset = result.offset || 0
        if (songs.length === 0) {
            searchList.currentIndex = -1
            return
        }
        searchList.currentIndex = Math.min(Math.max(0, wanted - resultPageOffset), songs.length - 1)
        searchList.positionViewAtIndex(searchList.currentIndex, ListView.Contain)
    }

    function moveSearchSelection(direction) {
        var current = globalSearchIndex()
        var target = current < 0 ? 0 : current + direction
        if (target < 0 || target >= resultTotalCount)
            return
        if (target < resultPageOffset || target >= resultPageOffset + songs.length)
            applyFilter(target)
        else
            searchList.currentIndex = target - resultPageOffset
    }

    function queueEntryIdAt(index) {
        return index >= 0 && index < queue.length ? queue[index].entryId || "" : ""
    }

    function syncQueue(items) {
        var selectedId = queueEntryIdAt(queueList.currentIndex)
        var previousIndex = queueList.currentIndex
        queue = (items || []).slice()
        var restored = -1
        if (selectedId !== "") {
            for (var index = 0; index < queue.length; ++index) {
                if (queue[index].entryId === selectedId) {
                    restored = index
                    break
                }
            }
        }
        if (restored < 0 && queue.length > 0)
            restored = Math.min(Math.max(0, previousIndex), queue.length - 1)
        queueList.currentIndex = restored
        if (restored >= 0)
            queueList.positionViewAtIndex(restored, ListView.Contain)
    }

    function selectedSearchSong() {
        return searchList.currentIndex >= 0 ? songs[searchList.currentIndex] : null
    }

    function addSelectedSong() {
        var song = selectedSearchSong()
        if (!song)
            return
        if (karaokeBackend.enqueue(song)) {
            statusMessage = "ADDED: " + song.displayTitle
            statusTimer.restart()
            var persistedQueue = karaokeBackend.getQueue()
            songEnqueued(persistedQueue[persistedQueue.length - 1])
        }
    }

    function moveSelectedQueueEntry(direction) {
        var fromIndex = queueList.currentIndex
        var toIndex = fromIndex + direction
        if (fromIndex < 0 || toIndex < 0 || toIndex >= queue.length)
            return
        if (playbackActive && (fromIndex <= currentQueueIndex || toIndex <= currentQueueIndex)) {
            statusMessage = "ONLY UPCOMING SONGS CAN BE MOVED"
            statusTimer.restart()
            return
        }
        if (karaokeBackend.moveQueueEntry(fromIndex, toIndex)) {
            queueEntryMoved(fromIndex, toIndex)
            queueList.currentIndex = toIndex
            queueList.positionViewAtIndex(toIndex, ListView.Contain)
        }
    }

    function removeSelectedQueueEntry() {
        var entry = queueList.currentIndex >= 0 ? queue[queueList.currentIndex] : null
        if (!entry)
            return
        if (playbackActive && queueList.currentIndex <= currentQueueIndex) {
            statusMessage = queueList.currentIndex === currentQueueIndex
                          ? "THE CURRENT SONG CANNOT BE REMOVED"
                          : "PAST SONGS CANNOT BE REMOVED DURING PLAYBACK"
            statusTimer.restart()
            return
        }
        var removeIndex = queueList.currentIndex
        if (karaokeBackend.removeQueueEntry(entry.entryId))
            queueEntryRemoved(removeIndex, entry.entryId)
    }

    function startQueue() {
        if (queue.length === 0 || queueList.currentIndex < 0)
            return
        if (playbackActive) {
            var requested = queue[queueList.currentIndex]
            queueEntryRequested(queueList.currentIndex, requested.entryId || "")
            statusMessage = "PLAYING: " + (requested.displayTitle || "KARAOKE SONG")
            statusTimer.restart()
            return
        }
        var playlistPath = karaokeBackend.writePlaybackPlaylist()
        if (playlistPath === "") {
            statusMessage = "COULD NOT PREPARE PLAYLIST"
            statusTimer.restart()
            return
        }
        navigateTo("Player.qml", {
            playlistPath: playlistPath,
            startIndex: queueList.currentIndex
        }, {
            filterText: filterText,
            activePane: activePane,
            searchIndex: globalSearchIndex(),
            queueIndex: queueList.currentIndex
        })
    }

    function requestClearQueue() {
        if (queue.length === 0)
            return
        clearConfirmationVisible = true
        clearDialog.choiceIndex = 1
        clearDialog.forceActiveFocus()
    }

    function confirmClearQueue() {
        if (!clearConfirmationVisible)
            return
        clearDialog.accepted()
    }

    function cancelClearQueue() {
        if (!clearConfirmationVisible)
            return
        clearDialog.rejected()
    }

    function handlePlaybackCommand(command) {
        if (clearConfirmationVisible)
            return
        if (command === "karaoke-up") {
            activePane = 1
            if (moveMode)
                moveSelectedQueueEntry(-1)
            else if (queueList.currentIndex > 0)
                queueList.currentIndex--
        } else if (command === "karaoke-down") {
            activePane = 1
            if (moveMode)
                moveSelectedQueueEntry(1)
            else if (queueList.currentIndex < queue.length - 1)
                queueList.currentIndex++
        } else if (command === "karaoke-move-up") {
            activePane = 1
            moveSelectedQueueEntry(-1)
        } else if (command === "karaoke-move-down") {
            activePane = 1
            moveSelectedQueueEntry(1)
        } else if (command === "karaoke-move-mode") {
            activePane = 1
            moveMode = !moveMode
            statusMessage = moveMode ? "MOVE MODE ON" : "MOVE MODE OFF"
            statusTimer.restart()
        } else if (command === "karaoke-remove") {
            activePane = 1
            removeSelectedQueueEntry()
        } else if (command === "karaoke-clear") {
            requestClearQueue()
        } else if (command === "karaoke-play-selected") {
            activePane = 1
            startQueue()
        }
        queueList.positionViewAtIndex(queueList.currentIndex, ListView.Contain)
    }

    function handleKey(event) {
        if (clearConfirmationVisible)
            return

        if (event.key === Qt.Key_Tab) {
            activePane = activePane === 0 ? 1 : 0
            moveMode = false
            event.accepted = true
            return
        }

        if (activePane === 0) {
            if (event.key === Qt.Key_Right) {
                activePane = 1
                event.accepted = true
            } else if (event.key === Qt.Key_Up) {
                moveSearchSelection(-1)
                event.accepted = true
            } else if (event.key === Qt.Key_Down) {
                moveSearchSelection(1)
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                addSelectedSong()
                event.accepted = true
            } else if (event.key === Qt.Key_Backspace) {
                if (filterText.length > 0) {
                    filterText = filterText.slice(0, -1)
                    applyFilter(0)
                } else {
                    goBack()
                }
                event.accepted = true
            } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
                if (filterText.length > 0) {
                    filterText = ""
                    applyFilter(0)
                } else {
                    goBack()
                }
                event.accepted = true
            } else {
                var typed = event.text || ""
                if (!(event.modifiers & Qt.ControlModifier) &&
                    !(event.modifiers & Qt.MetaModifier) &&
                    !(event.modifiers & Qt.AltModifier) &&
                    typed.length === 1 && typed.charCodeAt(0) >= 32) {
                    filterText += typed
                    applyFilter(0)
                    event.accepted = true
                }
            }
            return
        }

        if (event.key === Qt.Key_Left) {
            activePane = 0
            moveMode = false
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            if (moveMode || (event.modifiers & Qt.ShiftModifier))
                moveSelectedQueueEntry(-1)
            else if (queueList.currentIndex > 0)
                queueList.currentIndex--
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            if (moveMode || (event.modifiers & Qt.ShiftModifier))
                moveSelectedQueueEntry(1)
            else if (queueList.currentIndex < queueList.count - 1)
                queueList.currentIndex++
            event.accepted = true
        } else if (event.key === Qt.Key_M) {
            moveMode = !moveMode
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            startQueue()
            event.accepted = true
        } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            removeSelectedQueueEntry()
            event.accepted = true
        } else if (event.key === Qt.Key_C) {
            requestClearQueue()
            event.accepted = true
        } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
            if (moveMode)
                moveMode = false
            else if (playbackActive)
                goBack()
            else
                activePane = 0
            event.accepted = true
        }
    }

    focus: true
    Keys.onPressed: function(event) { handleKey(event) }

    Connections {
        target: karaokeBackend

        function onCatalogLoadStarted(hasCachedCatalog) {
            catalogLoading = catalogTotalCount === 0
            catalogRefreshing = hasCachedCatalog && catalogTotalCount > 0
            catalogError = ""
        }
        function onCatalogChanged(itemCount, fromCache) {
            var restoreIndex = resultTotalCount === 0 && navListState.searchIndex !== undefined
                             ? navListState.searchIndex : globalSearchIndex()
            applyFilter(Math.max(0, restoreIndex))
        }
        function onCatalogLoadFinished(itemCount, fromCache) {
            catalogLoading = false
            catalogRefreshing = false
            catalogError = ""
        }
        function onCatalogLoadFailed(message, usingCache) {
            catalogLoading = false
            catalogRefreshing = false
            catalogError = usingCache ? "REFRESH FAILED - USING SAVED CATALOG" : message
        }
        function onQueueChanged(items) {
            syncQueue(items)
        }
    }

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
    }

    Text {
        text: catalogLoading && catalogTotalCount === 0
              ? "SEARCH - LOADING CATALOG..."
              : "SEARCH " + resultTotalCount + "/" + catalogTotalCount +
                (catalogLoading ? " - LOADING MORE" :
                 (catalogRefreshing ? " - REFRESHING" : ""))
        color: activePane === 0 ? root.secondaryColor : root.tertiaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.205
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.46
        elide: Text.ElideRight
        font.pixelSize: root.sh * 0.0291667
    }

    Text {
        text: "QUEUE " + queue.length + (moveMode ? " - MOVE MODE" : "")
        color: activePane === 1 ? root.secondaryColor : root.tertiaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.205
        anchors.leftMargin: root.sw * 0.60625
        width: root.sw * 0.28
        elide: Text.ElideRight
        font.pixelSize: root.sh * 0.0291667
    }

    ListView {
        id: searchList
        model: songs
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25
        anchors.leftMargin: root.sw * 0.10625
        width: root.sw * 0.485
        height: root.sh * 0.525
        clip: true
        interactive: false

        delegate: SelectableMarqueeRow {
            width: searchList.width
            label: modelData.displayTitle || ""
            selected: activePane === 0 && searchList.currentIndex === index
            twoLine: true
            splitAtSeparator: true
            textSize: root.sh * 0.0229167
        }
    }

    ListView {
        id: queueList
        model: queue
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25
        anchors.leftMargin: root.sw * 0.596875
        width: root.sw * 0.37
        height: root.sh * 0.525
        clip: true
        interactive: false

        delegate: SelectableMarqueeRow {
            width: queueList.width
            label: (index + 1) + ". " +
                   (modelData.entryId === currentEntryId ? "[PLAYING] " : "") +
                   (modelData.entryId === readyEntryId ? "[READY] " : "") +
                   (modelData.status === "failed" ? "[FAILED] " : "") +
                   (modelData.displayTitle || "")
            selected: activePane === 1 && queueList.currentIndex === index
            normalColor: modelData.entryId === currentEntryId
                         ? root.secondaryColor
                         : (modelData.status === "failed" ? root.tertiaryColor : root.primaryColor)
            twoLine: true
            splitAtSeparator: true
            textSize: root.sh * 0.021875
        }
    }

    Text {
        visible: catalogLoading && catalogTotalCount === 0
        text: "LOADING KARAOKE CATALOG..."
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: searchList
        font.pixelSize: root.sh * 0.0416667
    }

    Text {
        visible: !catalogLoading && songs.length === 0
        text: filterText !== "" ? "NO MATCHES" : "NO SONGS FOUND"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: searchList
        font.pixelSize: root.sh * 0.0416667
    }

    Text {
        visible: catalogError !== ""
        text: catalogError
        color: root.tertiaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.sw * 0.115625
        anchors.bottomMargin: root.sh * 0.15
        width: root.sw * 0.77
        elide: Text.ElideRight
        font.pixelSize: root.sh * 0.025
    }

    Text {
        visible: filterText !== "" || statusMessage !== "" || playbackStatusText !== ""
        text: statusMessage !== "" ? statusMessage
              : (filterText !== "" ? "FILTER: " + filterText : playbackStatusText)
        color: root.secondaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.sw * 0.115625
        anchors.bottomMargin: root.sh * 0.125
        width: root.sw * 0.77
        elide: Text.ElideRight
        font.pixelSize: root.sh * 0.0291667
    }

    Text {
        text: activePane === 0
              ? "[TYPE]:SEARCH [ENTER]:ADD [TAB/RIGHT]:QUEUE [ESC]:BACK"
              : (playbackActive
                 ? "[ENTER]:PLAY [SHIFT+UP/DOWN]:MOVE [M]:MOVE MODE [DEL]:REMOVE [C]:CLEAR [ESC]:STOP"
                 : "[ENTER]:PLAY [SHIFT+UP/DOWN]:MOVE [M]:MOVE MODE [DEL]:REMOVE [C]:CLEAR")
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.0833333
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.77
        elide: Text.ElideRight
        font.pixelSize: root.sh * 0.025
    }

    ConfirmDialog {
        id: clearDialog
        visible: clearConfirmationVisible
        prompt: "CLEAR THE KARAOKE QUEUE?"
        confirmLabel: "CLEAR"
        cancelLabel: "CANCEL"
        onAccepted: {
            if (playbackActive) {
                karaokeBackend.clearQueueExcept(currentEntryId)
                upcomingQueueCleared()
            } else {
                karaokeBackend.clearQueue()
            }
            clearConfirmationVisible = false
            itemsRoot.forceActiveFocus()
        }
        onRejected: {
            clearConfirmationVisible = false
            itemsRoot.forceActiveFocus()
        }
    }

    Timer {
        id: statusTimer
        interval: 2500
        repeat: false
        onTriggered: statusMessage = ""
    }

    Component.onCompleted: {
        syncQueue(karaokeBackend.getQueue())
        karaokeBackend.loadCatalogDeferred()
        var queueRestore = navListState.queueIndex !== undefined ? navListState.queueIndex : 0
        queueList.currentIndex = queue.length > 0 ? Math.min(queueRestore, queue.length - 1) : -1
    }
}
