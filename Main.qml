import QtQuick
import QtQuick.Controls
import QtQuick.Window
import Components

Window {
    id: root
    flags: Qt.FramelessWindowHint | Qt.Window
    x:      Qt.platform.os === "osx" ? macScreenX      : Screen.virtualX
    y:      Qt.platform.os === "osx" ? macScreenY      : Screen.virtualY
    width:  Qt.platform.os === "osx" ? macScreenWidth  : Screen.width
    height: Qt.platform.os === "osx" ? macScreenHeight : Screen.height
    visible: true
    color: root.surfaceColor

    readonly property bool hasMediaOutputScreen: !!hasExternalMediaScreen
    property bool mediaOutputActive: false
    property bool mediaOutputOpaque: true
    property bool mediaOutputAcceptsFocus: false
    property alias mediaOutputLayer: mediaOutputLayerItem
    property bool updatePromptVisible: false
    property string updatePromptVersion: ""

    signal mediaOutputKeyPressed(var event)

    function openMediaOutput(opaque, acceptsFocus) {
        if (!root.hasMediaOutputScreen || !mediaOutputLayerItem)
            return false
        root.mediaOutputOpaque = opaque !== false
        root.mediaOutputAcceptsFocus = acceptsFocus === true
        root.mediaOutputActive = true
        return true
    }

    function closeMediaOutput() {
        root.mediaOutputActive = false
        root.mediaOutputAcceptsFocus = false
    }

    function navigateTo(path, params, listState) {
        root.appNavStack.push({
            source: moduleLoader.source,
            params: root.appCurrentParams,
            listState: listState || {}
        })
        root.appCurrentParams = params || {}
        moduleLoader.setSource(path, { "navParams": params || {} })
    }

    // --- Color Schemes ---
    readonly property var themes: ({
        "Video 1": {
            "primary": "#FFFFFF",
            "secondary": "#C2BFE4",
            "tertiary": "#8480C9",
            "surface": "#0A0094",
            "accent": "#AECFFF"
        },
        "Late Night": {
            "primary": "#FFFFFF",
            "secondary": "#A1A1A1",
            "tertiary": "#444444",
            "surface": "#000000",
            "accent": "#FFD900"
        },
        "Synthwave": {
            "primary": "#FFFFFF",
            "secondary": "#D48BFF",
            "tertiary": "#7836B5",
            "surface": "#12012B",
            "accent": "#00E5FF"
        },
        "Terminal": {
            "primary": "#4AF626",
            "secondary": "#32A81B",
            "tertiary": "#1A590E",
            "surface": "#000000",
            "accent": "#4AF626"
        },
        "T-120": {
            "primary": "#000000",
            "secondary": "#818181",
            "tertiary": "#df9c27",
            "surface": "#FAF5E8",
            "accent": "#EE442F"
        },
        "Amber": {
            "primary": "#FFB000",
            "secondary": "#B37B00",
            "tertiary": "#B37B00",
            "surface": "#000000",
            "accent": "#FFEE11"
        },
        "Kinescope": {
            "primary": "#FFFFFF",
            "secondary": "#9E9E9E",
            "tertiary": "#424242",
            "surface": "#121212",
            "accent": "#FFFFFF"
        },
        "SMPTE ECR 1-1978": {
            "primary": "#BFBFBF",
            "secondary": "#66BF66",
            "tertiary": "#6666BF",
            "surface": "#131313",
            "accent": "#BF6666"
        }
    })
    property var allThemes: themes  // may gain a "Custom" entry on startup
    property string currentTheme: "Video 1"
    property string primaryColor:   (allThemes[currentTheme] || allThemes["Video 1"]).primary
    property string secondaryColor: (allThemes[currentTheme] || allThemes["Video 1"]).secondary
    property string tertiaryColor:  (allThemes[currentTheme] || allThemes["Video 1"]).tertiary
    property string surfaceColor:   (allThemes[currentTheme] || allThemes["Video 1"]).surface
    property string accentColor:    (allThemes[currentTheme] || allThemes["Video 1"]).accent

    readonly property real sw: width
    readonly property real sh: height
    readonly property var hints: inputManager.hints

    Connections {
        target: appCore
        function onAppSettingChanged(key, value) {
            if (key === "color_scheme") {
                root.currentTheme = value
            } else if (key === "screensaver_timeout") {
                var seconds = parseInt(value)
                idleTracker.enabled = seconds > 0
                if (seconds > 0) idleTracker.threshold = seconds
                if (!(seconds > 0)) root.screenSaverActive = false
            }
        }
    }

    Component.onCompleted: {
        var cfg = appCore.get_settings()

        var customThemes = appCore.getCustomColorSchemes()
        if (Object.keys(customThemes).length > 0) {
            var mergedThemes = Object.assign({}, themes, root.allThemes)
            for (var customName in customThemes) {
                if (Object.keys(customThemes[customName]).length === 5)
                    mergedThemes[customName] = customThemes[customName]
            }
            root.allThemes = mergedThemes
        }

        var custom = appCore.getCustomColorScheme()
        if (Object.keys(custom).length === 5) {
            var t = Object.assign({}, root.allThemes)
            t["Custom"] = custom
            root.allThemes = t
        }

        var savedTheme = (cfg.app && cfg.app.color_scheme) || "Video 1"
        if (savedTheme === "Custom" && !root.allThemes["Custom"]) {
            appCore.save_setting("", "color_scheme", "Video 1")
            savedTheme = "Video 1"
        }
        root.currentTheme = savedTheme

        var screenSaverSeconds = parseInt(cfg.app && cfg.app.screensaver_timeout)
        idleTracker.enabled = screenSaverSeconds > 0
        if (screenSaverSeconds > 0) idleTracker.threshold = screenSaverSeconds

        // Break declarative bindings on macOS so the C++ NSWindow override
        // in forceWindowFullScreen() isn't immediately re-fought by QML.
        if (Qt.platform.os === "osx") {
            root.x = macScreenX
            root.y = macScreenY
            root.width = macScreenWidth
            root.height = macScreenHeight
        }
    }
    
    FontLoader { 
        id: font; source: "assets/fonts/VCR_OSD_MONO_1.001.ttf" 
    }
    // Register a retro-styled fallback for glyphs VCR OSD Mono does not cover,
    // including CJK and Hangul. Qt's normal missing-glyph fallback selects it.
    FontLoader {
        id: unifontLoader; source: "assets/fonts/unifont.otf"
    }
    property string globalFont: font.name;

    // --- APP-LEVEL NAV STACK ---
    property var appNavStack: []
    property var appCurrentParams: ({})
    property bool startupNavigated: false
    property bool screenSaverActive: false

    Connections {
        target: mpvController
        function onPositionChanged(ms) {
            if (ms > 0 && !idleTracker.mpvActive)
                idleTracker.mpvActive = true
        }
        function onPlaybackEnded(finalPositionMs, finalDurationMs, reason) {
            idleTracker.mpvActive = false
        }
    }

    Connections {
        target: idleTracker
        function onActiveChanged() {
            if (idleTracker.active && idleTracker.enabled) {
                root.screenSaverActive = true
                var maxX = Math.max(1, screenSaverOverlay.width - bounceLogo.width)
                var maxY = Math.max(1, screenSaverOverlay.height - bounceLogo.height)
                bounceLogo.x = Math.random() * maxX
                bounceLogo.y = Math.random() * maxY
                bounceLogo.vx = Math.random() > 0.5 ? 2 : -2
                bounceLogo.vy = Math.random() > 0.5 ? 2 : -2
                screenSaverOverlay.forceActiveFocus()
            }
        }
    }

    Connections {
        target: updateManager
        function onLaunchUpdateAvailable(version) {
            root.updatePromptVersion = version
            root.updatePromptVisible = true
            Qt.callLater(function() { updatePrompt.forceActiveFocus() })
        }
    }

    // --- MODULE LOADER ---
    Loader {
        id: moduleLoader;
        anchors.fill: parent;
        focus: true;
        source: "views/ModuleList.qml";

        Keys.onPressed: (event) => {
            if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Q) {
                Qt.quit()
            }
        }

        onLoaded: {
            item.forceActiveFocus()
            if (!root.startupNavigated) {
                root.startupNavigated = true
                var startupEntry = appCore.startupModuleEntryPoint()
                if (startupEntry) {
                    root.appNavStack.push({ source: moduleLoader.source, params: {}, listState: {} })
                    moduleLoader.setSource(startupEntry, { "navParams": {} })
                }
            }
        }

        Connections {
            target: moduleLoader.item
            ignoreUnknownSignals: true

            function onNavigateTo(path, params, listState) {
                root.navigateTo(path, params, listState)
            }

            function onGoBack() {
                if (root.appNavStack.length === 0) return
                var prev = root.appNavStack.pop()
                root.appCurrentParams = prev.params
                moduleLoader.setSource(prev.source, { "navParams": prev.params, "navListState": prev.listState || {} })
            }

        }
    }

    ConfirmDialog {
        id: updatePrompt
        visible: root.updatePromptVisible
        prompt: "VERSION " + root.updatePromptVersion + " IS AVAILABLE"
        confirmLabel: "VIEW"
        cancelLabel: "LATER"
        choiceIndex: 1
        onAccepted: {
            root.updatePromptVisible = false
            root.navigateTo("views/Update.qml", {}, {})
        }
        onRejected: {
            root.updatePromptVisible = false
            moduleLoader.forceActiveFocus()
        }
    }

    Item {
        id: screenSaverOverlay
        anchors.fill: parent
        visible: root.screenSaverActive
        focus: visible
        z: 9999

        Rectangle { anchors.fill: parent; color: "black" }

        Image {
            id: bounceLogo
            source: "assets/images/logo.svg"
            width: root.sw * 0.1
            height: width * 0.55
            fillMode: Image.PreserveAspectFit
            property real vx: 2
            property real vy: 2

            Timer {
                interval: 16
                repeat: true
                running: root.screenSaverActive
                onTriggered: {
                    bounceLogo.x += bounceLogo.vx
                    bounceLogo.y += bounceLogo.vy
                    if (bounceLogo.x + bounceLogo.width >= screenSaverOverlay.width) {
                        bounceLogo.x = screenSaverOverlay.width - bounceLogo.width
                        bounceLogo.vx = -Math.abs(bounceLogo.vx)
                    } else if (bounceLogo.x <= 0) {
                        bounceLogo.x = 0; bounceLogo.vx = Math.abs(bounceLogo.vx)
                    }
                    if (bounceLogo.y + bounceLogo.height >= screenSaverOverlay.height) {
                        bounceLogo.y = screenSaverOverlay.height - bounceLogo.height
                        bounceLogo.vy = -Math.abs(bounceLogo.vy)
                    } else if (bounceLogo.y <= 0) {
                        bounceLogo.y = 0; bounceLogo.vy = Math.abs(bounceLogo.vy)
                    }
                }
            }
        }

        Keys.onPressed: function(event) {
            event.accepted = true
            if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Q) {
                Qt.quit()
                return
            }
            root.screenSaverActive = false
            idleTracker.resetActivity()
            moduleLoader.forceActiveFocus()
        }
    }

    Window {
        id: mediaOutputWindow
        flags: {
            var windowFlags = Qt.FramelessWindowHint | Qt.Window | Qt.WindowStaysOnTopHint
            if (!root.mediaOutputAcceptsFocus)
                windowFlags |= Qt.WindowDoesNotAcceptFocus
            if (!root.mediaOutputOpaque)
                windowFlags |= Qt.WindowTransparentForInput
            return windowFlags
        }
        visible: root.mediaOutputActive && root.hasMediaOutputScreen
        x: externalMediaScreenX
        y: externalMediaScreenY
        width: externalMediaScreenWidth
        height: externalMediaScreenHeight
        color: root.mediaOutputOpaque ? "black" : "transparent"

        Item {
            id: mediaOutputLayerItem
            anchors.fill: parent
            focus: root.mediaOutputAcceptsFocus

            Keys.onPressed: function(event) {
                root.mediaOutputKeyPressed(event)
            }
        }

        onVisibleChanged: {
            if (visible && root.mediaOutputAcceptsFocus)
                mediaOutputLayerItem.forceActiveFocus()
        }
    }
}
