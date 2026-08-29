import QtQuick
import Components

FocusScope {
    id: itemsRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})
    property string folderPath: navParams.folderPath || localFilesBackend.mediaRoot()
    property string folderName: navParams.folderName || ""
    property bool hideExtensions: false
    property int activePane: navListState.activePane !== undefined ? navListState.activePane : 0
    property string queueKind: navListState.queueKind || "media"
    property var mediaQueue: []
    property var soundtrackQueue: []
    property var visibleQueue: queueKind === "soundtrack" ? soundtrackQueue : mediaQueue
    property bool moveMode: false
    property bool clearConfirmationVisible: false
    property string statusMessage: ""
    property string repeatMode: "off"
    property bool queueShuffle: false
    property bool soundtrackShuffle: false
    property bool autoLaunch: false
    property bool youtubeInputVisible: false
    property bool youtubeImporting: false

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    function enabledSetting(key) {
        var value = appCore.get_setting(moduleRoot.moduleId, key)
        return value === true || value === "ON"
    }

    function displayName(item) {
        var name = item ? (item.name || item.displayTitle || "") : ""
        if (!hideExtensions || (item && (item.isFolder || item.source === "youtube")) ||
            name.charAt(0) === ".")
            return name
        var dot = name.lastIndexOf(".")
        return dot > 0 ? name.slice(0, dot) : name
    }

    function repeatLabel() {
        return repeatMode === "queue" ? "REPEAT QUEUE"
             : repeatMode === "one" ? "REPEAT ONE" : "REPEAT OFF"
    }

    function syncQueue(kind, values) {
        var previousIndex = queueList.currentIndex
        if (kind === "soundtrack")
            soundtrackQueue = (values || []).slice()
        else
            mediaQueue = (values || []).slice()
        var count = visibleQueue.length
        queueList.currentIndex = count > 0 ? Math.min(Math.max(0, previousIndex), count - 1) : -1
        if (queueList.currentIndex >= 0)
            queueList.positionViewAtIndex(queueList.currentIndex, ListView.Contain)
    }

    function cycleRepeatMode() {
        repeatMode = repeatMode === "off" ? "queue" : (repeatMode === "queue" ? "one" : "off")
        appCore.save_setting(moduleRoot.moduleId, "repeat_mode", repeatMode)
        statusMessage = repeatLabel()
        statusTimer.restart()
    }

    function toggleShuffle() {
        queueShuffle = !queueShuffle
        appCore.save_setting(moduleRoot.moduleId, "queue_shuffle", queueShuffle)
        statusMessage = queueShuffle ? "QUEUE SHUFFLE ON" : "QUEUE SHUFFLE OFF"
        statusTimer.restart()
    }

    function toggleQueueKind() {
        queueKind = queueKind === "media" ? "soundtrack" : "media"
        moveMode = false
        queueList.currentIndex = visibleQueue.length > 0 ? 0 : -1
        statusMessage = queueKind === "media" ? "MEDIA QUEUE" : "SOUNDTRACK QUEUE"
        statusTimer.restart()
    }

    function openSelectedFile() {
        var item = fileList.currentIndex >= 0 ? fileList.model[fileList.currentIndex] : null
        if (!item)
            return
        var state = {
            currentIndex: fileList.currentIndex,
            activePane: activePane,
            queueKind: queueKind,
            queueIndex: queueList.currentIndex
        }
        if (item.isFolder) {
            navigateTo("Items.qml", { folderPath: item.path, folderName: item.name }, state)
        } else {
            navigateTo("Detail.qml", { filePath: item.path, title: item.name }, state)
        }
    }

    function moveSelectedQueueEntry(direction) {
        var fromIndex = queueList.currentIndex
        var toIndex = fromIndex + direction
        if (fromIndex < 0 || toIndex < 0 || toIndex >= visibleQueue.length)
            return
        if (localFilesBackend.moveQueueEntry(queueKind, fromIndex, toIndex)) {
            queueList.currentIndex = toIndex
            queueList.positionViewAtIndex(toIndex, ListView.Contain)
        }
    }

    function removeSelectedQueueEntry() {
        var entry = queueList.currentIndex >= 0 ? visibleQueue[queueList.currentIndex] : null
        if (entry)
            localFilesBackend.removeQueueEntry(queueKind, entry.entryId || "")
    }

    function requestClearQueue() {
        if (visibleQueue.length === 0)
            return
        clearConfirmationVisible = true
        clearDialog.choiceIndex = 1
        clearDialog.forceActiveFocus()
    }

    function startQueue(entryId) {
        if (mediaQueue.length === 0) {
            statusMessage = "THE MEDIA QUEUE IS EMPTY"
            statusTimer.restart()
            return
        }
        var plan = localFilesBackend.preparePlayback(entryId || "", queueShuffle)
        if (!plan || !plan.playlistPath) {
            statusMessage = "COULD NOT PREPARE LOCAL QUEUE"
            statusTimer.restart()
            return
        }
        navigateTo("QueuePlayer.qml", {
            playlistPath: plan.playlistPath,
            entries: plan.entries || [],
            startIndex: plan.startIndex || 0,
            repeatMode: repeatMode,
            shuffled: queueShuffle,
            soundtrackPaths: localFilesBackend.soundtrackPaths(soundtrackShuffle),
            muteMainAudio: plan.muteMainAudio === true
        }, {
            currentIndex: fileList.currentIndex,
            activePane: 1,
            queueKind: "media",
            queueIndex: queueList.currentIndex,
            returnedFromPlayer: true
        })
    }

    function startSelectedQueueEntry() {
        if (queueKind !== "media") {
            openYouTubePlaylistInput()
            return
        }
        var entry = queueList.currentIndex >= 0 ? mediaQueue[queueList.currentIndex] : null
        startQueue(entry ? entry.entryId : "")
    }

    function openYouTubePlaylistInput() {
        if (youtubeImporting)
            return
        youtubeInputVisible = true
        youtubeUrlInput.text = ""
        Qt.callLater(function() { youtubeUrlInput.forceActiveFocus() })
    }

    function restoreKeyboardFocus() {
        youtubeUrlInput.focus = false
        itemsRoot.focus = true
        itemsRoot.forceActiveFocus()
    }

    function closeYouTubePlaylistInput() {
        if (youtubeImporting) {
            youtubeInputVisible = false
            restoreKeyboardFocus()
            localFilesBackend.cancelYouTubePlaylistImport()
            return
        }
        youtubeInputVisible = false
        restoreKeyboardFocus()
    }

    function submitYouTubePlaylist() {
        if (youtubeImporting)
            return
        var value = youtubeUrlInput.text.trim()
        if (value === "") {
            statusMessage = "PASTE A YOUTUBE PLAYLIST URL"
            statusTimer.restart()
            return
        }
        localFilesBackend.importYouTubePlaylist(value)
    }

    function handleKey(event) {
        if (clearConfirmationVisible || youtubeInputVisible)
            return
        if (event.key === Qt.Key_R) {
            cycleRepeatMode()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_S) {
            toggleShuffle()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_T) {
            activePane = 1
            toggleQueueKind()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Tab) {
            activePane = activePane === 0 ? 1 : 0
            moveMode = false
            event.accepted = true
            return
        }

        if (activePane === 0) {
            if (event.key === Qt.Key_Right) {
                activePane = 1
            } else if (event.key === Qt.Key_Up && fileList.count > 0) {
                fileList.currentIndex = (fileList.currentIndex - 1 + fileList.count) % fileList.count
                fileList.positionViewAtIndex(fileList.currentIndex, ListView.Contain)
            } else if (event.key === Qt.Key_Down && fileList.count > 0) {
                fileList.currentIndex = (fileList.currentIndex + 1) % fileList.count
                fileList.positionViewAtIndex(fileList.currentIndex, ListView.Contain)
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                openSelectedFile()
            } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace ||
                       event.key === Qt.Key_Back) {
                goBack()
            } else {
                return
            }
            event.accepted = true
            return
        }

        if (event.key === Qt.Key_Left) {
            activePane = 0
            moveMode = false
        } else if (event.key === Qt.Key_Up) {
            if (moveMode || (event.modifiers & Qt.ShiftModifier))
                moveSelectedQueueEntry(-1)
            else if (queueList.currentIndex > 0)
                queueList.currentIndex--
        } else if (event.key === Qt.Key_Down) {
            if (moveMode || (event.modifiers & Qt.ShiftModifier))
                moveSelectedQueueEntry(1)
            else if (queueList.currentIndex < visibleQueue.length - 1)
                queueList.currentIndex++
        } else if (event.key === Qt.Key_M) {
            moveMode = !moveMode
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            startSelectedQueueEntry()
        } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            removeSelectedQueueEntry()
        } else if (event.key === Qt.Key_C) {
            requestClearQueue()
        } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
            if (moveMode)
                moveMode = false
            else
                activePane = 0
        } else {
            return
        }
        queueList.positionViewAtIndex(queueList.currentIndex, ListView.Contain)
        event.accepted = true
    }

    focus: true
    Keys.onPressed: function(event) { handleKey(event) }

    Connections {
        target: localFilesBackend
        function onQueueChanged(kind, values) { syncQueue(kind, values) }
        function onYoutubePlaylistImportStarted() {
            youtubeImporting = true
            statusMessage = "LOADING YOUTUBE PLAYLIST"
            statusTimer.stop()
        }
        function onYoutubePlaylistImportProgress(addedCount) {
            statusMessage = addedCount === 1 ? "ADDED 1 TRACK - LOADING MORE"
                                             : "ADDED " + addedCount + " TRACKS - LOADING MORE"
        }
        function onYoutubePlaylistImportFinished(addedCount) {
            youtubeImporting = false
            youtubeInputVisible = false
            youtubeUrlInput.text = ""
            statusMessage = addedCount === 1 ? "ADDED 1 YOUTUBE TRACK"
                                             : "ADDED " + addedCount + " YOUTUBE TRACKS"
            statusTimer.restart()
            restoreKeyboardFocus()
        }
        function onYoutubePlaylistImportFailed(message) {
            youtubeImporting = false
            statusMessage = message || "COULD NOT LOAD YOUTUBE PLAYLIST"
            statusTimer.restart()
            if (youtubeInputVisible)
                youtubeUrlInput.forceActiveFocus()
            else
                restoreKeyboardFocus()
        }
    }

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: folderName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
    }

    Text {
        text: "FILES - " + localFilesBackend.mediaRoot()
        color: activePane === 0 ? root.secondaryColor : root.tertiaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.205
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.46
        elide: Text.ElideMiddle
        font.pixelSize: root.sh * 0.025
    }

    Text {
        text: (queueKind === "media" ? "QUEUE " : "SOUNDTRACK ") + visibleQueue.length +
              (moveMode ? " - MOVE" : "")
        color: activePane === 1 ? root.secondaryColor : root.tertiaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.205
        anchors.leftMargin: root.sw * 0.60625
        width: root.sw * 0.36
        elide: Text.ElideRight
        font.pixelSize: root.sh * 0.025
    }

    ListView {
        id: fileList
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25
        anchors.leftMargin: root.sw * 0.10625
        width: root.sw * 0.485
        height: root.sh * 0.525
        clip: true
        interactive: false

        delegate: SelectableMarqueeRow {
            width: fileList.width
            label: itemsRoot.displayName(modelData) + (modelData.isFolder ? "/" : "")
            selected: activePane === 0 && fileList.currentIndex === index
            textSize: root.sh * 0.03125
        }
    }

    ListView {
        id: queueList
        model: visibleQueue
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * (queueKind === "soundtrack" ? 0.31875 : 0.25)
        anchors.leftMargin: root.sw * 0.596875
        width: root.sw * 0.37
        height: root.sh * (queueKind === "soundtrack" ? 0.45625 : 0.525)
        clip: true
        interactive: false

        delegate: SelectableMarqueeRow {
            width: queueList.width
            label: (index + 1) + ". " +
                   (modelData.status === "failed" ? "[FAILED] " : "") +
                   itemsRoot.displayName(modelData)
            selected: activePane === 1 && queueList.currentIndex === index
            normalColor: modelData.status === "failed" ? root.tertiaryColor : root.primaryColor
            textSize: root.sh * 0.0270833
        }
    }

    Rectangle {
        visible: queueKind === "soundtrack"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25
        anchors.leftMargin: root.sw * 0.596875
        width: root.sw * 0.37
        height: root.sh * 0.0520833
        color: root.surfaceColor
        border.color: activePane === 1 ? root.accentColor : root.tertiaryColor
        border.width: root.sh * 0.003125

        Text {
            anchors.centerIn: parent
            width: parent.width - root.sw * 0.0125
            text: youtubeImporting ? "LOADING YOUTUBE PLAYLIST..." : "+ ADD YOUTUBE PLAYLIST"
            color: activePane === 1 ? root.secondaryColor : root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            font.pixelSize: root.sh * 0.025
        }
    }

    Text {
        visible: fileList.count === 0
        text: "NO ITEMS FOUND"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: fileList
        font.pixelSize: root.sh * 0.0416667
    }

    Text {
        visible: visibleQueue.length === 0
        text: queueKind === "media" ? "QUEUE EMPTY" : "NO SOUNDTRACK"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: queueList
        font.pixelSize: root.sh * 0.0333333
    }

    Text {
        text: statusMessage !== "" ? statusMessage
              : (repeatLabel() + (queueShuffle ? " - SHUFFLE ON" : " - SHUFFLE OFF") +
                 (soundtrackQueue.length > 0 ? " - SOUNDTRACK " + soundtrackQueue.length : ""))
        color: statusMessage !== "" ? root.secondaryColor : root.tertiaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.sw * 0.115625
        anchors.bottomMargin: root.sh * 0.125
        width: root.sw * 0.82
        elide: Text.ElideRight
        font.pixelSize: root.sh * 0.025
    }

    Text {
        text: activePane === 0
              ? "[ENTER]:DETAIL [TAB/RIGHT]:QUEUE [R]:REPEAT [S]:SHUFFLE [T]:QUEUE TYPE [ESC]:BACK"
              : (queueKind === "soundtrack"
                 ? "[ENTER]:ADD YOUTUBE [SHIFT+UP/DOWN]:MOVE [DEL]:REMOVE [C]:CLEAR [T]:QUEUE TYPE"
                 : "[ENTER]:PLAY [SHIFT+UP/DOWN]:MOVE [DEL]:REMOVE [C]:CLEAR [T]:QUEUE TYPE")
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.0833333
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.82
        elide: Text.ElideRight
        font.pixelSize: root.sh * 0.0208333
    }

    ConfirmDialog {
        id: clearDialog
        visible: clearConfirmationVisible
        prompt: queueKind === "media" ? "CLEAR THE MEDIA QUEUE?" : "CLEAR THE SOUNDTRACK QUEUE?"
        confirmLabel: "CLEAR"
        cancelLabel: "CANCEL"
        onAccepted: {
            localFilesBackend.clearQueue(queueKind)
            clearConfirmationVisible = false
            itemsRoot.forceActiveFocus()
        }
        onRejected: {
            clearConfirmationVisible = false
            itemsRoot.forceActiveFocus()
        }
    }

    Rectangle {
        visible: youtubeInputVisible
        z: 100
        anchors.centerIn: parent
        width: root.sw * 0.75
        height: root.sh * 0.29
        color: root.surfaceColor
        border.color: root.accentColor
        border.width: root.sh * 0.0041667

        Text {
            text: "ADD YOUTUBE PLAYLIST"
            color: root.secondaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.topMargin: root.sh * 0.0333333
            anchors.leftMargin: root.sw * 0.025
            font.pixelSize: root.sh * 0.0333333
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: root.sw * 0.025
            anchors.rightMargin: root.sw * 0.025
            anchors.topMargin: root.sh * 0.1
            height: root.sh * 0.0666667
            color: root.surfaceColor
            border.color: youtubeUrlInput.activeFocus ? root.accentColor : root.tertiaryColor
            border.width: root.sh * 0.003125

            TextInput {
                id: youtubeUrlInput
                anchors.fill: parent
                anchors.leftMargin: root.sw * 0.0125
                anchors.rightMargin: root.sw * 0.0125
                color: root.primaryColor
                selectionColor: root.accentColor
                selectedTextColor: root.surfaceColor
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.025
                verticalAlignment: TextInput.AlignVCenter
                clip: true
                readOnly: youtubeImporting
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText |
                                  Qt.ImhNoAutoUppercase
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
                        closeYouTubePlaylistInput()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        submitYouTubePlaylist()
                        event.accepted = true
                    }
                }
            }
        }

        Text {
            text: youtubeImporting ? "LOADING PLAYLIST..."
                                   : "PASTE URL, THEN PRESS ENTER  -  [ESC]:CANCEL"
            color: youtubeImporting ? root.secondaryColor : root.tertiaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.leftMargin: root.sw * 0.025
            anchors.bottomMargin: root.sh * 0.0333333
            font.pixelSize: root.sh * 0.025
        }
    }

    Timer {
        id: statusTimer
        interval: 2500
        repeat: false
        onTriggered: statusMessage = ""
    }

    Component.onCompleted: {
        var hideRaw = appCore.get_setting(moduleRoot.moduleId, "hide_extensions")
        hideExtensions = hideRaw === true || hideRaw === "ON"
        repeatMode = appCore.get_setting(moduleRoot.moduleId, "repeat_mode") ||
                     (enabledSetting("loop_playback") ? "queue" : "off")
        if (["off", "queue", "one"].indexOf(repeatMode) < 0)
            repeatMode = "off"
        queueShuffle = enabledSetting("queue_shuffle")
        soundtrackShuffle = enabledSetting("soundtrack_shuffle")
        autoLaunch = enabledSetting("auto_launch")
        mediaQueue = localFilesBackend.getQueue("media")
        soundtrackQueue = localFilesBackend.getQueue("soundtrack")

        var loaded = localFilesBackend.getItems(folderPath)
        fileList.model = loaded
        var fileRestore = navListState.currentIndex !== undefined ? navListState.currentIndex : 0
        fileList.currentIndex = loaded.length > 0 ? Math.min(fileRestore, loaded.length - 1) : -1
        var queueRestore = navListState.queueIndex !== undefined ? navListState.queueIndex : 0
        queueList.currentIndex = visibleQueue.length > 0
                               ? Math.min(queueRestore, visibleQueue.length - 1) : -1
        if (autoLaunch && mediaQueue.length > 0 && moduleRoot.claimAutoLaunch())
            Qt.callLater(function() { startQueue(mediaQueue[0].entryId || "") })
        else
            fileList.forceActiveFocus()
    }
}
