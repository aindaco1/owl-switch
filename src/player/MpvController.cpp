#include "MpvController.h"
#include "AppCore.h"
#include "tools/HelperResolver.h"
#include <QDir>
#include <QFile>
#include <QProcessEnvironment>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryFile>
#include <QUrl>
#include <QUrlQuery>
#ifdef Q_OS_MACOS
#include "macos_utils.h"
#endif

static QString redactedUrlForLog(const QString &value) {
    QUrl url(value);
    if (!url.isValid() || url.scheme().isEmpty())
        return value;

    QUrlQuery query(url);
    bool changed = false;
    const QStringList sensitiveKeys = {
        QStringLiteral("api_key"),
        QStringLiteral("access_token"),
        QStringLiteral("X-Plex-Token"),
        QStringLiteral("token")
    };
    for (const QString &key : sensitiveKeys) {
        if (query.hasQueryItem(key)) {
            query.removeAllQueryItems(key);
            query.addQueryItem(key, QStringLiteral("<redacted>"));
            changed = true;
        }
    }
    if (changed)
        url.setQuery(query);
    return url.toString();
}

static QString redactedArgForLog(const QString &arg) {
    if (arg.startsWith(QStringLiteral("--http-header-fields")) ||
        arg.contains(QStringLiteral("X-Plex-Token:"), Qt::CaseInsensitive) ||
        arg.contains(QStringLiteral("X-Emby-Token:"), Qt::CaseInsensitive) ||
        arg.contains(QStringLiteral("Authorization:"), Qt::CaseInsensitive)) {
        return QStringLiteral("--http-header-fields=<redacted>");
    }
    if (arg.startsWith(QStringLiteral("--include=")) &&
        arg.contains(QStringLiteral("http-headers"))) {
        return QStringLiteral("--include=<private-http-headers>");
    }
    return redactedUrlForLog(arg);
}

static QStringList redactedArgsForLog(const QStringList &args) {
    QStringList redacted;
    redacted.reserve(args.size());
    for (const QString &arg : args)
        redacted << redactedArgForLog(arg);
    return redacted;
}

static QString redactedTextForLog(QString text) {
    text.replace(QRegularExpression("(?i)(api_key|access_token|token|X-Plex-Token)=([^&\\s]+)"),
                 "\\1=<redacted>");
    text.replace(QRegularExpression("(?i)(X-Emby-Token|X-Plex-Token|Authorization):[^\\r\\n]*"),
                 "\\1:<redacted>");
    return text;
}

static QString boundedOverlayLine(const QString &value, const QString &fallback) {
    QString safe;
    safe.reserve(44);
    bool truncated = false;
    for (const QChar character : value.trimmed()) {
        if (character == QLatin1Char('{') || character == QLatin1Char('}') ||
            character == QLatin1Char('\\')) {
            continue;
        }
        if (character.isSpace()) {
            if (!safe.isEmpty() && safe.back() != QLatin1Char(' '))
                safe.append(QLatin1Char(' '));
        } else if (!character.isNull() && character.isPrint()) {
            safe.append(character);
        }
        if (safe.size() >= 44) {
            truncated = true;
            break;
        }
    }
    safe = safe.trimmed();
    if (safe.isEmpty())
        safe = fallback;
    if (truncated && safe.size() > 3)
        safe = safe.left(safe.size() - 3) + QStringLiteral("...");
    return safe;
}

static int fittedOverlayFontSize(const QString &value, int preferred, int minimum) {
    // VCR OSD Mono is close to 0.62 em per glyph. Keep even the longest bounded
    // line inside the card while allowing ordinary titles to render much larger.
    static constexpr int kUsableWidth = 680;
    const int fitted = value.isEmpty()
        ? preferred
        : (kUsableWidth * 100) / (value.size() * 62);
    return qBound(minimum, fitted, preferred);
}

#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <linux/vt.h>
#include <string>
// DRM master ioctls (also provided by xf86drm.h, but define as fallback).
#ifndef DRM_IOCTL_SET_MASTER
#define DRM_IOCTL_SET_MASTER   _IO('d', 0x1e)
#define DRM_IOCTL_DROP_MASTER  _IO('d', 0x1f)
#endif

// Write a fontconfig override so the mpv subprocess's libass can find custom
// fonts without needing them installed system-wide.
static QString writeFontconfigOverride(const QString &fontsDir) {
    const QString path = QDir::tempPath() + "/240-mp-jellyfin-fonts.conf";
    QFile f(path);
    if (!f.open(QFile::WriteOnly | QFile::Text))
        return {};
    f.write(QString(
        "<?xml version=\"1.0\"?>\n"
        "<!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">\n"
        "<fontconfig>\n"
        "  <dir>%1</dir>\n"
        "  <include ignore_missing=\"yes\">/etc/fonts/fonts.conf</include>\n"
        "</fontconfig>\n"
    ).arg(fontsDir).toUtf8());
    return path;
}
#endif

MpvController::MpvController(const QString &appRoot, AppCore *appCore, QObject *parent)
    : QObject(parent)
    , m_appCore(appCore)
    , m_appRoot(appRoot)
    , m_socketPath(QDir::tempPath() + "/240-mp-jellyfin-mpv.sock")
    , m_inputConfPath(QDir::tempPath() + "/240-mp-jellyfin-input.conf")
    , m_logFilePath(QDir::tempPath() + "/240-mp-jellyfin-mpv.log")
{
    writeInputConfig();

    m_ipc = new QLocalSocket(this);
    connect(m_ipc, &QLocalSocket::connected, this, [this] {
        m_connectTimer->stop();
        m_lastIpcEventMs = QDateTime::currentMSecsSinceEpoch();
        m_watchdogTimer->start();
        sendCommand({"observe_property", 1, "time-pos"});
        sendCommand({"observe_property", 2, "duration"});
        sendCommand({"observe_property", 3, "playlist-pos"});
        sendCommand({"observe_property", 4, "pause"});
        sendCommand({"observe_property", 5, "path"});
        const QList<QJsonArray> pendingCommands = m_pendingPlaylistCommands;
        m_pendingPlaylistCommands.clear();
        for (const QJsonArray &command : pendingCommands)
            sendCommand(command);
    });
    connect(m_ipc, &QLocalSocket::readyRead, this, &MpvController::onIpcReadyRead);

    m_connectTimer = new QTimer(this);
    m_connectTimer->setInterval(100);
    connect(m_connectTimer, &QTimer::timeout, this, &MpvController::tryConnectIpc);

    // Watchdog: fires every 10 s; logs a warning if no IPC time-pos event has
    // arrived for 30 s while connected — strong indicator of a playback freeze.
    m_watchdogTimer = new QTimer(this);
    m_watchdogTimer->setInterval(10000);
    connect(m_watchdogTimer, &QTimer::timeout, this, [this] {
        if (m_ipc->state() != QLocalSocket::ConnectedState || m_paused) return;
        qint64 silenceMs = QDateTime::currentMSecsSinceEpoch() - m_lastIpcEventMs;
        if (silenceMs > 30000) {
            qWarning("[MpvController] WATCHDOG: no IPC time-pos event for %lld s — possible freeze",
                     silenceMs / 1000);
        }
    });

    m_trackOverlayTimer = new QTimer(this);
    m_trackOverlayTimer->setSingleShot(true);
    connect(m_trackOverlayTimer, &QTimer::timeout,
            this, &MpvController::clearTrackOverlay);
}

MpvController::~MpvController() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        m_process->waitForFinished(2000);
    }
    removeHttpHeaderConfig();
}

QString MpvController::writeHttpHeaderConfig(const QStringList &httpHeaderFields) {
    QStringList cleaned;
    for (const QString &header : httpHeaderFields) {
        const QString trimmed = header.trimmed();
        if (!trimmed.isEmpty() && !trimmed.contains('\n') && !trimmed.contains('\r'))
            cleaned << trimmed;
    }
    if (cleaned.isEmpty())
        return {};

    QTemporaryFile file(QDir::tempPath() + "/240-mp-jellyfin-http-headers.XXXXXX.conf");
    file.setAutoRemove(false);
    if (!file.open()) {
        qWarning("[MpvController] Could not create private mpv HTTP header config");
        return {};
    }

    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    for (const QString &header : cleaned) {
        file.write("http-header-fields-append=");
        file.write(header.toUtf8());
        file.write("\n");
    }
    const QString path = file.fileName();
    file.close();
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return path;
}

void MpvController::removeHttpHeaderConfig() {
    if (!m_httpHeaderConfPath.isEmpty()) {
        QFile::remove(m_httpHeaderConfPath);
        m_httpHeaderConfPath.clear();
    }
}

void MpvController::writeInputConfig(const QStringList &inputBindings) {
    QSet<QString> overrideKeys;
    for (const QString &binding : inputBindings) {
        const QString trimmed = binding.trimmed();
        const int split = trimmed.indexOf(' ');
        if (split > 0)
            overrideKeys.insert(trimmed.left(split));
    }

    QFile f(m_inputConfPath);
    if (!f.open(QFile::WriteOnly | QFile::Text))
        return;

    const QStringList defaults = {
        QStringLiteral("ESC quit"),
        QStringLiteral("BS quit"),
        QStringLiteral("ENTER cycle pause")
    };
    for (const QString &binding : defaults) {
        const QString key = binding.section(' ', 0, 0);
        if (!overrideKeys.contains(key)) {
            f.write(binding.toUtf8());
            f.write("\n");
        }
    }

    for (const QString &binding : inputBindings) {
        const QString trimmed = binding.trimmed();
        if (!trimmed.isEmpty() && !trimmed.contains('\n') && !trimmed.contains('\r')) {
            f.write(trimmed.toUtf8());
            f.write("\n");
        }
    }
}

void MpvController::loadAndPlay(const QString &url, float startSeconds,
                                 int audioTrack, int subTrack,
                                 const QStringList &subFiles, bool loop,
                                 int playlistStart, float transcodeOffsetSec,
                                 const QString &plexToken, bool muteAudio,
                                 const QString &oscMode, bool shuffle,
                                 const QStringList &httpHeaderFields,
                                 const QString &videoFilters,
                                 const QStringList &inputBindings) {
    loadAndPlayWithOptions(url, QVariantMap{
        {"startSeconds", startSeconds},
        {"audioTrack", audioTrack},
        // Legacy callers used every negative value to mean subtitles off.
        {"subtitleTrack", subTrack < 0 ? -2 : subTrack},
        {"subtitleFiles", subFiles},
        {"loop", loop},
        {"playlistStart", playlistStart},
        {"transcodeOffsetSeconds", transcodeOffsetSec},
        {"plexToken", plexToken},
        {"muteAudio", muteAudio},
        {"oscMode", oscMode},
        {"shuffle", shuffle},
        {"httpHeaderFields", httpHeaderFields},
        {"videoFilters", videoFilters},
        {"inputBindings", inputBindings}
    });
}

void MpvController::loadAndPlayWithOptions(const QString &url, const QVariantMap &options) {
    const float startSeconds = options.value("startSeconds").toFloat();
    const int audioTrack = options.value("audioTrack").toInt();
    const int subTrack = options.contains("subtitleTrack")
        ? options.value("subtitleTrack").toInt() : -1;
    const QStringList subFiles = options.value("subtitleFiles").toStringList();
    const QStringList subLangs = options.value("subtitleLanguages").toStringList();
    const bool loop = options.value("loop").toBool();
    const QString repeatMode = options.value("repeatMode").toString().trimmed().toLower();
    const int playlistStart = options.contains("playlistStart")
        ? options.value("playlistStart").toInt() : -1;
    const float transcodeOffsetSec = options.value("transcodeOffsetSeconds").toFloat();
    const QString plexToken = options.value("plexToken").toString();
    const bool muteAudio = options.value("muteAudio").toBool();
    const QString oscMode = options.value("oscMode").toString();
    const bool shuffle = options.value("shuffle").toBool();
    const QStringList httpHeaderFields = options.value("httpHeaderFields").toStringList();
    const QString videoFilters = options.value("videoFilters").toString();
    const QStringList inputBindings = options.value("inputBindings").toStringList();
    const QStringList extraArguments = options.value("extraArguments").toStringList();
    const int imageDurationSeconds = qMax(0, options.value("imageDurationSeconds").toInt());
    m_muteAudio = muteAudio;
    clearTrackOverlay();
    if (m_process) {
        m_process->disconnect();
        if (m_process->state() != QProcess::NotRunning) {
            m_process->terminate();
            m_process->waitForFinished(1000);
        }
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_watchdogTimer->stop();
    m_ipc->abort();
    QFile::remove(m_socketPath);
    removeHttpHeaderConfig();
    m_position    = 0;
    m_duration    = 0;
    m_playlistPos = -1;
    if (!m_currentPath.isEmpty()) {
        m_currentPath.clear();
        emit currentPathChanged({});
    }
    m_lastEndFileReason.clear();
    m_lastEndFileError.clear();
    m_pendingStartClear = false;
    m_paused = false;
    m_pendingPlaylistCommands.clear();
    writeInputConfig(inputBindings);

    const QString bin = HelperResolver::mpv(m_appRoot);
    if (bin.isEmpty()) {
        qWarning("[MpvController] mpv not found in app bundle or PATH");
        QTimer::singleShot(0, this, [this]() {
            emit playbackEnded(0, 0, QStringLiteral("failed"));
            emit playbackFailed();
        });
        return;
    }

    const QString oscScript = m_appRoot + QStringLiteral("/scripts/mpv-osc.lua");
    const bool hasOscScript = QFile::exists(oscScript);
    const QString mediaKeysScript = m_appRoot + "/scripts/media-keys.lua";

    // Stamp the log file so each session is identifiable when tailing over SSH.
    {
        QFile lf(m_logFilePath);
        if (lf.open(QFile::Append | QFile::Text)) {
            lf.write(QString("\n=== 240-mp-jellyfin session start %1 ===\n    url: %2\n\n")
                         .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
                         .arg(redactedUrlForLog(url))
                         .toUtf8());
        }
    }

    QStringList args;
    args << url
         << QString("--input-ipc-server=%1").arg(m_socketPath)
         << QString("--log-file=%1").arg(m_logFilePath)
         << (hasOscScript ? "--osc=no" : "--osc=yes")
         << "--osd-level=0";

    if (hasOscScript)
        args << QString("--script=%1").arg(oscScript);
    if (QFile::exists(mediaKeysScript))
        args << QString("--script=%1").arg(mediaKeysScript);
    int screensaverTimeout = 0;
    if (m_appCore) {
        screensaverTimeout = m_appCore->get_setting({}, "screensaver_timeout").toString().toInt();
        const QString screensaverScript = m_appRoot + "/scripts/screensaver.lua";
        if (screensaverTimeout > 0 && QFile::exists(screensaverScript))
            args << QString("--script=%1").arg(screensaverScript);
    }

    if (playlistStart >= 0)
        args << QString("--playlist-start=%1").arg(playlistStart);
    if (startSeconds > 0.5f) {
        args << QString("--start=%1").arg(double(startSeconds), 0, 'f', 3);
        m_pendingStartClear = true;
    }
    if (audioTrack > 0)
        args << QString("--aid=%1").arg(audioTrack);
    for (const QString &sf : subFiles)
        args << QString("--sub-file=%1").arg(sf);
    if (subTrack > 0)
        args << QString("--sid=%1").arg(subTrack);
    else if (subTrack < -1)
        args << QStringLiteral("--sid=no");
    else if (subTrack == -1)
        args << QStringLiteral("--subs-with-matching-audio=forced")
             << QStringLiteral("--subs-fallback-forced=always");
    else if (subTrack == 0) {
        args << QStringLiteral("--subs-with-matching-audio=yes")
             << QStringLiteral("--subs-fallback=yes");
        if (subFiles.isEmpty())
            args << QStringLiteral("--sid=auto");
    }
    // else: external sub(s) loaded, subTrack==0 → mpv auto-selects first loaded sub
    if (!subLangs.isEmpty())
        args << QString("--slang=%1").arg(subLangs.join(QLatin1Char(',')));

    if (transcodeOffsetSec > 0.5f)
        args << QString("--script-opts-append=transcode-offset=%1").arg(double(transcodeOffsetSec), 0, 'f', 3);
    if (oscMode == QStringLiteral("retro"))
        args << QStringLiteral("--script-opts-append=retro-tv=1");
    if (oscMode == QStringLiteral("karaoke"))
        args << QStringLiteral("--script-opts-append=karaoke=1");
    if (screensaverTimeout > 0)
        args << QString("--script-opts-append=screensaver-timeout=%1").arg(screensaverTimeout);

    if (repeatMode == QLatin1String("one"))
        args << QStringLiteral("--loop-file=inf");
    else if (loop || repeatMode == QLatin1String("queue"))
        args << QStringLiteral("--loop-playlist=inf");
    if (shuffle)
        args << QStringLiteral("--shuffle");
    if (muteAudio)
        args << QStringLiteral("--no-audio");
    if (!videoFilters.trimmed().isEmpty())
        args << QStringLiteral("--vf=%1").arg(videoFilters.trimmed());
    if (m_appCore) {
        const QString crop = m_appCore->get_setting({}, "auto_crop").toString();
        if (crop.compare("On", Qt::CaseInsensitive) == 0)
            args << QStringLiteral("--panscan=1");
        const QString levels = m_appCore->get_setting({}, "video_output_levels").toString();
        if (levels == QLatin1String("Limited"))
            args << QStringLiteral("--video-output-levels=limited");
        else if (levels == QLatin1String("Full"))
            args << QStringLiteral("--video-output-levels=full");
    }
    if (imageDurationSeconds > 0)
        args << QString("--image-display-duration=%1").arg(imageDurationSeconds);
    args << extraArguments;

    const bool usesYoutube = oscMode == QStringLiteral("karaoke")
                          || oscMode == QStringLiteral("retro");
    if (usesYoutube)
        args << HelperResolver::youtubeMpvArguments(m_appRoot);

    QStringList playbackHeaders = httpHeaderFields;
    if (!plexToken.isEmpty()) {
        playbackHeaders << QString("X-Plex-Token:%1").arg(plexToken);
        // Plex URLs are direct file paths — yt-dlp hook is not needed and causes
        // spurious 401 errors when mpv encounters a non-2xx response from PMS.
    }
    m_httpHeaderConfPath = writeHttpHeaderConfig(playbackHeaders);
    if (!m_httpHeaderConfPath.isEmpty()) {
        args << QString("--include=%1").arg(m_httpHeaderConfPath);
        args << QStringLiteral("--ytdl=no");
    }

    // plex.direct certs are Let's Encrypt-signed but ffmpeg's bundled CA bundle
    // may not trust the full chain (same reason Qt needs ignoreSslErrors for these
    // hosts). Disable TLS verification only for plex.direct playback URLs.
    if (QUrl(url).host().endsWith(QStringLiteral(".plex.direct")))
        args << QStringLiteral("--tls-verify=no");

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MpvController::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        qWarning("[MpvController] mpv failed to start: %s",
                 qPrintable(redactedTextForLog(m_process->errorString())));
        m_connectTimer->stop();
        m_watchdogTimer->stop();
        QFile::remove(m_socketPath);
        removeHttpHeaderConfig();
        emit playbackFailed();
    });
    connect(m_process, &QProcess::readyRead, this, [this]() {
        const QByteArray out = m_process->readAll();
        if (!out.isEmpty())
            qWarning("[mpv] %s", qPrintable(redactedTextForLog(QString::fromUtf8(out).trimmed())));
    });

    m_headlessMode = detectHeadlessMode();
    if (m_headlessMode) {
        {
            QProcessEnvironment env = HelperResolver::processEnvironment(m_appRoot);
#ifdef Q_OS_LINUX
            const QString fcConf = writeFontconfigOverride(m_appRoot + "/assets/fonts");
            if (!fcConf.isEmpty())
                env.insert("FONTCONFIG_FILE", fcConf);
#endif
            m_process->setProcessEnvironment(env);
        }

        if (m_previousVt > 0) {
            // loadAndPlay called while already in headless mode (e.g. rapid
            // double call from Plex Player). m_previousVt already holds Qt's
            // real VT — do NOT overwrite it with the current free VT. The old
            // mpv was terminated above; just launch the replacement directly.
            args << QString("--input-conf=%1").arg(m_inputConfPath)
                 << "--video-sync=audio"
                 << "--vo=drm" << "--hwdec=auto-safe" << "--no-input-terminal";
            m_process->start(bin, args);
            m_connectTimer->start();
            return;
        }

        // First entry into headless mode.
        //
        // On kernels 5.8+, drmSetMaster() returns EACCES for non-root if any
        // other process holds DRM master — even after a VT switch, because Qt
        // EGLFS runs in VT_AUTO mode and never calls drmDropMaster() itself.
        //
        // Fix: switch to a free VT first (suspends Qt's render thread), then
        // drop Qt's DRM master so mpv can acquire it cleanly.

        m_previousVt = getActiveVt();
        m_qtDrmFd    = -1;

#ifdef Q_OS_LINUX
        // Switch VT first — suspends Qt's render thread via the kernel's VT
        // switch signal before DRM master is dropped, eliminating the race
        // that causes "Failed to commit atomic request" log noise.
        switchToVt(findFreeVt());

        m_qtDrmFd = findQtDrmFd();
        if (m_qtDrmFd < 0) {
            qWarning("[MpvController] Could not find Qt DRM fd");
        } else {
            qDebug("[MpvController] DRM master dropped (fd %d)", m_qtDrmFd);
            // Save the current CRTC state so we can restore it exactly after
            // mpv exits. mpv's atomic cleanup disables the CRTC (CRTC_ACTIVE=0);
            // without this restore, Qt EGLFS gets EINVAL on its next page flip.
            saveDrmCrtcState(m_qtDrmFd);
        }
#endif

        args << QString("--input-conf=%1").arg(m_inputConfPath)
             << "--video-sync=audio"
             << "--vo=drm" << "--hwdec=auto-safe" << "--no-input-terminal";
        m_process->start(bin, args);
        m_connectTimer->start();
    } else {
        // Desktop: X11 or Wayland compositor present.
        // Remove WAYLAND_DISPLAY so mpv uses X11/Xwayland — the Wayland VO
        // stalls waiting for wl_surface frame-done callbacks from labwc.
        // --no-native-fs avoids macOS Space-transition delays that can
        // prevent early OSD renders from appearing.
        QProcessEnvironment env = HelperResolver::processEnvironment(m_appRoot);
        env.remove("WAYLAND_DISPLAY");
#ifdef Q_OS_LINUX
        const QString fcConf = writeFontconfigOverride(m_appRoot + "/assets/fonts");
        if (!fcConf.isEmpty())
            env.insert("FONTCONFIG_FILE", fcConf);
#endif
        m_process->setProcessEnvironment(env);
        args << QString("--input-conf=%1").arg(m_inputConfPath)
             << "--video-sync=audio"
             << "--fullscreen" << "--no-native-fs"
             << "--hwdec=videotoolbox"
             << QString("--osd-fonts-dir=%1").arg(m_appRoot + "/assets/fonts");
#ifdef Q_OS_MACOS
        if (m_playbackScreenIndex >= 0) {
            args << QString("--screen=%1").arg(m_playbackScreenIndex)
                 << QString("--fs-screen=%1").arg(m_playbackScreenIndex);
        }
#endif
        qDebug("[MpvController] desktop launch: mpv %s", qPrintable(redactedArgsForLog(args).join(" ")));
        m_process->start(bin, args);
        m_connectTimer->start();
    }
}

void MpvController::stop() {
    if (m_ipc->state() == QLocalSocket::ConnectedState) {
        sendCommand({"quit"});
    } else if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
    }
}

void MpvController::seekTo(int positionMs) {
    sendCommand({"seek", positionMs / 1000.0, "absolute+exact"});
}

void MpvController::sendKey(const QString &key) {
    sendCommand({"keypress", key});
}

void MpvController::setVideoFilters(const QString &filters) {
    sendCommand({"vf", "set", filters.trimmed()});
}

void MpvController::showText(const QString &text, int durationMs) {
    sendCommand({"show-text", text, durationMs});
}

void MpvController::showTrackOverlay(const QVariantMap &track, const QString &heading,
                                     int durationMs) {
    QString rawArtist = track.value(QStringLiteral("artist")).toString().trimmed();
    QString rawSong = track.value(QStringLiteral("song")).toString().trimmed();
    if (rawSong.isEmpty()) {
        const QString displayTitle = track.value(
            QStringLiteral("displayTitle")).toString().trimmed();
        const int separator = displayTitle.indexOf(QStringLiteral(" - "));
        if (separator > 0) {
            rawArtist = displayTitle.left(separator).trimmed();
            rawSong = displayTitle.mid(separator + 3).trimmed();
        } else {
            rawSong = displayTitle;
        }
    }

    const QString fallbackArtist = boundedOverlayLine(
        track.value(QStringLiteral("fallbackArtist")).toString(),
        QStringLiteral("SOUNDTRACK"));
    const QString artist = boundedOverlayLine(rawArtist, fallbackArtist);
    const QString song = boundedOverlayLine(rawSong, QStringLiteral("UNKNOWN SONG"));
    if (song == QLatin1String("UNKNOWN SONG"))
        return;

    const QString safeHeading = boundedOverlayLine(heading, QString{});
    const int artistSize = fittedOverlayFontSize(artist, 42, 26);
    const int songSize = fittedOverlayFontSize(song, 34, 24);

    static constexpr int kOverlayId = 240;
    const QString background = QStringLiteral(
        "{\\an7\\pos(1096,806)\\p1\\bord0\\shad0\\1c&H000000&\\1a&H38&}"
        "m 0 0 l 760 0 760 210 0 210");
    const QString headingLine = safeHeading.isEmpty()
        ? QString{}
        : QStringLiteral("{\\fs22\\b1\\1c&HBBBBBB&}%1\\N").arg(safeHeading);
    const QString text = QStringLiteral(
        "{\\an5\\pos(1476,911)\\fnVCR OSD Mono\\bord2\\shad0\\3c&H000000&}"
        "%1{\\fs%2\\b1\\1c&HFFFFFF&}%3"
        "\\N{\\fs%4\\b0\\1c&HE6E6E6&}%5")
        .arg(headingLine)
        .arg(artistSize)
        .arg(artist)
        .arg(songSize)
        .arg(song);
    sendNamedCommand(QJsonObject{
        {QStringLiteral("name"), QStringLiteral("osd-overlay")},
        {QStringLiteral("id"), kOverlayId},
        {QStringLiteral("format"), QStringLiteral("ass-events")},
        {QStringLiteral("data"), background + QLatin1Char('\n') + text},
        {QStringLiteral("res_x"), 1920},
        {QStringLiteral("res_y"), 1080},
        {QStringLiteral("z"), 100}
    });
    if (durationMs > 0)
        m_trackOverlayTimer->start(qBound(100, durationMs, 60000));
    else
        m_trackOverlayTimer->stop();
}

void MpvController::clearTrackOverlay() {
    if (m_trackOverlayTimer)
        m_trackOverlayTimer->stop();
    sendNamedCommand(QJsonObject{
        {QStringLiteral("name"), QStringLiteral("osd-overlay")},
        {QStringLiteral("id"), 240},
        {QStringLiteral("format"), QStringLiteral("none")},
        {QStringLiteral("data"), QString{}},
        {QStringLiteral("res_x"), 1920},
        {QStringLiteral("res_y"), 1080},
        {QStringLiteral("z"), 100}
    });
}

void MpvController::showOsdSkipPrompt() {
    sendCommand({"script-message", "skip-overlay-state", "1"});
    sendCommand({"keypress", "DOWN"});
}

void MpvController::clearOsdPrompt() {
    sendCommand({"script-message", "skip-overlay-state", "0"});
}

void MpvController::selectPlaybackTracks(int audioTrack, int subtitleTrack,
                                         const QStringList &subtitleFiles) {
    // A playlist item load must not undo the launch-time soundtrack mute.
    // Local reapplies each file's saved track choices after file-loaded, so
    // selecting that saved aid here used to re-enable the video's audio even
    // when the process had started with --no-audio.
    const QJsonValue selectedAudio = m_muteAudio
        ? QJsonValue(QStringLiteral("no"))
        : (audioTrack > 0 ? QJsonValue(audioTrack)
                          : QJsonValue(QStringLiteral("auto")));
    sendCommand({"set_property", "aid", selectedAudio});
    for (int index = 0; index < subtitleFiles.size(); ++index) {
        const QString path = subtitleFiles.at(index).trimmed();
        if (!path.isEmpty())
            sendCommand({"sub-add", path, index == 0 ? "select" : "auto"});
    }
    if (!subtitleFiles.isEmpty())
        return;
    if (subtitleTrack > 0)
        sendCommand({"set_property", "sid", subtitleTrack});
    else if (subtitleTrack < -1)
        sendCommand({"set_property", "sid", "no"});
    else
        sendCommand({"set_property", "sid", "auto"});
}

void MpvController::appendPlaylistItem(const QString &url) {
    if (!url.trimmed().isEmpty())
        sendPlaylistCommand({"loadfile", url, "append"});
}

void MpvController::playPlaylistItem(int index) {
    if (index >= 0)
        sendPlaylistCommand({"playlist-play-index", index});
}

void MpvController::replacePlaylistItem(int index, const QString &url) {
    if (index < 0 || url.trimmed().isEmpty())
        return;

    // Prefetched media only replaces an upcoming item. Insert first so a
    // failed insert leaves the original remote entry intact, then remove the
    // displaced entry now one slot later.
    sendPlaylistCommand({"loadfile", url, "insert-at", index});
    sendPlaylistCommand({"playlist-remove", index + 1});
}

void MpvController::removePlaylistItem(int index) {
    if (index >= 0)
        sendPlaylistCommand({"playlist-remove", index});
}

void MpvController::movePlaylistItem(int fromIndex, int toIndex) {
    if (fromIndex < 0 || toIndex < 0 || fromIndex == toIndex)
        return;

    // mpv's playlist-move target is the entry to insert before, so moving an
    // item downward by one is most reliably expressed by moving its next
    // neighbor upward. Repeating adjacent moves gives exact QList::move
    // semantics without relying on an out-of-range append index.
    if (fromIndex < toIndex) {
        for (int index = fromIndex; index < toIndex; ++index)
            sendPlaylistCommand({"playlist-move", index + 1, index});
    } else {
        for (int index = fromIndex; index > toIndex; --index)
            sendPlaylistCommand({"playlist-move", index, index - 1});
    }
}

void MpvController::clearPlaylistExceptCurrent() {
    sendPlaylistCommand({"playlist-clear"});
}

void MpvController::tryConnectIpc() {
    if (m_ipc->state() == QLocalSocket::ConnectedState ||
        m_ipc->state() == QLocalSocket::ConnectingState)
        return;
    m_ipc->connectToServer(m_socketPath);
}

void MpvController::onIpcReadyRead() {
    while (m_ipc->canReadLine()) {
        const QByteArray line = m_ipc->readLine().trimmed();
        const QJsonObject obj = QJsonDocument::fromJson(line).object();
        if (obj.isEmpty())
            continue;

        m_lastIpcEventMs = QDateTime::currentMSecsSinceEpoch();

        const QString event = obj["event"].toString();
        if (event == QStringLiteral("client-message")) {
            const QJsonArray args = obj["args"].toArray();
            if (!args.isEmpty() && args.at(0).toString() == QStringLiteral("skip-segment"))
                emit skipRequested();
            else if (args.size() >= 2 && args.at(0).toString() == QStringLiteral("240mp-key"))
                emit mpvKeyPressed(args.at(1).toString());
            else if (!args.isEmpty() && args.at(0).toString() == QStringLiteral("cycle-sub"))
                emit subtitleCycleRequested();
            else if (!args.isEmpty() && args.at(0).toString() == QStringLiteral("cycle-audio"))
                emit audioCycleRequested();
            continue;
        }

        if (event == QStringLiteral("end-file")) {
            m_lastEndFileReason = obj.value("reason").toString();
            m_lastEndFileError = obj.value("file_error").toString();
            emit playbackItemEnded(m_playlistPos,
                                   m_lastEndFileReason,
                                   m_lastEndFileError);
            continue;
        }

        if (event == QStringLiteral("playback-restart")) {
            if (m_pendingStartClear) {
                m_pendingStartClear = false;
                sendCommand({"set_property", "start", "none"});
            }
            continue;
        }

        if (event == QStringLiteral("file-loaded")) {
            emit playbackItemLoaded(m_playlistPos);
            continue;
        }

        if (event != QStringLiteral("property-change"))
            continue;

        const QString     name = obj["name"].toString();
        const QJsonValue  data = obj["data"];
        if (data.isNull() || data.isUndefined()) continue; // property unavailable during shutdown
        if (name == QStringLiteral("pause")) {
            m_paused = data.toBool();
            continue;
        }
        const double val = data.toDouble();
        if (name == "time-pos") {
            m_position = int(val * 1000.0);
            emit positionChanged(m_position);
        } else if (name == "duration") {
            m_duration = int(val * 1000.0);
            emit durationChanged(m_duration);
        } else if (name == "playlist-pos") {
            m_playlistPos = int(val);
            emit playlistPosChanged(m_playlistPos);
        } else if (name == "path") {
            const QString path = data.toString();
            if (path != m_currentPath) {
                m_currentPath = path;
                emit currentPathChanged(m_currentPath);
            }
        }
    }
}

void MpvController::onProcessFinished() {
    int exitCode = m_process ? m_process->exitCode() : -1;
    if (m_process) {
        const QByteArray remaining = m_process->readAll();
        if (!remaining.isEmpty())
            qWarning("[mpv] %s", qPrintable(redactedTextForLog(QString::fromUtf8(remaining).trimmed())));
    }
    if (exitCode != 0)
        qWarning("[MpvController] mpv exited with code %d", exitCode);
    m_connectTimer->stop();
    m_watchdogTimer->stop();
    m_trackOverlayTimer->stop();
    m_ipc->abort();
    QFile::remove(m_socketPath);
    removeHttpHeaderConfig();
    const int pos = m_position;
    const int dur = m_duration;
    m_position = 0;
    m_duration = 0;
    if (!m_currentPath.isEmpty()) {
        m_currentPath.clear();
        emit currentPathChanged({});
    }

    QString reason;
    if (exitCode != 0 || m_lastEndFileReason == QStringLiteral("error"))
        reason = QStringLiteral("failed");
    else if (m_lastEndFileReason == QStringLiteral("eof"))
        reason = QStringLiteral("eof");
    else
        reason = QStringLiteral("stopped");

    if (m_headlessMode) {
        // Defer DRM restore and VT switch by 200 ms. mpv's last KMS atomic
        // commit may still be pending in the vc4 driver at the moment the
        // process exits. If EGLFS tries to commit before that pending flip
        // is signaled, it gets EBUSY repeatedly, drops its DRM pipeline, and
        // the kernel falls back to showing the text console on Qt's VT.
        // 200 ms is more than three VSync periods at 60 Hz — enough to clear
        // any in-flight commit without a perceptible delay for the user.
        QTimer::singleShot(200, this, [this, pos, dur, reason]() {
            doHeadlessRestore(pos, dur);
            emit playbackEnded(pos, dur, reason);
            if (reason == QStringLiteral("failed")) emit playbackFailed();
            else emit playbackFinished(pos, dur);
        });
    } else {
        emit playbackEnded(pos, dur, reason);
        if (reason == QStringLiteral("failed")) emit playbackFailed();
        else emit playbackFinished(pos, dur);
    }
}

void MpvController::doHeadlessRestore(int pos, int dur) {
#ifdef Q_OS_LINUX
    if (m_qtDrmFd >= 0) {
        if (::ioctl(m_qtDrmFd, DRM_IOCTL_SET_MASTER, 0) < 0) {
            qWarning("[MpvController] drmSetMaster failed: %s", strerror(errno));
        } else {
            qDebug("[MpvController] DRM master restored (fd %d)", m_qtDrmFd);
            // Restore CRTC to its pre-mpv state using legacy drmModeSetCrtc.
            // This re-enables the CRTC with the original mode and Qt's last
            // framebuffer, so EGLFS's first atomic page flip succeeds instead
            // of getting EINVAL from a disabled CRTC.
            restoreDrmCrtcState(m_qtDrmFd);
        }
        m_qtDrmFd = -1;
    }
#endif
    if (m_previousVt > 0) {
        qDebug("[MpvController] Switching back to VT %d", m_previousVt);
        int prevVt = m_previousVt;
        m_previousVt = -1;
        switchToVt(prevVt);
    }
    m_headlessMode = false;
}

void MpvController::sendCommand(const QJsonArray &args) {
    if (m_ipc->state() != QLocalSocket::ConnectedState) return;
    QJsonObject cmd;
    cmd["command"] = args;
    m_ipc->write(QJsonDocument(cmd).toJson(QJsonDocument::Compact) + "\n");
}

void MpvController::sendNamedCommand(const QJsonObject &command) {
    if (m_ipc->state() != QLocalSocket::ConnectedState)
        return;
    QJsonObject request;
    request[QStringLiteral("command")] = command;
    m_ipc->write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
}

void MpvController::sendPlaylistCommand(const QJsonArray &args) {
    if (m_ipc->state() == QLocalSocket::ConnectedState) {
        sendCommand(args);
    } else {
        m_pendingPlaylistCommands.append(args);
    }
}

bool MpvController::detectHeadlessMode() const {
#ifdef Q_OS_LINUX
    return qgetenv("DISPLAY").isEmpty() && qgetenv("WAYLAND_DISPLAY").isEmpty();
#else
    return false;
#endif
}

int MpvController::getActiveVt() const {
#ifdef Q_OS_LINUX
    QFile f("/sys/class/tty/tty0/active");
    if (!f.open(QIODevice::ReadOnly)) return -1;
    const QString name = QString::fromLatin1(f.readAll()).trimmed();
    bool ok;
    int n = name.mid(3).toInt(&ok);
    return ok ? n : -1;
#else
    return -1;
#endif
}

int MpvController::findFreeVt() const {
#ifdef Q_OS_LINUX
    int fd = ::open("/dev/tty0", O_WRONLY);
    if (fd < 0) return 7;
    int n = -1;
    ::ioctl(fd, VT_OPENQRY, &n);
    ::close(fd);
    return (n > 0) ? n : 7;
#else
    return -1;
#endif
}

void MpvController::switchToVt(int vt) {
#ifdef Q_OS_LINUX
    int fd = ::open("/dev/tty0", O_WRONLY);
    if (fd < 0) {
        qWarning("[MpvController] switchToVt %d: open /dev/tty0 failed: %s", vt, strerror(errno));
        return;
    }
    if (::ioctl(fd, VT_ACTIVATE, vt) < 0)
        qWarning("[MpvController] VT_ACTIVATE %d failed: %s", vt, strerror(errno));
    if (::ioctl(fd, VT_WAITACTIVE, vt) < 0)
        qWarning("[MpvController] VT_WAITACTIVE %d failed: %s", vt, strerror(errno));
    ::close(fd);
#else
    Q_UNUSED(vt)
#endif
}

int MpvController::findQtDrmFd() const {
#ifdef Q_OS_LINUX
    // Scan the process's open file descriptors for Qt's DRM primary card
    // device. DRM primary nodes have major=226, minor 0-63 (card0, card1…).
    // We try DRM_IOCTL_DROP_MASTER on each candidate — it succeeds only on
    // the fd that currently holds DRM master, which tells us it's Qt's fd.
    QDir fdDir("/proc/self/fd");
    const QStringList entries = fdDir.entryList(QDir::Files | QDir::System);
    for (const QString &entry : entries) {
        bool ok;
        int fd = entry.toInt(&ok);
        if (!ok) continue;
        struct stat st;
        if (::fstat(fd, &st) < 0) continue;
        if (!S_ISCHR(st.st_mode)) continue;
        if (major(st.st_rdev) != 226) continue;   // not a DRM device
        if (minor(st.st_rdev) >= 64) continue;    // render node, not primary card
        // Found a DRM primary fd — try to drop master; if it works, this is it.
        if (::ioctl(fd, DRM_IOCTL_DROP_MASTER, 0) == 0)
            return fd;
    }
    return -1;
#else
    return -1;
#endif
}

#ifdef Q_OS_LINUX
void MpvController::saveDrmCrtcState(int fd) {
    m_savedDrm = {};

    drmModeResPtr res = drmModeGetResources(fd);
    if (!res) {
        qWarning("[MpvController] saveDrmCrtcState: drmModeGetResources failed");
        return;
    }

    for (int i = 0; i < res->count_crtcs && !m_savedDrm.valid; ++i) {
        drmModeCrtcPtr crtc = drmModeGetCrtc(fd, res->crtcs[i]);
        if (!crtc) continue;

        if (crtc->mode_valid) {
            m_savedDrm.crtcId = crtc->crtc_id;
            m_savedDrm.fbId   = crtc->buffer_id;
            m_savedDrm.x      = crtc->x;
            m_savedDrm.y      = crtc->y;
            m_savedDrm.mode   = crtc->mode;

            // Find the connector whose encoder is driving this CRTC
            for (int j = 0; j < res->count_connectors; ++j) {
                drmModeConnectorPtr conn = drmModeGetConnector(fd, res->connectors[j]);
                if (!conn) continue;
                if (conn->encoder_id) {
                    drmModeEncoderPtr enc = drmModeGetEncoder(fd, conn->encoder_id);
                    if (enc) {
                        if (enc->crtc_id == m_savedDrm.crtcId) {
                            m_savedDrm.connectorId = conn->connector_id;
                            m_savedDrm.valid = true;
                        }
                        drmModeFreeEncoder(enc);
                    }
                }
                drmModeFreeConnector(conn);
                if (m_savedDrm.valid) break;
            }
        }
        drmModeFreeCrtc(crtc);
    }
    drmModeFreeResources(res);

    if (m_savedDrm.valid)
        qDebug("[MpvController] Saved CRTC %u connector %u mode %dx%d@%d",
               m_savedDrm.crtcId, m_savedDrm.connectorId,
               m_savedDrm.mode.hdisplay, m_savedDrm.mode.vdisplay,
               m_savedDrm.mode.vrefresh);
    else
        qWarning("[MpvController] Could not save CRTC state");
}

void MpvController::restoreDrmCrtcState(int fd) {
    if (!m_savedDrm.valid) return;

    int ret = drmModeSetCrtc(fd,
                              m_savedDrm.crtcId,
                              m_savedDrm.fbId,
                              m_savedDrm.x, m_savedDrm.y,
                              &m_savedDrm.connectorId, 1,
                              &m_savedDrm.mode);
    if (ret < 0)
        qWarning("[MpvController] drmModeSetCrtc restore failed: %s", strerror(errno));
    else
        qDebug("[MpvController] CRTC restored (mode %dx%d@%d)",
               m_savedDrm.mode.hdisplay, m_savedDrm.mode.vdisplay,
               m_savedDrm.mode.vrefresh);

    m_savedDrm.valid = false;
}
#endif
