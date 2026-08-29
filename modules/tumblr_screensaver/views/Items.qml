import QtQuick
import Components

FocusScope {
    id: itemsRoot

    property var navParams: ({})
    property var navListState: navParams.navListState || ({})
    property int focusRow: 0
    property var favorites: []
    property string statusMessage: ""
    readonly property string moduleId: "com.owlswitch.tumblr_screensaver"
    readonly property string defaultUrl: "https://pixelskylines.tumblr.com/"
    readonly property int firstFavoriteRow: 3
    readonly property int maxFocusRow: firstFavoriteRow + favorites.length - 1

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    focus: true

    function normalizedUrl(value) {
        return tumblrScreensaverBackend.normalizeBlogUrl(String(value || ""))
    }

    function savedUrl() {
        var saved = appCore.get_setting(moduleId, "tumblr_url")
        var normalized = normalizedUrl(saved)
        return normalized !== "" ? normalized : defaultUrl
    }

    function favoriteIndexFor(value) {
        var normalized = normalizedUrl(value)
        for (var i = 0; i < favorites.length; i++) {
            if (favorites[i] === normalized)
                return i
        }
        return -1
    }

    function favoriteLabel(value) {
        var label = String(value || "").replace(/^https?:\/\//i, "")
        label = label.split("/")[0]
        label = label.replace(/\.tumblr\.com$/i, "")
        return label || value
    }

    function loadFavorites() {
        var stored = appCore.get_setting(moduleId, "favorites")
        var loaded = []
        if (stored && stored.length !== undefined) {
            for (var i = 0; i < stored.length; i++) {
                var normalized = normalizedUrl(stored[i])
                if (normalized !== "" && loaded.indexOf(normalized) < 0)
                    loaded.push(normalized)
            }
        }
        favorites = loaded
        appCore.save_setting(moduleId, "favorites", loaded)
    }

    function saveFavorites(values) {
        favorites = values.slice()
        appCore.save_setting(moduleId, "favorites", favorites)
    }

    function showStatus(message) {
        statusMessage = message
        statusTimer.restart()
    }

    function setFocusRow(row) {
        var upper = Math.max(2, maxFocusRow)
        focusRow = Math.max(0, Math.min(upper, row))
        if (focusRow === 0) {
            urlInput.forceActiveFocus()
        } else if (focusRow === 1) {
            startButton.forceActiveFocus()
        } else if (focusRow === 2) {
            favoriteButton.forceActiveFocus()
        } else {
            favoritesList.currentIndex = focusRow - firstFavoriteRow
            favoritesList.positionViewAtIndex(favoritesList.currentIndex, ListView.Contain)
            favoritesList.forceActiveFocus()
        }
    }

    function startMontage(value) {
        var normalized = normalizedUrl(value === undefined ? urlInput.text : value)
        if (normalized === "") {
            showStatus("ENTER A VALID TUMBLR")
            return
        }
        urlInput.text = normalized
        appCore.save_setting(moduleId, "tumblr_url", normalized)
        navigateTo("Player.qml", { tumblrUrl: normalized }, { focusRow: focusRow })
    }

    function toggleFavoriteForInput() {
        var normalized = normalizedUrl(urlInput.text)
        if (normalized === "") {
            showStatus("ENTER A VALID TUMBLR")
            return
        }
        urlInput.text = normalized
        var index = favoriteIndexFor(normalized)
        var updated = favorites.slice()
        if (index >= 0) {
            updated.splice(index, 1)
            saveFavorites(updated)
            showStatus("REMOVED FROM FAVORITES")
        } else {
            updated.push(normalized)
            saveFavorites(updated)
            showStatus("SAVED TO FAVORITES")
        }
        setFocusRow(2)
    }

    function removeFavorite(index) {
        if (index < 0 || index >= favorites.length)
            return
        var updated = favorites.slice()
        updated.splice(index, 1)
        saveFavorites(updated)
        showStatus("REMOVED FROM FAVORITES")
        if (updated.length === 0)
            setFocusRow(2)
        else
            setFocusRow(firstFavoriteRow + Math.min(index, updated.length - 1))
    }

    function activateFocusedRow() {
        if (focusRow === 0 || focusRow === 1)
            startMontage()
        else if (focusRow === 2)
            toggleFavoriteForInput()
        else {
            var index = focusRow - firstFavoriteRow
            if (index >= 0 && index < favorites.length) {
                urlInput.text = favorites[index]
                startMontage(favorites[index])
            }
        }
    }

    function handleNavigationKey(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        } else if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
            if (focusRow >= firstFavoriteRow)
                removeFavorite(focusRow - firstFavoriteRow)
            else if (focusRow !== 0)
                goBack()
            event.accepted = focusRow !== 0
        } else if (event.key === Qt.Key_Tab) {
            var count = Math.max(3, firstFavoriteRow + favorites.length)
            setFocusRow((focusRow + 1) % count)
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            setFocusRow(focusRow - 1)
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            setFocusRow(focusRow + 1)
            event.accepted = true
        } else if (event.key === Qt.Key_Left && focusRow === 2) {
            setFocusRow(1)
            event.accepted = true
        } else if (event.key === Qt.Key_Right && focusRow === 1) {
            setFocusRow(2)
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            activateFocusedRow()
            event.accepted = true
        }
    }

    Component.onCompleted: {
        loadFavorites()
        urlInput.text = savedUrl()
        var restore = navListState.focusRow !== undefined ? navListState.focusRow : 0
        setFocusRow(restore)
        if (focusRow === 0)
            urlInput.selectAll()
    }

    Keys.onPressed: function(event) { handleNavigationKey(event) }

    AppBar {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
    }

    Text {
        id: urlLabel
        text: "TUMBLR URL"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.1958333
        anchors.leftMargin: root.sw * 0.125
        width: root.sw * 0.76875
        elide: Text.ElideRight
        font.pixelSize: root.sh * 0.0291667
    }

    Rectangle {
        id: inputFrame
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.2375
        anchors.leftMargin: root.sw * 0.115625
        width: root.sw * 0.76875
        height: root.sh * 0.0666667
        color: focusRow === 0 ? root.accentColor : "transparent"
        border.color: focusRow === 0 ? root.accentColor : root.tertiaryColor
        border.width: Math.max(1, root.sh * 0.002)

        TextInput {
            id: urlInput
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: root.sw * 0.0125
            anchors.rightMargin: root.sw * 0.0125
            color: focusRow === 0 ? root.surfaceColor : root.primaryColor
            selectedTextColor: root.surfaceColor
            selectionColor: root.accentColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0416667
            clip: true
            focus: focusRow === 0
            selectByMouse: false
            activeFocusOnTab: true
            inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase

            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
                    itemsRoot.goBack()
                    event.accepted = true
                } else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Down) {
                    itemsRoot.setFocusRow(1)
                    event.accepted = true
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    itemsRoot.startMontage()
                    event.accepted = true
                }
            }
        }
    }

    Item {
        id: startButton
        anchors.top: inputFrame.bottom
        anchors.left: inputFrame.left
        anchors.topMargin: root.sh * 0.025
        width: root.sw * 0.30
        height: root.sh * 0.0583333
        focus: focusRow === 1

        Rectangle { color: focusRow === 1 ? root.accentColor : "transparent"; anchors.fill: parent }
        Text {
            text: "START MONTAGE"
            color: focusRow === 1 ? root.surfaceColor : root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            anchors.verticalCenter: parent.verticalCenter
            leftPadding: root.sw * 0.009375
            rightPadding: root.sw * 0.009375
            font.pixelSize: root.sh * 0.0416667
        }
    }

    Item {
        id: favoriteButton
        anchors.top: inputFrame.bottom
        anchors.left: startButton.right
        anchors.topMargin: root.sh * 0.025
        anchors.leftMargin: root.sw * 0.025
        width: root.sw * 0.40
        height: root.sh * 0.0583333
        focus: focusRow === 2

        Rectangle { color: focusRow === 2 ? root.accentColor : "transparent"; anchors.fill: parent }
        Text {
            text: itemsRoot.favoriteIndexFor(urlInput.text) >= 0 ? "REMOVE FAVORITE" : "SAVE FAVORITE"
            color: focusRow === 2 ? root.surfaceColor : root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            anchors.verticalCenter: parent.verticalCenter
            leftPadding: root.sw * 0.009375
            rightPadding: root.sw * 0.009375
            font.pixelSize: root.sh * 0.0416667
        }
    }

    Text {
        id: favoritesLabel
        text: "FAVORITES " + favorites.length
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.top: startButton.bottom
        anchors.left: inputFrame.left
        anchors.topMargin: root.sh * 0.025
        font.pixelSize: root.sh * 0.0291667
    }

    ListView {
        id: favoritesList
        model: favorites
        anchors.top: favoritesLabel.bottom
        anchors.left: inputFrame.left
        anchors.topMargin: root.sh * 0.0083333
        width: inputFrame.width
        height: root.sh * 0.2458333
        clip: true
        focus: focusRow >= firstFavoriteRow
        keyNavigationEnabled: false
        Keys.onPressed: function(event) { itemsRoot.handleNavigationKey(event) }

        delegate: Item {
            width: favoritesList.width
            height: root.sh * 0.0583333

            Rectangle {
                anchors.fill: parent
                color: itemsRoot.focusRow === itemsRoot.firstFavoriteRow + index
                       ? root.accentColor : "transparent"
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                text: itemsRoot.favoriteLabel(modelData)
                color: itemsRoot.focusRow === itemsRoot.firstFavoriteRow + index
                       ? root.surfaceColor : root.primaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                elide: Text.ElideRight
                leftPadding: root.sw * 0.009375
                rightPadding: root.sw * 0.009375
                font.pixelSize: root.sh * 0.0416667
            }
        }
    }

    Text {
        visible: statusMessage !== ""
        text: statusMessage
        color: root.secondaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.rightMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0291667
    }

    Timer {
        id: statusTimer
        interval: 2400
        repeat: false
        onTriggered: statusMessage = ""
    }

    Text {
        text: "[ESC]:BACK [TAB/ARROWS]:NAVIGATE [ENTER]:SELECT [DEL]:REMOVE"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0291667
    }
}
