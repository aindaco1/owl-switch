import QtQuick
import QtTest
import "../../modules/jellyfin/views" as Jellyfin

Item {
    id: root
    width: 640
    height: 480
    property real sw: width
    property real sh: height
    property color primaryColor: "white"
    property color secondaryColor: "yellow"
    property color tertiaryColor: "gray"
    property color accentColor: "cyan"
    property color surfaceColor: "black"
    property string globalFont: "Monaco"

    QtObject {
        id: moduleRoot
        property string moduleId: "com.owlswitch.jellyfin"
        property string moduleName: "Jellyfin"
        property string moduleIcon: ""
    }
    QtObject {
        id: appCore
        property var watchedSetting
        function get_setting(moduleId, key) {
            return key === "watched_indicators" ? watchedSetting : undefined
        }
    }
    QtObject {
        id: jellyfinBackend
        property var rows: []
        property bool firstPageOnly: false
        signal itemsPageLoaded(var rows, bool reset, bool finished)
        signal errorOccurred(string message)
        signal watchHistoryChanged()
        function load_items_for_type(parentId, itemType, recursive) { itemsPageLoaded(rows, true, !firstPageOnly) }
    }
    Component { id: listPage; Jellyfin.Items { anchors.fill: parent } }

    TestCase {
        name: "JellyfinWatched"
        when: windowShown

        function test_labels_data() {
            return [
                {tag: "default off", setting: undefined, type: "movie", played: true, prefix: ""},
                {tag: "boolean off", setting: false, type: "movie", played: true, prefix: ""},
                {tag: "string off", setting: "OFF", type: "episode", played: true, prefix: ""},
                {tag: "watched movie", setting: true, type: "movie", played: true, prefix: "[WATCHED] "},
                {tag: "watched episode", setting: "ON", type: "episode", played: true, prefix: "[WATCHED] "},
                {tag: "resume only", setting: true, type: "episode", played: false, prefix: ""},
                {tag: "completed series", setting: true, type: "series", played: true, count: 0, prefix: "[WATCHED] "},
                {tag: "remaining episodes", setting: true, type: "season", played: false, count: 3, prefix: "[3 LEFT] "},
                {tag: "no container data", setting: true, type: "season", prefix: ""},
                {tag: "empty season", setting: true, type: "season", played: false, count: 0, prefix: ""},
                {tag: "folder", setting: true, type: "folder", played: true, prefix: ""}
            ]
        }

        function test_labels(data) {
            appCore.watchedSetting = data.setting
            jellyfinBackend.rows = [{id: "1", title: "Pilot", type: data.type, played: data.played,
                                     viewOffset: 5000, unplayedItemCount: data.count}]
            var page = createTemporaryObject(listPage, root)
            verify(page)
            var list = findChild(page, "jellyfinItemsList")
            tryVerify(() => list.currentItem !== null)
            compare(list.currentItem.label, data.prefix + "Pilot")
            compare(list.currentItem.selected, true)
        }

        function test_refreshKeepsSelectionAcrossPages() {
            appCore.watchedSetting = true
            var titles = ["One", "Two", "Three"].map(title => ({title: title, type: "movie"}))
            jellyfinBackend.rows = titles
            var page = createTemporaryObject(listPage, root)
            var list = findChild(page, "jellyfinItemsList")
            list.currentIndex = 2
            jellyfinBackend.rows = [titles[0]]
            jellyfinBackend.firstPageOnly = true
            jellyfinBackend.watchHistoryChanged()
            compare(list.count, 1)
            jellyfinBackend.itemsPageLoaded(titles.slice(1), false, true)
            compare(list.currentIndex, 2)
            jellyfinBackend.firstPageOnly = false
        }

        function test_stopAcknowledgedAfterReturningToList() {
            appCore.watchedSetting = true
            jellyfinBackend.rows = [{id: "1", type: "movie", title: "Pilot", played: false}]
            var page = createTemporaryObject(listPage, root)
            var list = findChild(page, "jellyfinItemsList")
            tryVerify(() => list.currentItem !== null)
            compare(list.currentItem.label, "Pilot")
            jellyfinBackend.rows = [{id: "1", type: "movie", title: "Pilot", played: true}]
            jellyfinBackend.watchHistoryChanged()
            tryVerify(() => list.currentItem && list.currentItem.label === "[WATCHED] Pilot")
        }
    }
}
