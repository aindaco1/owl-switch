#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QDir>
#include <QStandardPaths>
#include <QTimer>
#include <QCursor>
#include <QDebug>
#include <QElapsedTimer>
#include <QWindow>
#include <QQuickWindow>
#include <QScreen>
#include <QVariant>
#include <locale.h>

#include "AppCore.h"
#include "display/DisplaySelection.h"
#include "modules/local_files/LocalFilesBackend.h"
#include "modules/plex/PlexBackend.h"
#include "modules/jellyfin/JellyfinBackend.h"
#include "modules/karaoke/KaraokeBackend.h"
#include "modules/tumblr_screensaver/TumblrScreensaverBackend.h"
#include "modules/nature/NatureBackend.h"
#include "player/MpvController.h"
#include "input/IdleTracker.h"
#include "input/InputManager.h"
#include "update/UpdateManager.h"
#include "tools/YouTubeJob.h"
#include "diagnostics/DiagnosticsManager.h"
#ifdef Q_OS_MAC
#include "macos_utils.h"
#endif

static QString resolveAppRoot() {
    QString envRoot = qEnvironmentVariable("APP_ROOT");
    if (!envRoot.isEmpty())
        return QDir(envRoot).canonicalPath();

    QString appDir = QCoreApplication::applicationDirPath();

    if (QCoreApplication::applicationFilePath().contains(".app/Contents/MacOS/"))
        return QDir(appDir + "/../Resources").canonicalPath();

    QDir fhsData(appDir + "/../share/240-mp-jellyfin");
    if (fhsData.exists())
        return fhsData.canonicalPath();

    return QDir(appDir + "/..").canonicalPath();
}

static QString resolveDataRoot() {
    QString envRoot = qEnvironmentVariable("DATA_ROOT");
    if (!envRoot.isEmpty()) {
        QDir().mkpath(envRoot);
        return QDir(envRoot).canonicalPath();
    }

    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(path);
    return path;
}

static bool preventSleepEnabledFromSettings(AppCore &appCore) {
    const QString value = appCore.get_setting("", "prevent_sleep").toString().trimmed();
    return value.isEmpty() || value.compare(QStringLiteral("ON"), Qt::CaseInsensitive) == 0;
}

static int lowBatteryThresholdFromSettings(AppCore &appCore) {
    QString value = appCore.get_setting("", "battery_sleep_threshold").toString().trimmed();
    if (value.isEmpty())
        return 10;
    if (value.compare(QStringLiteral("OFF"), Qt::CaseInsensitive) == 0)
        return -1;
    value.remove('%');
    bool ok = false;
    const int threshold = value.toInt(&ok);
    return ok ? qBound(1, threshold, 100) : 10;
}

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    // Keep the internal application name stable so 1.6.4 reuses the existing
    // Application Support directory. The visible product name is OwlSwitch.
    app.setApplicationName("240-mp-jellyfin");
    app.setApplicationDisplayName(QStringLiteral("OwlSwitch"));
#ifdef APP_VERSION
    app.setApplicationVersion(QStringLiteral(APP_VERSION));
#else
    app.setApplicationVersion(QStringLiteral("dev"));
#endif

    // Hide cursor — this app is keyboard-only so the cursor serves no purpose.
    // On Linux, only hide on headless EGLFS (not desktop X11/Wayland sessions).
#ifdef Q_OS_LINUX
    if (qgetenv("DISPLAY").isEmpty() && qgetenv("WAYLAND_DISPLAY").isEmpty())
        QGuiApplication::setOverrideCursor(Qt::BlankCursor);
#endif
#ifdef Q_OS_MAC
    QGuiApplication::setOverrideCursor(Qt::BlankCursor);
    hideMacOSMenuBar();
#endif

    setlocale(LC_NUMERIC, "C");

    const QString appRoot  = resolveAppRoot();
    const QString dataRoot = resolveDataRoot();
    DiagnosticsManager diagnosticsManager(dataRoot);
    qDebug("[main] appRoot  = %s", qPrintable(appRoot));
    qDebug("[main] dataRoot = %s", qPrintable(dataRoot));

    QQmlApplicationEngine engine;

    AppCore             appCore(appRoot, dataRoot);
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (int index = 0; index < screens.size(); ++index) {
        const QRect geometry = screens.at(index)->geometry();
        qInfo("[main] display index %d: \"%s\" %dx%d at (%d,%d)",
              index, qPrintable(screens.at(index)->name()),
              geometry.width(), geometry.height(), geometry.x(), geometry.y());
    }

    const QVariant controllerValue = appCore.get_setting({}, QStringLiteral("controller_display_index"));
    const QVariant mediaValue = appCore.get_setting({}, QStringLiteral("media_display_index"));
    const int controllerSetting = controllerValue.isValid() && !controllerValue.isNull()
        ? controllerValue.toInt() : -1;
    const int mediaSetting = mediaValue.isValid() && !mediaValue.isNull()
        ? mediaValue.toInt() : -1;
    const int primaryIndex = screens.indexOf(QGuiApplication::primaryScreen());
    const DisplaySelection displaySelection = resolveDisplaySelection(
        screens.size(), primaryIndex, controllerSetting, mediaSetting);
    QScreen *controllerScreen = screens.value(displaySelection.controllerIndex,
                                               QGuiApplication::primaryScreen());
    QScreen *mediaScreen = screens.value(displaySelection.mediaIndex, controllerScreen);
    const QRect controllerGeometry = controllerScreen
        ? controllerScreen->geometry() : QRect(0, 0, 1920, 1080);
    const QRect mediaGeometry = mediaScreen ? mediaScreen->geometry() : controllerGeometry;
    qInfo("[main] display roles: controller=%d media=%d",
          displaySelection.controllerIndex, displaySelection.mediaIndex);
    LocalFilesBackend   localFiles(appRoot, dataRoot);
    PlexBackend         plexBackend(appRoot, dataRoot);
    JellyfinBackend     jellyfinBackend(appRoot, dataRoot);
    KaraokeBackend      karaokeBackend(appRoot, dataRoot);
    TumblrScreensaverBackend tumblrScreensaver;
    NatureBackend       natureBackend(dataRoot);
    MpvController       mpvController(appRoot, &appCore);
    mpvController.setPlaybackScreenIndex(displaySelection.mediaIndex);
    IdleTracker         idleTracker;
    InputManager        inputManager;
    UpdateManager       updateManager(dataRoot);
    QObject::connect(&inputManager, &InputManager::mpvKeyRequested,
                     &mpvController, &MpvController::sendKey);

#ifdef Q_OS_MAC
    auto applySleepPreventionSettings = [&appCore]() {
        configureMacSleepPrevention(preventSleepEnabledFromSettings(appCore),
                                    lowBatteryThresholdFromSettings(appCore));
    };
    applySleepPreventionSettings();
    QObject::connect(&appCore, &AppCore::appSettingChanged, &app,
                     [&applySleepPreventionSettings](const QString &key, const QString &) {
        if (key == QStringLiteral("prevent_sleep") ||
            key == QStringLiteral("battery_sleep_threshold")) {
            applySleepPreventionSettings();
        }
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [] {
        stopMacSleepPrevention();
    });
#endif

    // Each module backend is wired in one call: stored for action routing, exposed to QML
    // under its context-property name, and its optional signals/slots connected by
    // introspection. The module ID lives in exactly one place per module.
    QQmlContext *ctx = engine.rootContext();
    appCore.registerModule("com.240mp.local_files",  "localFilesBackend",  &localFiles,  ctx);
    appCore.registerModule("com.240mp.plex",         "plexBackend",        &plexBackend, ctx);
    appCore.registerModule("com.240mp.jellyfin",     "jellyfinBackend",    &jellyfinBackend, ctx);
    appCore.registerModule("com.240mp.karaoke",      "karaokeBackend",     &karaokeBackend, ctx);
    appCore.registerModule("com.240mp.tumblr_screensaver", "tumblrScreensaverBackend", &tumblrScreensaver, ctx);
    appCore.registerModule("com.240mp.nature", "natureBackend", &natureBackend, ctx);

    ctx->setContextProperty("appCore",       &appCore);
    ctx->setContextProperty("mpvController", &mpvController);
    ctx->setContextProperty("idleTracker",   &idleTracker);
    ctx->setContextProperty("inputManager",  &inputManager);
    ctx->setContextProperty("updateManager", &updateManager);
    ctx->setContextProperty("diagnosticsManager", &diagnosticsManager);
    ctx->setContextProperty("hasExternalMediaScreen", displaySelection.hasSeparateMediaScreen());
    ctx->setContextProperty("externalMediaScreenX", mediaGeometry.x());
    ctx->setContextProperty("externalMediaScreenY", mediaGeometry.y());
    ctx->setContextProperty("externalMediaScreenWidth", mediaGeometry.width());
    ctx->setContextProperty("externalMediaScreenHeight", mediaGeometry.height());
#ifdef Q_OS_MAC
    engine.rootContext()->setContextProperty("macScreenX",      QVariant(controllerGeometry.x()));
    engine.rootContext()->setContextProperty("macScreenY",      QVariant(controllerGeometry.y()));
    engine.rootContext()->setContextProperty("macScreenWidth",  QVariant(controllerGeometry.width()));
    engine.rootContext()->setContextProperty("macScreenHeight", QVariant(controllerGeometry.height()));
#endif

    engine.addImportPath(appRoot + "/views");

    engine.load(QUrl::fromLocalFile(appRoot + "/Main.qml"));
    if (engine.rootObjects().isEmpty()) {
        qCritical("[main] QML engine failed to load Main.qml");
        return 1;
    }
    if (QQuickWindow *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()))
        inputManager.setTargetWindow(window);
    QTimer::singleShot(0, &updateManager, &UpdateManager::checkForUpdatesOnLaunch);

    // The official yt-dlp onedir build starts quickly after its runtime pages
    // have been touched once. Prime it after the controller is interactive;
    // this is a local --version invocation and never contacts YouTube.
    auto *youtubeWarmup = new YouTubeJob(appRoot, &app);
    auto *youtubeWarmupTimer = new QElapsedTimer;
    QObject::connect(youtubeWarmup, &YouTubeJob::completed, &app,
                     [youtubeWarmup, youtubeWarmupTimer](YouTubeJob::Failure failure,
                                                        int, const QString &safeError) {
        const qint64 elapsedMs = youtubeWarmupTimer->isValid()
            ? youtubeWarmupTimer->elapsed() : -1;
        if (failure == YouTubeJob::Failure::None) {
            qInfo("[YouTube] helper warm-up completed in %lld ms", elapsedMs);
        } else {
            qWarning("[YouTube] helper warm-up failed in %lld ms: %s",
                     elapsedMs, qPrintable(safeError));
        }
        delete youtubeWarmupTimer;
        youtubeWarmup->deleteLater();
    });
    QTimer::singleShot(0, &app, [youtubeWarmup, youtubeWarmupTimer] {
        youtubeWarmupTimer->start();
        youtubeWarmup->start({QStringLiteral("--version")}, 10000, 4096, 4096);
    });

#ifdef Q_OS_MAC
    if (QWindow *win = qobject_cast<QWindow *>(engine.rootObjects().first())) {
        if (controllerScreen)
            win->setScreen(controllerScreen);
        win->setGeometry(controllerGeometry);
        win->winId(); // ensure native NSWindow is created
        forceWindowFullScreenOnScreen(reinterpret_cast<void *>(win->winId()),
                                      displaySelection.controllerIndex);
    }
#endif

    return app.exec();
}
