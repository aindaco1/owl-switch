import QtQuick
import QtTest
import "../../modules/local_files/views" as Local

Item {
    id: root
    width: 640
    height: 480
    property real sw: width
    property real sh: height
    property color surfaceColor: "black"
    property color primaryColor: "white"
    property color secondaryColor: "yellow"
    property color tertiaryColor: "gray"
    property color accentColor: "cyan"
    property string globalFont: "Monaco"
    property bool hasMediaOutputScreen: false

    QtObject {
        id: appCore
        function get_module_info(moduleId) { return {name: "Local"} }
        function get_setting(moduleId, key) { return key === "queue_shuffle" }
    }

    QtObject {
        id: localFilesBackend
        property var savedMedia: []
        function mediaRoot() { return "/media" }
        function getItems(folder) { return [] }
        function getQueue(kind) { return kind === "media" ? savedMedia : [] }
        function queueIndexForEntryId(kind, id) {
            return getQueue(kind).findIndex(entry => entry.entryId === id)
        }
        function preparePlayback(entryId, shuffle) {
            // Play order B, C, A. The saved queue has A and C pointing to the same file.
            return { playlistPath: "/queue.m3u8", entries: [savedMedia[1], savedMedia[2], savedMedia[0]], startIndex: 0 }
        }
        function soundtrackPaths(shuffle) { return [] }
        function getSavedPosition(path) { return {} }
        function resetQueueEntry(kind, entryId) {}
        function currentSoundtrack() { return {} }
        function isImage(path) { return false }
        function isAudio(path) { return false }
        function isPlaylist(path) { return false }
        function clearPosition(path) {}
        function stopAudio() {}
        signal queueChanged(string kind, var values)
        signal youtubePlaylistImportStarted()
        signal youtubePlaylistImportProgress(int addedCount)
        signal youtubePlaylistImportFinished(int addedCount)
        signal youtubePlaylistImportFailed(string message)
        signal audioPlaybackStarted()
        signal audioPlaybackFailed()
        signal audioTrackStarted(var track)
    }

    QtObject {
        id: mpvController
        property int launches: 0
        function loadAndPlayWithOptions(path, options) { ++launches }
        function clearTrackOverlay() {}
        function stop() { playbackEnded(0, 0, "stopped") }
        function selectPlaybackTracks(audio, subtitle, files) {}
        signal positionChanged(int position)
        signal durationChanged(int duration)
        signal playlistPosChanged(int position)
        signal playbackItemLoaded(int playlistIndex)
        signal playbackItemEnded(int playlistIndex, string reason, string error)
        signal playbackEnded(int position, int duration, string reason)
    }

    Component { id: localModule; Local.Root { anchors.fill: parent } }

    TestCase {
        name: "LocalQueueReturn"
        when: windowShown

        function test_selection_data() {
            return [
                {tag: "shuffle with duplicate paths", loaded: true, remove: false, reason: "stopped", expected: 2},
                {tag: "natural end", loaded: true, remove: false, reason: "eof", expected: 2},
                {tag: "joined after file load", loaded: false, remove: false, reason: "eof", expected: 2},
                {tag: "last entry removed", loaded: true, remove: true, reason: "stopped", expected: 1},
                {tag: "nothing loaded", loaded: false, remove: false, reason: "failed", expected: 1}
            ]
        }

        function test_selection(data) {
            localFilesBackend.savedMedia = [
                {entryId: "A", filePath: "/media/same.mp4", displayTitle: "Same"},
                {entryId: "B", filePath: "/media/other.mp4", displayTitle: "Other"},
                {entryId: "C", filePath: "/media/same.mp4", displayTitle: "Same"}
            ]
            mpvController.launches = 0
            var page = createTemporaryObject(localModule, root)
            verify(page)
            var list = findChild(page, "localMediaQueueList")
            verify(list)
            page.forceActiveFocus()
            keyClick(Qt.Key_Right)
            keyClick(Qt.Key_Down)
            compare(list.currentIndex, 1)
            keyClick(Qt.Key_Return)
            tryCompare(mpvController, "launches", 1)
            if (data.loaded) {
                mpvController.playbackItemLoaded(0)
                mpvController.playlistPosChanged(1)
                mpvController.playbackItemLoaded(1)
                // A position change alone does not establish that the next file played.
                mpvController.playlistPosChanged(2)
            }
            if (data.remove)
                localFilesBackend.savedMedia = localFilesBackend.savedMedia.slice(0, 2)
            if (data.reason === "stopped") {
                keyClick(Qt.Key_Escape)
            } else {
                if (data.reason === "eof")
                    mpvController.playbackItemEnded(1, "eof", "")
                mpvController.playbackEnded(0, 0, data.reason)
            }
            var restored = findChild(page, "localMediaQueueList")
            verify(restored)
            compare(restored.currentIndex, data.expected)
            compare(restored.currentItem.selected, true)
        }
    }
}
