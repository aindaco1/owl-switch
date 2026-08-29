import QtQuick
import Components

FocusScope {
    id: settingsRoot

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})
    property var navListState: ({})

    property var appSettings: ({})
    property var installedModules: []

    // Flat model: mix of section headers and rows
    property var settingsItems: []

    property bool quitOverlayVisible: false
    property int quitChoiceIndex: 0

    function buildModel() {
        var cfg = appCore.get_settings()
        appSettings = cfg.app || {}
        installedModules = appCore.get_installed_modules()

        var items = []

        // APPLICATION section
        var colorOpts = Object.keys(root.allThemes)
        items.push({
            type: "list_single",
            key: "startup_module",
            label: "Start on Module",
            options: ["None"],
            values: ["None"],
            value: "None",
            description: "Open an enabled module immediately at launch.",
            moduleId: ""
        })
        var startupRow = items[items.length - 1]
        var startupId = appSettings["startup_module"] || "None"
        for (var sm = 0; sm < installedModules.length; sm++) {
            if (!installedModules[sm].enabled) continue
            startupRow.options.push(installedModules[sm].name)
            startupRow.values.push(installedModules[sm].id)
            if (installedModules[sm].id === startupId)
                startupRow.value = installedModules[sm].name
        }

        var displays = appCore.displayOptions()
        var controllerLabels = ["Automatic"]
        var controllerValues = [-1]
        var mediaLabels = ["Automatic", "Same as Controller"]
        var mediaValues = [-1, -2]
        for (var displayIndex = 0; displayIndex < displays.length; displayIndex++) {
            controllerLabels.push(displays[displayIndex].label)
            controllerValues.push(displays[displayIndex].id)
            mediaLabels.push(displays[displayIndex].label)
            mediaValues.push(displays[displayIndex].id)
        }

        var controllerStored = appSettings["controller_display_index"] !== undefined
                ? Number(appSettings["controller_display_index"]) : -1
        var controllerOption = controllerValues.indexOf(controllerStored)
        if (controllerOption < 0) controllerOption = 0
        items.push({
            type: "list_single",
            key: "controller_display_index",
            label: "Controller Display",
            options: controllerLabels,
            values: controllerValues,
            value: controllerLabels[controllerOption],
            description: "Display for menus and controls. Takes effect after restart.",
            moduleId: ""
        })

        var mediaStored = appSettings["media_display_index"] !== undefined
                ? Number(appSettings["media_display_index"]) : -1
        var mediaOption = mediaValues.indexOf(mediaStored)
        if (mediaOption < 0) mediaOption = 0
        items.push({
            type: "list_single",
            key: "media_display_index",
            label: "Media Display",
            options: mediaLabels,
            values: mediaValues,
            value: mediaLabels[mediaOption],
            description: "Display for video and full-screen media. Takes effect after restart.",
            moduleId: ""
        })
        items.push({
            type: "list_single",
            key: "color_scheme",
            label: "Color Scheme",
            options: colorOpts,
            value: appSettings["color_scheme"] || "Video 1",
            moduleId: ""
        })
        items.push({
            type: "list_single",
            key: "auto_crop",
            label: "Auto Crop",
            options: ["Off", "On"],
            value: appSettings["auto_crop"] || "Off",
            description: "Start video zoomed to fill the display; the OSD Crop control still toggles it.",
            moduleId: ""
        })
        items.push({
            type: "list_single",
            key: "video_output_levels",
            label: "Video Levels",
            options: ["Auto", "Limited", "Full"],
            value: appSettings["video_output_levels"] || "Auto",
            description: "Override output range when video looks washed out or crushed.",
            moduleId: ""
        })
        items.push({
            type: "list_single",
            key: "screensaver_timeout",
            label: "Screen Saver",
            options: ["OFF", "30", "60", "120"],
            value: appSettings["screensaver_timeout"] || "OFF",
            description: "Protect the display after menu inactivity or paused playback, in seconds.",
            moduleId: ""
        })
        items.push({
            type: "list_single",
            key: "prevent_sleep",
            label: "Prevent Sleep",
            options: ["ON", "OFF"],
            value: appSettings["prevent_sleep"] || "ON",
            moduleId: ""
        })
        items.push({
            type: "list_single",
            key: "battery_sleep_threshold",
            label: "Battery Sleep At",
            options: ["OFF", "5%", "10%", "15%", "20%"],
            value: appSettings["battery_sleep_threshold"] || "10%",
            moduleId: ""
        })

        // MODULES section — only show modules with has_settings
        var hasModuleSettings = false
        for (var i = 0; i < installedModules.length; i++) {
            if (installedModules[i].has_settings) { hasModuleSettings = true; break }
        }

        if (hasModuleSettings) {
            items.push({ type: "section", label: "Modules:" })
            for (var j = 0; j < installedModules.length; j++) {
                var m = installedModules[j]
                if (m.has_settings) {
                    items.push({ type: "submenu", label: m.name, moduleId: m.id })
                }
            }
        }

        // SYSTEM section
        items.push({ type: "section", label: "System:" })
        items.push({ type: "system_submenu", label: "Diagnostics", path: "views/Diagnostics.qml" })
        items.push({ type: "system_submenu", label: "Software Update", path: "views/Update.qml" })
        items.push({ type: "quit", label: "Quit OwlSwitch" })

        settingsItems = items

        // Restore saved position, or default to first selectable row
        if (navListState.currentIndex !== undefined) {
            settingsList.currentIndex = Math.min(navListState.currentIndex, items.length - 1)
        } else {
            for (var k = 0; k < items.length; k++) {
                if (items[k].type !== "section") {
                    settingsList.currentIndex = k
                    break
                }
            }
        }
        settingsList.positionViewAtIndex(settingsList.currentIndex, ListView.Contain)
    }

    function nextSelectable(idx, direction) {
        if (settingsItems.length === 0) return -1
        for (var step = 1; step <= settingsItems.length; step++) {
            var candidate = (idx + direction * step + settingsItems.length) % settingsItems.length
            if (settingsItems[candidate].type !== "section") return candidate
        }
        return idx
    }

    Component.onCompleted: buildModel()

    // Header
    AppBar {
        iconSource: "../../assets/images/settings.svg"
        title: "Settings"
        subtitle: appCore.appVersion
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125 //60
        anchors.leftMargin: root.sw * 0.125 //80
    }

    property string ipAddress: ""
    Timer {
        interval: 5000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: settingsRoot.ipAddress = appCore.localIpAddress()
    }
    Text {
        text: settingsRoot.ipAddress
        visible: settingsRoot.ipAddress !== ""
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: root.sh * 0.125
        anchors.rightMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0291667
        color: root.tertiaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        topPadding: root.sh * 0.0125
        rightPadding: root.sw * 0.00625
    }

    ListView {
        id: settingsList
        model: settingsItems
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.25 //120
        anchors.leftMargin: root.sw * 0.115625 //74
        width: root.sw * 0.76875 //492
        height: root.sh * 0.525 //252
        clip: true
        focus: true

        Keys.onUpPressed: {
            currentIndex = settingsRoot.nextSelectable(currentIndex, -1)
            settingsList.positionViewAtIndex(currentIndex, ListView.Contain)
        }
        Keys.onDownPressed: {
            currentIndex = settingsRoot.nextSelectable(currentIndex, 1)
            settingsList.positionViewAtIndex(currentIndex, ListView.Contain)
        }

        Keys.onLeftPressed: {
            var row = settingsItems[currentIndex]
            if (row && row.type === "list_single") {
                var opts = row.options
                var idx = opts.indexOf(row.value)
                var newIdx = (idx - 1 + opts.length) % opts.length
                var newVal = opts[newIdx]
                var savedVal = row.values ? row.values[newIdx] : newVal
                var updated = settingsItems.slice()
                updated[currentIndex] = Object.assign({}, row, { value: newVal })
                var savedIndex = currentIndex
                settingsItems = updated
                currentIndex = savedIndex
                appCore.save_setting(row.moduleId, row.key, savedVal)
            }
        }

        Keys.onRightPressed: {
            var row = settingsItems[currentIndex]
            if (row && row.type === "list_single") {
                var opts = row.options
                var idx = opts.indexOf(row.value)
                var newIdx = (idx + 1) % opts.length
                var newVal = opts[newIdx]
                var savedVal = row.values ? row.values[newIdx] : newVal
                var updated = settingsItems.slice()
                updated[currentIndex] = Object.assign({}, row, { value: newVal })
                var savedIndex = currentIndex
                settingsItems = updated
                currentIndex = savedIndex
                appCore.save_setting(row.moduleId, row.key, savedVal)
            }
        }

        Keys.onReturnPressed: {
            var row = settingsItems[currentIndex]
            if (row && row.type === "submenu") {
                settingsRoot.navigateTo("views/ModuleSettings.qml", { moduleId: row.moduleId }, { currentIndex: settingsList.currentIndex })
            } else if (row && row.type === "system_submenu") {
                settingsRoot.navigateTo(row.path, {}, { currentIndex: settingsList.currentIndex })
            } else if (row && row.type === "quit") {
                settingsRoot.quitChoiceIndex = 0
                settingsRoot.quitOverlayVisible = true
            }
        }

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                settingsRoot.goBack()
                event.accepted = true
            }
        }

        delegate: Item {
            width: settingsList.width
            height: root.sh * 0.0583333 //28

            // --- SECTION LABEL ---
            Text {
                visible: modelData.type == "section"
                text: modelData.label || ""
                color: root.secondaryColor
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                anchors.verticalCenter: parent.verticalCenter
                topPadding: root.sh * 0.0020833 //1
                leftPadding: root.sw * 0.009375 //6
                rightPadding: root.sw * 0.009375 //6
                font.pixelSize: root.sh * 0.0291667 //14
            }

            // --- SELECTABLE ROW ---
            Rectangle {
                visible: modelData.type !== "section"
                anchors.fill: parent
                color: settingsList.currentIndex === index ? root.accentColor : "transparent"

                // Label
                Text {
                    text: modelData.label || ""
                    color: settingsList.currentIndex === index ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    anchors.verticalCenter: parent.verticalCenter
                    x: 0
                    topPadding: root.sh * 0.0041667 //2
                    leftPadding: root.sw * 0.009375 //6
                    rightPadding: root.sw * 0.009375 //6
                    bottomPadding: root.sh * 0.00625 //3
                    font.pixelSize: root.sh * 0.05 //24
                }

                // Value / arrow indicator
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: root.sw * 0.009375 //6
                    spacing: root.sw * 0.00625 //4

                    Text {
                        visible: modelData.type === "list_single"
                        text: "\u25C4"
                        color: settingsList.currentIndex === index ? root.surfaceColor : root.tertiaryColor
                        font.family: root.globalFont
                        anchors.verticalCenter: parent.verticalCenter
                        topPadding: root.sh * 0.0041667 //2
                        bottomPadding: root.sh * 0.00625 //3
                        font.pixelSize: root.sh * 0.0375 //18
                    }
                    Item {
                        id: valueClip
                        visible: modelData.type === "list_single"
                        width: Math.min(valueText.implicitWidth, root.sw * 0.35)
                        height: parent.height
                        clip: true

                        Text {
                            id: valueText
                            text: modelData.value || ""
                            color: settingsList.currentIndex === index ? root.surfaceColor : root.primaryColor
                            font.family: root.globalFont
                            font.capitalization: Font.AllUppercase
                            anchors.verticalCenter: parent.verticalCenter
                            x: 0
                            topPadding: root.sh * 0.0041667 //2
                            leftPadding: root.sw * 0.009375 //6
                            rightPadding: root.sw * 0.009375 //6
                            bottomPadding: root.sh * 0.00625 //3
                            font.pixelSize: root.sh * 0.05 //24
                        }

                        SequentialAnimation {
                            running: settingsList.currentIndex === index && valueText.implicitWidth > valueClip.width
                            loops: Animation.Infinite
                            onRunningChanged: if (!running) valueText.x = 0

                            PauseAnimation { duration: 1500 }
                            NumberAnimation {
                                target: valueText
                                property: "x"
                                to: valueClip.width - valueText.implicitWidth
                                duration: Math.abs(to) * 20
                            }
                            PauseAnimation { duration: 2000 }
                            PropertyAction { target: valueText; property: "x"; value: 0 }
                        }
                    }
                    Text {
                        visible: modelData.type === "submenu" || modelData.type === "system_submenu" || modelData.type === "list_single"
                        text: "\u25BA"
                        color: settingsList.currentIndex === index ? root.surfaceColor : root.tertiaryColor
                        font.family: root.globalFont
                        anchors.verticalCenter: parent.verticalCenter
                        topPadding: root.sh * 0.0041667 //2
                        bottomPadding: root.sh * 0.00625 //3
                        font.pixelSize: root.sh * 0.0375 //18
                    }
                }
            }
        }
    }

    Rectangle {
        id: rowHelpBackground
        property var currentRow: settingsRoot.settingsItems[settingsList.currentIndex]
        visible: !!(currentRow && currentRow.description)
        color: root.accentColor
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1583333
        anchors.leftMargin: root.sw * 0.125
        width: root.sw * 0.75
        height: root.sh * 0.0583333
        clip: true

        Text {
            text: (rowHelpBackground.currentRow && rowHelpBackground.currentRow.description) || ""
            color: root.surfaceColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.0291667
            wrapMode: Text.WordWrap
            anchors.fill: parent
            anchors.margins: root.sw * 0.0125
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    // --- FOOTER ---
    Text {
        id: footer
        text: "[ESC]:BACK [\u25B2\u25BC]:NAVIGATE [\u25C4\u25BA]:CHANGE [ENTER]:SELECT"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667 //50
        anchors.leftMargin: root.sw * 0.125 //80
        font.pixelSize: root.sh * 0.0333333 //16
    }

    // --- QUIT CONFIRMATION OVERLAY ---
    Rectangle {
        anchors.fill: parent
        color: root.surfaceColor
        visible: quitOverlayVisible
        focus: quitOverlayVisible

        Keys.onUpPressed:   { quitChoiceIndex = 0 }
        Keys.onDownPressed: { quitChoiceIndex = 1 }
        Keys.onReturnPressed: {
            if (quitChoiceIndex === 0) Qt.quit()
            else { quitOverlayVisible = false; settingsList.forceActiveFocus() }
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                quitOverlayVisible = false
                settingsList.forceActiveFocus()
                event.accepted = true
            }
        }

        Rectangle {
            color: root.surfaceColor
            anchors.centerIn: parent
            width: root.sw * 0.76875   //492
            height: root.sh * 0.2833333 //136

            Column {
                id: quitDialogColumn
                anchors.fill: parent
                spacing: root.sh * 0.05 //24

                Text {
                    text: "REALLY QUIT?"
                    color: root.secondaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.0333333 //16
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Column {
                    Repeater {
                        model: ["Yes", "No"]
                        delegate: Item {
                            width: quitDialogColumn.width
                            height: root.sh * 0.0583333 //28

                            Rectangle {
                                anchors.fill: quitOptionText
                                color: root.accentColor
                                visible: index === quitChoiceIndex
                            }

                            Text {
                                id: quitOptionText
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData
                                color: index === quitChoiceIndex ? root.surfaceColor : root.primaryColor
                                font.family: root.globalFont
                                font.capitalization: Font.AllUppercase
                                topPadding: root.sh * 0.0041667 //2
                                leftPadding: root.sw * 0.009375 //6
                                rightPadding: root.sw * 0.009375 //6
                                bottomPadding: root.sh * 0.00625 //3
                                font.pixelSize: root.sh * 0.05 //24
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
}
