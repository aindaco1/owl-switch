import QtQuick
import Components
import "JellyfinMedia.js" as JellyfinMedia

FocusScope {
    id: itemListRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property string parentId: navParams.parentId || navParams.libraryId || ""
    property string libraryId: navParams.libraryId || parentId
    property string libraryName: navParams.libraryName || ""
    property string collectionType: navParams.collectionType || ""
    property string itemType: navParams.itemType || "Movie"
    property bool recursive: navParams.recursive !== undefined ? Boolean(navParams.recursive) : true
    property string mode: navParams.mode || "browse"
    property string seriesName: navParams.seriesName || ""
    property string seasonName: navParams.seasonName || ""
    property var seriesItem: navParams.seriesItem || ({})
    property bool showSeriesMetadata: itemType === "Season" && seriesItem && seriesItem.id
    property var allItems: []
    property var items: []
    property string filterText: navListState.filterText || ""
    readonly property bool showWatchedIndicators: {
        var value = appCore.get_setting(moduleRoot.moduleId, "watched_indicators")
        return value === true || value === "ON"
    }
    property int pendingRestoreIndex: navListState.currentIndex ?? -1
    property bool isLoading: false
    property bool isLoadingMore: false
    property string errorMessage: ""

    function normalizeText(value) {
        return TextSearch.normalize(value)
    }

    function applyFilter(preferredIndex) {
        var q = normalizeText(filterText)
        if (q === "") {
            items = allItems.slice()
        } else {
            var matches = []
            for (var i = 0; i < allItems.length; i++) {
                var item = allItems[i]
                if (normalizeText(JellyfinMedia.searchText(item)).indexOf(q) >= 0)
                    matches.push(item)
            }
            items = matches
        }

        if (items.length > 0) {
            var nextIndex = preferredIndex !== undefined ? preferredIndex : itemList.currentIndex
            if (nextIndex < 0) nextIndex = 0
            itemList.currentIndex = Math.min(nextIndex, items.length - 1)
            itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
        } else {
            itemList.currentIndex = -1
        }
    }

    function subtitleText() {
        return JellyfinMedia.breadcrumbText([libraryName, seriesName, seasonName])
    }

    function emptyLabel() {
        if (filterText !== "") return "NO MATCHES"
        if (itemType === "Series") return "NO SHOWS FOUND"
        if (itemType === "Season") return "NO SEASONS FOUND"
        if (itemType === "Episode") return "NO EPISODES FOUND"
        return "NO ITEMS FOUND"
    }

    function handleKey(event) {
        pendingRestoreIndex = -1
        if (event.key === Qt.Key_Up) {
            if (itemList.count > 0)
                itemList.currentIndex = (itemList.currentIndex - 1 + itemList.count) % itemList.count
            itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Down) {
            if (itemList.count > 0)
                itemList.currentIndex = (itemList.currentIndex + 1) % itemList.count
            itemList.positionViewAtIndex(itemList.currentIndex, ListView.Contain)
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            selectCurrentItem()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Backspace) {
            if (filterText.length > 0) {
                filterText = filterText.slice(0, -1)
                applyFilter(0)
            } else {
                goBack()
            }
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
            if (filterText.length > 0) {
                filterText = ""
                applyFilter(0)
            } else {
                goBack()
            }
            event.accepted = true
            return
        }

        var typed = event.text || ""
        if (!(event.modifiers & Qt.ControlModifier) &&
            !(event.modifiers & Qt.MetaModifier) &&
            !(event.modifiers & Qt.AltModifier) &&
            typed.length === 1 &&
            typed.charCodeAt(0) >= 32) {
            filterText += typed
            applyFilter(0)
            event.accepted = true
        }
    }

    function selectCurrentItem() {
        var item = items[itemList.currentIndex]
        if (!item) return

        if (item.type === "series") {
            itemListRoot.navigateTo("Items.qml", {
                parentId: item.id,
                libraryId: libraryId,
                libraryName: libraryName,
                collectionType: collectionType,
                itemType: "Season",
                recursive: false,
                seriesName: JellyfinMedia.primaryTitle(item),
                seriesItem: item
            }, {
                currentIndex: itemList.currentIndex,
                filterText: filterText
            })
            return
        }

        if (item.type === "season") {
            itemListRoot.navigateTo("Items.qml", {
                parentId: item.id,
                libraryId: libraryId,
                libraryName: libraryName,
                collectionType: collectionType,
                itemType: "Episode",
                recursive: false,
                seriesName: seriesName,
                seasonName: JellyfinMedia.primaryTitle(item)
            }, {
                currentIndex: itemList.currentIndex,
                filterText: filterText
            })
            return
        }

        if (item.type === "boxset" || item.isFolder) {
            itemListRoot.navigateTo("Items.qml", {
                parentId: item.id,
                libraryId: libraryId,
                libraryName: JellyfinMedia.breadcrumbText([libraryName, item.title]),
                collectionType: collectionType,
                mode: item.type === "boxset" ? "boxset" : "folder",
                itemType: item.type === "boxset" ? "BoxSet" : "Video",
                recursive: false
            }, { currentIndex: itemList.currentIndex, filterText: filterText })
            return
        }

        itemListRoot.navigateTo("Detail.qml", {
            item: item,
            libraryName: subtitleText()
        }, {
            currentIndex: itemList.currentIndex,
            filterText: filterText
        })
    }

    function appendLoadedItems(loadedItems, reset, finished) {
        var merged = reset ? [] : allItems.slice()
        for (var i = 0; i < loadedItems.length; i++)
            merged.push(loadedItems[i])
        allItems = merged

        isLoading = false
        isLoadingMore = !finished
        errorMessage = ""

        applyFilter(pendingRestoreIndex >= 0 ? pendingRestoreIndex : itemList.currentIndex)
        if (finished || pendingRestoreIndex < items.length)
            pendingRestoreIndex = -1
    }

    Connections {
        target: jellyfinBackend
        function onItemsPageLoaded(loadedItems, reset, finished) {
            itemListRoot.appendLoadedItems(loadedItems, reset, finished)
        }
        function onWatchHistoryChanged() {
            if (itemListRoot.pendingRestoreIndex < 0)
                itemListRoot.pendingRestoreIndex = itemList.currentIndex
            itemListRoot.loadItems()
        }
        function onErrorOccurred(message) {
            itemListRoot.isLoading = false
            itemListRoot.isLoadingMore = false
            itemListRoot.errorMessage = message
        }
    }

    Component.onCompleted: loadItems()

    function loadItems() {
        isLoading = true
        isLoadingMore = false
        errorMessage = ""
        if (mode === "continue")
            jellyfinBackend.load_continue_watching()
        else if (mode === "upnext")
            jellyfinBackend.load_up_next()
        else if (mode === "boxset")
            jellyfinBackend.load_boxset_children(parentId)
        else if (mode === "folder")
            jellyfinBackend.load_folder_children(parentId || libraryId)
        else
            jellyfinBackend.load_items_for_type(parentId, itemType, recursive)
    }

    focus: true
    Keys.onPressed: function(event) { itemListRoot.handleKey(event) }

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: subtitleText()
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
    }

    Text {
        visible: isLoading
        text: "LOADING..."
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05
    }

    Text {
        visible: !isLoading && errorMessage !== ""
        text: errorMessage
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        width: root.sw * 0.76875
        font.pixelSize: root.sh * 0.05
    }

    Text {
        visible: !isLoading && !isLoadingMore && errorMessage === "" && items.length === 0
        text: emptyLabel()
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.centerIn: parent
        font.pixelSize: root.sh * 0.05
    }

    Text {
        visible: !isLoading && isLoadingMore
        text: "LOADING MORE... " + allItems.length
        color: root.tertiaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        elide: Text.ElideRight
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: root.sh * 0.205
        anchors.rightMargin: root.sw * 0.125
        width: root.sw * 0.35
        horizontalAlignment: Text.AlignRight
        font.pixelSize: root.sh * 0.0333333
    }

    Text {
        visible: filterText !== ""
        text: "FILTER: " + filterText + " (" + items.length + "/" + allItems.length + ")"
        color: root.secondaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        elide: Text.ElideRight
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.205
        anchors.leftMargin: root.sw * 0.125
        width: isLoadingMore ? root.sw * 0.38 : root.sw * 0.75
        font.pixelSize: root.sh * 0.0333333
    }

    Item {
        id: seriesDetails
        visible: showSeriesMetadata
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.76875
        height: root.sh * 0.21875
        clip: true

        Column {
            anchors.fill: parent
            spacing: root.sh * 0.0104167

            Text {
                text: JellyfinMedia.primaryTitle(seriesItem)
                color: root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                elide: Text.ElideRight
                width: parent.width
                font.pixelSize: root.sh * 0.05
            }

            Text {
                text: JellyfinMedia.seriesMetadataLine(seriesItem, allItems.length)
                color: root.secondaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                elide: Text.ElideRight
                width: parent.width
                font.pixelSize: root.sh * 0.0333333
            }

            Text {
                visible: JellyfinMedia.genreLine(seriesItem) !== ""
                text: JellyfinMedia.genreLine(seriesItem)
                color: root.tertiaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                elide: Text.ElideRight
                width: parent.width
                font.pixelSize: root.sh * 0.0291667
            }

            Item {
                id: seriesSummaryContainer
                width: parent.width
                height: root.sh * 0.0875
                clip: true

                Text {
                    id: seriesSummaryText
                    anchors.left: parent.left
                    anchors.right: parent.right
                    text: seriesItem.summary || ""
                    color: root.primaryColor
                    font.family: root.globalFont
                    wrapMode: Text.WordWrap
                    font.pixelSize: root.sh * 0.0291667
                    lineHeight: 1.3
                }

                SequentialAnimation {
                    running: showSeriesMetadata &&
                             seriesSummaryText.text !== "" &&
                             seriesSummaryText.implicitHeight > seriesSummaryContainer.height
                    loops: Animation.Infinite
                    onRunningChanged: if (!running) seriesSummaryText.y = 0
                    PauseAnimation { duration: 3000 }
                    NumberAnimation {
                        target: seriesSummaryText
                        property: "y"
                        to: seriesSummaryContainer.height - seriesSummaryText.implicitHeight
                        duration: Math.abs(to) * 120
                    }
                    PauseAnimation { duration: 4000 }
                    PropertyAction { target: seriesSummaryText; property: "y"; value: 0 }
                }
            }
        }
    }

    ListView {
        id: itemList
        objectName: "jellyfinItemsList"
        model: items
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: showSeriesMetadata ? root.sh * 0.4895833 : root.sh * 0.25
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.76875
        height: showSeriesMetadata ? root.sh * 0.2854167 : root.sh * 0.525
        clip: true
        focus: true

        Keys.onPressed: function(event) { itemListRoot.handleKey(event) }

        delegate: SelectableMarqueeRow {
            width: itemList.width
            height: root.sh * 0.0583333
            label: (itemListRoot.showWatchedIndicators ? JellyfinMedia.watchedPrefix(modelData) : "") +
                   JellyfinMedia.listTitle(modelData, mode === "continue" || mode === "upnext")
            selected: itemList.currentIndex === index
        }
    }

    Text {
        text: filterText !== "" ? "[ESC]:CLEAR [BS]:DELETE [▲▼]:NAVIGATE [ENTER]:SELECT"
                                : "[TYPE]:FILTER [ESC]:BACK [▲▼]:NAVIGATE [ENTER]:SELECT"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
    }
}
