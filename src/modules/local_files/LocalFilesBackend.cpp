#include "LocalFilesBackend.h"

#include "tools/HelperResolver.h"
#include "tools/YouTubePolicy.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMap>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace {

const QStringList kImageExts = {
    QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
    QStringLiteral("gif"), QStringLiteral("webp"), QStringLiteral("bmp"),
    QStringLiteral("tif"), QStringLiteral("tiff")
};

const QStringList kVideoExts = {
    QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("avi"),
    QStringLiteral("mov"), QStringLiteral("m4v"), QStringLiteral("webm"),
    QStringLiteral("wmv"), QStringLiteral("flv"), QStringLiteral("f4v"),
    QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("vob")
};

const QStringList kAudioExts = {
    QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("flac"),
    QStringLiteral("m4a"), QStringLiteral("ogg"), QStringLiteral("aac"),
    QStringLiteral("opus"), QStringLiteral("aiff"), QStringLiteral("aif")
};

const QStringList kPlaylistExts = {QStringLiteral("m3u"), QStringLiteral("m3u8")};
const QStringList kMediaExts = kVideoExts + kImageExts + kAudioExts + kPlaylistExts;
const QStringList kSidecarSubtitleExts = {
    QStringLiteral("srt"), QStringLiteral("ass"), QStringLiteral("ssa"),
    QStringLiteral("sub"), QStringLiteral("vtt"), QStringLiteral("smi")
};
constexpr qint64 kMaxPlaylistBytes = 8 * 1024 * 1024;
constexpr qint64 kMaxStateBytes = 16 * 1024 * 1024;
constexpr qint64 kMaxYouTubePlaylistOutputBytes = 8 * 1024 * 1024;
constexpr int kYouTubePlaylistPublishBatchSize = 16;

bool writeOwnerOnlyAtomicFile(const QString &path, const QByteArray &contents)
{
    constexpr QFileDevice::Permissions permissions =
        QFileDevice::ReadOwner | QFileDevice::WriteOwner;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(permissions)) {
        file.cancelWriting();
        return false;
    }
    if (file.write(contents) != contents.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit() && QFile::setPermissions(path, permissions);
}

QString defaultMediaRoot()
{
    return QDir::home().filePath(QStringLiteral("Desktop"));
}

QString expandedMediaRoot(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
        return defaultMediaRoot();
    if (trimmed == QLatin1String("~"))
        return QDir::homePath();
    if (trimmed.startsWith(QLatin1String("~/")))
        return QDir::home().filePath(trimmed.mid(2));
    return trimmed;
}

QString streamTag(const QJsonObject &stream, const QString &key)
{
    return stream.value(QStringLiteral("tags")).toObject().value(key).toString().trimmed();
}

QString streamLabel(const QJsonObject &stream, const QString &fallbackPrefix, int trackNumber)
{
    const QString displayTitle = streamTag(stream, QStringLiteral("title"));
    const QString language = streamTag(stream, QStringLiteral("language")).toUpper();
    const QString codec = stream.value(QStringLiteral("codec_name")).toString().toUpper();
    const int channels = stream.value(QStringLiteral("channels")).toInt();

    QString label;
    if (!language.isEmpty() && !displayTitle.isEmpty())
        label = language + QStringLiteral(" - ") + displayTitle;
    else if (!displayTitle.isEmpty())
        label = displayTitle;
    else if (!language.isEmpty())
        label = language;
    else
        label = QStringLiteral("%1 %2").arg(fallbackPrefix).arg(trackNumber);

    QStringList details;
    if (!codec.isEmpty())
        details << codec;
    if (channels > 0)
        details << QStringLiteral("%1CH").arg(channels);
    if (!details.isEmpty())
        label += QStringLiteral(" (") + details.join(QStringLiteral(", ")) + QLatin1Char(')');
    return label.toUpper();
}

QString sidecarLabel(const QFileInfo &mediaInfo, const QFileInfo &subtitleInfo,
                     int trackNumber)
{
    QString stem = subtitleInfo.completeBaseName();
    const QString mediaStem = mediaInfo.completeBaseName();
    if (stem == mediaStem)
        stem = QStringLiteral("SUBTITLE %1").arg(trackNumber);
    else if (stem.startsWith(mediaStem + QLatin1Char('.')))
        stem = stem.mid(mediaStem.length() + 1);

    QString label = stem.trimmed().isEmpty()
        ? QStringLiteral("SUBTITLE %1").arg(trackNumber) : stem.trimmed();
    label += QStringLiteral(" (%1)").arg(subtitleInfo.suffix().toUpper());
    return label.toUpper();
}

QString boundedError(const QString &message)
{
    QString safe;
    safe.reserve(qMin(message.size(), 300));
    for (const QChar character : message.trimmed()) {
        if (!character.isNull() && (character.isPrint() || character.isSpace()))
            safe.append(character);
        if (safe.size() >= 300)
            break;
    }
    return safe;
}

QString boundedDisplayTitle(const QString &title)
{
    QString safe;
    safe.reserve(qMin(title.size(), 300));
    for (const QChar character : title.trimmed()) {
        if (character.isSpace()) {
            if (!safe.isEmpty() && safe.back() != QLatin1Char(' '))
                safe.append(QLatin1Char(' '));
        } else if (!character.isNull() && character.isPrint()) {
            safe.append(character);
        }
        if (safe.size() >= 300)
            break;
    }
    return safe.trimmed();
}

QString metadataValue(const QVariantMap &metadata, const QStringList &names)
{
    for (auto iterator = metadata.cbegin(); iterator != metadata.cend(); ++iterator) {
        const bool matches = std::any_of(names.cbegin(), names.cend(),
            [&iterator](const QString &name) {
                return iterator.key().compare(name, Qt::CaseInsensitive) == 0;
            });
        if (!matches)
            continue;
        const QString value = boundedDisplayTitle(iterator.value().toString());
        if (!value.isEmpty())
            return value;
    }
    return {};
}

QPair<QString, QString> splitArtistAndSong(const QString &title)
{
    const QString cleaned = boundedDisplayTitle(title);
    static const QRegularExpression separator(QStringLiteral("\\s+[-\\x{2013}\\x{2014}]\\s+"));
    const QRegularExpressionMatch match = separator.match(cleaned);
    if (!match.hasMatch() || match.capturedStart() <= 0 || match.capturedEnd() >= cleaned.size())
        return {};
    return {cleaned.left(match.capturedStart()).trimmed(),
            cleaned.mid(match.capturedEnd()).trimmed()};
}

template<typename T>
void secureShuffle(T &values)
{
    for (int index = int(values.size()) - 1; index > 0; --index) {
        const int target = QRandomGenerator::global()->bounded(index + 1);
        values.swapItemsAt(index, target);
    }
}

} // namespace

LocalFilesBackend::LocalFilesBackend(const QString &appRoot, const QString &dataRoot,
                                     QObject *parent)
    : QObject(parent), m_appRoot(appRoot), m_dataRoot(dataRoot),
      m_mediaRoot(defaultMediaRoot())
{
    m_audioSocketPath = QStringLiteral("/tmp/owl-switch-local-audio-") +
        QUuid::createUuid().toString(QUuid::Id128).left(16) + QStringLiteral(".sock");
    m_audioIpc = new QLocalSocket(this);
    connect(m_audioIpc, &QLocalSocket::connected, this, [this] {
        m_audioConnectTimer->stop();
        sendAudioCommand({QStringLiteral("observe_property"), 1,
                          QStringLiteral("playlist-pos")});
        sendAudioCommand({QStringLiteral("observe_property"), 2,
                          QStringLiteral("metadata")});
        sendAudioCommand({QStringLiteral("observe_property"), 3,
                          QStringLiteral("media-title")});
    });
    connect(m_audioIpc, &QLocalSocket::readyRead,
            this, &LocalFilesBackend::onAudioIpcReadyRead);
    m_audioConnectTimer = new QTimer(this);
    m_audioConnectTimer->setInterval(100);
    connect(m_audioConnectTimer, &QTimer::timeout,
            this, &LocalFilesBackend::tryConnectAudioIpc);

    m_youtubePlaylistJob = new YouTubeJob(m_appRoot, this);
    connect(m_youtubePlaylistJob, &YouTubeJob::outputReady, this,
            [this](const QByteArray &output) {
        m_youtubePlaylistOutputBuffer += output;
        consumeYouTubePlaylistOutput();
    });
    connect(m_youtubePlaylistJob, &YouTubeJob::completed, this,
            &LocalFilesBackend::onYouTubePlaylistJobCompleted);

    // Local remains configurable, with ~/Desktop as the sole default. The old
    // Loop root is intentionally not migrated into the consolidated module.
    QFile configFile(m_dataRoot + QStringLiteral("/config.json"));
    if (configFile.open(QIODevice::ReadOnly)) {
        const QJsonObject config = QJsonDocument::fromJson(configFile.readAll()).object();
        const QString configuredRoot = config.value(QStringLiteral("modules")).toObject()
            .value(QStringLiteral("com.owlswitch.local_files")).toObject()
            .value(QStringLiteral("media_directory")).toString();
        if (!configuredRoot.isEmpty())
            m_mediaRoot = expandedMediaRoot(configuredRoot);
    }
    QDir().mkpath(m_mediaRoot);
    loadQueues();
}

LocalFilesBackend::~LocalFilesBackend()
{
    clearYouTubePlaylistProcess();
    stopAudio();
}

bool LocalFilesBackend::isImage(const QString &path) const
{
    return kImageExts.contains(QFileInfo(path).suffix().toLower());
}

bool LocalFilesBackend::isAudio(const QString &path) const
{
    return kAudioExts.contains(QFileInfo(path).suffix().toLower());
}

bool LocalFilesBackend::isPlaylist(const QString &path) const
{
    return kPlaylistExts.contains(QFileInfo(path).suffix().toLower());
}

bool LocalFilesBackend::isValidYouTubeVideoId(const QString &videoId)
{
    return YouTubePolicy::isValidVideoId(videoId);
}

QString LocalFilesBackend::canonicalYouTubeVideoUrl(const QString &videoId)
{
    return YouTubePolicy::canonicalVideoUrl(videoId);
}

QString LocalFilesBackend::canonicalYouTubePlaylistUrl(const QString &input)
{
    return YouTubePolicy::canonicalPlaylistUrl(input);
}

bool LocalFilesBackend::isCanonicalYouTubeVideoUrl(const QString &url)
{
    return YouTubePolicy::isCanonicalVideoUrl(url);
}

bool LocalFilesBackend::isPathWithinMediaRoot(const QString &path) const
{
    const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString root = QDir::cleanPath(QFileInfo(m_mediaRoot).absoluteFilePath());
    return clean == root || clean.startsWith(root + QLatin1Char('/'));
}

bool LocalFilesBackend::playlistContainsImages(const QString &path) const
{
    if (!isPlaylist(path) || !isPathWithinMediaRoot(path))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text) ||
        file.size() > kMaxPlaylistBytes) {
        return false;
    }
    const QDir playlistDir = QFileInfo(path).absoluteDir();
    while (!file.atEnd()) {
        const QString entry = QString::fromUtf8(file.readLine()).trimmed();
        if (entry.isEmpty() || entry.startsWith(QLatin1Char('#')))
            continue;
        QString resolved = entry;
        const QUrl url(entry);
        if (url.isLocalFile())
            resolved = url.toLocalFile();
        else if (!url.scheme().isEmpty())
            continue;
        else if (!QFileInfo(entry).isAbsolute())
            resolved = playlistDir.absoluteFilePath(entry);
        if (isPathWithinMediaRoot(resolved) && isImage(resolved))
            return true;
    }
    return false;
}

QString LocalFilesBackend::historyFilePath() const
{
    return m_dataRoot + QStringLiteral("/local_files_history.json");
}

QString LocalFilesBackend::queueFilePath() const
{
    return m_dataRoot + QStringLiteral("/local_queue.json");
}

QString LocalFilesBackend::playbackPlaylistFilePath() const
{
    return m_dataRoot + QStringLiteral("/local_queue.m3u8");
}

QVariantMap LocalFilesBackend::loadHistory() const
{
    QFile file(historyFilePath());
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxStateBytes)
        return {};
    return QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
}

void LocalFilesBackend::saveHistory(const QVariantMap &history)
{
    writeOwnerOnlyAtomicFile(
        historyFilePath(),
        QJsonDocument(QJsonObject::fromVariantMap(history)).toJson(QJsonDocument::Compact));
}

QVariantMap LocalFilesBackend::getSavedPosition(const QString &filePath)
{
    if (!isPathWithinMediaRoot(filePath))
        return {};
    const QVariant value = loadHistory().value(filePath);
    if (!value.isValid())
        return {};
    if (value.canConvert<QVariantMap>()) {
        QVariantMap entry = value.toMap();
        if (!entry.contains(QStringLiteral("plPos")))
            entry[QStringLiteral("plPos")] = -1;
        return entry;
    }
    return {{QStringLiteral("pos"), value.toInt()}, {QStringLiteral("plPos"), -1}};
}

void LocalFilesBackend::savePosition(const QString &filePath, int positionMs, int playlistPos)
{
    if (!isPathWithinMediaRoot(filePath) || positionMs < 0)
        return;
    QVariantMap history = loadHistory();
    history[filePath] = QVariantMap{{QStringLiteral("pos"), positionMs},
                                    {QStringLiteral("plPos"), playlistPos}};
    saveHistory(history);
}

void LocalFilesBackend::clearPosition(const QString &filePath)
{
    if (!isPathWithinMediaRoot(filePath))
        return;
    QVariantMap history = loadHistory();
    history.remove(filePath);
    saveHistory(history);
}

QVariantMap LocalFilesBackend::probeMediaTracks(const QString &filePath)
{
    QVariantMap result{{QStringLiteral("audioStreams"), QVariantList{}},
                       {QStringLiteral("subtitleStreams"), QVariantList{}}};
    const QFileInfo mediaInfo(filePath);
    if (!mediaInfo.exists() || !mediaInfo.isFile() || !isPathWithinMediaRoot(filePath)) {
        qWarning("[LocalFiles] track probe rejected path: %s", qPrintable(filePath));
        return result;
    }

    const QString ffprobe = HelperResolver::ffprobe(m_appRoot);
    if (ffprobe.isEmpty()) {
        qWarning("[LocalFiles] ffprobe unavailable for %s", qPrintable(filePath));
        return result;
    }

    QProcess process;
    process.setProcessEnvironment(HelperResolver::processEnvironment(m_appRoot));
    process.start(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"),
                            QStringLiteral("-print_format"), QStringLiteral("json"),
                            QStringLiteral("-show_streams"), filePath});
    if (!process.waitForStarted(3000)) {
        qWarning("[LocalFiles] ffprobe could not start for %s", qPrintable(filePath));
        return result;
    }
    if (!process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished(1000);
        qWarning("[LocalFiles] ffprobe timed out for %s", qPrintable(filePath));
        return result;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        qWarning("[LocalFiles] ffprobe failed for %s", qPrintable(filePath));
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        process.readAllStandardOutput(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning("[LocalFiles] ffprobe returned invalid JSON for %s", qPrintable(filePath));
        return result;
    }

    QVariantList audioStreams;
    QVariantList subtitleStreams;
    int audioTrack = 0;
    int embeddedSubtitleTrack = 0;
    for (const QJsonValue &value : document.object().value(QStringLiteral("streams")).toArray()) {
        const QJsonObject stream = value.toObject();
        const QString type = stream.value(QStringLiteral("codec_type")).toString();
        if (type == QLatin1String("audio")) {
            ++audioTrack;
            audioStreams.append(QVariantMap{
                {QStringLiteral("id"), audioTrack},
                {QStringLiteral("mpvTrack"), audioTrack},
                {QStringLiteral("displayTitle"),
                 streamLabel(stream, QStringLiteral("AUDIO"), audioTrack)}
            });
        } else if (type == QLatin1String("subtitle")) {
            ++embeddedSubtitleTrack;
            subtitleStreams.append(QVariantMap{
                {QStringLiteral("id"), embeddedSubtitleTrack},
                {QStringLiteral("mpvTrack"), embeddedSubtitleTrack},
                {QStringLiteral("displayTitle"),
                 streamLabel(stream, QStringLiteral("SUBTITLE"), embeddedSubtitleTrack)},
                {QStringLiteral("subFile"), QString{}}
            });
        }
    }

    const QDir mediaDirectory(mediaInfo.absolutePath());
    int sidecarTrack = embeddedSubtitleTrack;
    for (const QString &name : mediaDirectory.entryList(QDir::Files, QDir::Name)) {
        const QFileInfo subtitleInfo(mediaDirectory.absoluteFilePath(name));
        if (!kSidecarSubtitleExts.contains(subtitleInfo.suffix().toLower()))
            continue;
        const QString subtitleStem = subtitleInfo.completeBaseName();
        const QString mediaStem = mediaInfo.completeBaseName();
        if (subtitleStem != mediaStem && !subtitleStem.startsWith(mediaStem + QLatin1Char('.')))
            continue;
        ++sidecarTrack;
        subtitleStreams.append(QVariantMap{
            {QStringLiteral("id"), sidecarTrack},
            {QStringLiteral("mpvTrack"), 0},
            {QStringLiteral("displayTitle"), sidecarLabel(mediaInfo, subtitleInfo, sidecarTrack)},
            {QStringLiteral("subFile"), subtitleInfo.absoluteFilePath()}
        });
    }

    result[QStringLiteral("audioStreams")] = audioStreams;
    result[QStringLiteral("subtitleStreams")] = subtitleStreams;
    return result;
}

bool LocalFilesBackend::isValidQueueKind(const QString &kind) const
{
    return kind == QLatin1String("media") || kind == QLatin1String("soundtrack");
}

bool LocalFilesBackend::queueKindAcceptsPath(const QString &kind, const QString &path) const
{
    if (!isPathWithinMediaRoot(path))
        return false;
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (kind == QLatin1String("media"))
        return kVideoExts.contains(suffix) || kImageExts.contains(suffix);
    if (kind == QLatin1String("soundtrack"))
        return kAudioExts.contains(suffix);
    return false;
}

QVariantList &LocalFilesBackend::mutableQueue(const QString &kind)
{
    return kind == QLatin1String("soundtrack") ? m_soundtrackQueue : m_mediaQueue;
}

const QVariantList &LocalFilesBackend::queue(const QString &kind) const
{
    return kind == QLatin1String("soundtrack") ? m_soundtrackQueue : m_mediaQueue;
}

QVariantList LocalFilesBackend::getQueue(const QString &kind) const
{
    return isValidQueueKind(kind) ? queue(kind) : QVariantList{};
}

int LocalFilesBackend::queueIndexForEntryId(const QString &kind,
                                            const QString &entryId) const
{
    if (!isValidQueueKind(kind))
        return -1;
    const QVariantList &items = queue(kind);
    for (int index = 0; index < items.size(); ++index) {
        if (items.at(index).toMap().value(QStringLiteral("entryId")).toString() == entryId)
            return index;
    }
    return -1;
}

QVariantMap LocalFilesBackend::validatedQueueEntry(const QString &kind,
                                                   const QVariantMap &candidate,
                                                   bool createEntryId,
                                                   bool requireFile) const
{
    if (!isValidQueueKind(kind))
        return {};
    const QString source = candidate.value(QStringLiteral("source"))
                               .toString().trimmed().toLower();
    if (!source.isEmpty() && source != QLatin1String("local") &&
        source != QLatin1String("youtube")) {
        return {};
    }
    const bool isYouTube = source == QLatin1String("youtube");
    if (isYouTube && kind != QLatin1String("soundtrack"))
        return {};

    QString filePath;
    QFileInfo fileInfo;
    QString videoId;
    if (isYouTube) {
        videoId = candidate.value(QStringLiteral("videoId")).toString().trimmed();
        if (!isValidYouTubeVideoId(videoId))
            return {};
    } else {
        filePath = QDir::cleanPath(
            QFileInfo(candidate.value(QStringLiteral("filePath")).toString()).absoluteFilePath());
        fileInfo = QFileInfo(filePath);
        if (filePath.contains(QLatin1Char('\n')) || filePath.contains(QLatin1Char('\r')) ||
            !queueKindAcceptsPath(kind, filePath) ||
            (requireFile && (!fileInfo.exists() || !fileInfo.isFile()))) {
            return {};
        }
    }

    QString entryId = candidate.value(QStringLiteral("entryId")).toString().trimmed();
    if (entryId.isEmpty() && createEntryId)
        entryId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (entryId.length() != 36 || QUuid(entryId).isNull())
        return {};

    const QString requestedStatus = candidate.value(
        QStringLiteral("status"), QStringLiteral("queued")).toString();
    const QString status = requestedStatus == QLatin1String("failed")
        ? QStringLiteral("failed") : QStringLiteral("queued");
    QVariantMap entry{{QStringLiteral("entryId"), entryId},
                      {QStringLiteral("status"), status}};
    if (isYouTube) {
        QString title = boundedDisplayTitle(
            candidate.value(QStringLiteral("displayTitle")).toString());
        if (title.isEmpty())
            title = QStringLiteral("YouTube video %1").arg(videoId);
        entry[QStringLiteral("source")] = QStringLiteral("youtube");
        entry[QStringLiteral("videoId")] = videoId;
        entry[QStringLiteral("displayTitle")] = title;
    } else {
        entry[QStringLiteral("filePath")] = filePath;
        entry[QStringLiteral("displayTitle")] = fileInfo.fileName().left(300);
    }

    if (kind == QLatin1String("media")) {
        entry[QStringLiteral("audioTrack")] = qBound(
            0, candidate.value(QStringLiteral("audioTrack")).toInt(), 999);
        entry[QStringLiteral("subtitleTrack")] = qBound(
            -2, candidate.value(QStringLiteral("subtitleTrack"), -1).toInt(), 999);
        entry[QStringLiteral("subtitleExplicit")] = candidate.value(
            QStringLiteral("subtitleExplicit")).toBool();

        QStringList subtitleFiles;
        for (const QString &subtitlePath : candidate.value(
                 QStringLiteral("subtitleFiles")).toStringList()) {
            const QFileInfo subtitleInfo(subtitlePath);
            if (subtitleFiles.size() >= 8 || !subtitleInfo.exists() || !subtitleInfo.isFile() ||
                !isPathWithinMediaRoot(subtitlePath) ||
                !kSidecarSubtitleExts.contains(subtitleInfo.suffix().toLower())) {
                continue;
            }
            subtitleFiles.append(QDir::cleanPath(subtitleInfo.absoluteFilePath()));
        }
        entry[QStringLiteral("subtitleFiles")] = subtitleFiles;
    }

    const QString error = boundedError(candidate.value(QStringLiteral("error")).toString());
    if (status == QLatin1String("failed") && !error.isEmpty())
        entry[QStringLiteral("error")] = error;
    return entry;
}

QStringList LocalFilesBackend::expandedPlaylistEntries(const QString &playlistPath,
                                                       const QString &kind,
                                                       QSet<QString> &visited,
                                                       int depth) const
{
    if (depth > kMaxPlaylistDepth || !isPlaylist(playlistPath) ||
        !isPathWithinMediaRoot(playlistPath)) {
        return {};
    }
    const QString cleanPlaylist = QDir::cleanPath(QFileInfo(playlistPath).absoluteFilePath());
    if (visited.contains(cleanPlaylist))
        return {};
    visited.insert(cleanPlaylist);

    QFile file(cleanPlaylist);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text) ||
        file.size() > kMaxPlaylistBytes) {
        return {};
    }
    const QDir playlistDirectory = QFileInfo(cleanPlaylist).absoluteDir();
    QStringList entries;
    while (!file.atEnd() && entries.size() < kMaxQueueEntries) {
        QString value = QString::fromUtf8(file.readLine()).trimmed();
        if (!value.isEmpty() && value.front() == QChar::ByteOrderMark)
            value.removeFirst();
        if (value.isEmpty() || value.startsWith(QLatin1Char('#')))
            continue;

        QString path = value;
        const QUrl url(value);
        if (url.isLocalFile())
            path = url.toLocalFile();
        else if (!url.scheme().isEmpty())
            continue;
        else if (!QFileInfo(value).isAbsolute())
            path = playlistDirectory.absoluteFilePath(value);
        path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
        if (!isPathWithinMediaRoot(path))
            continue;
        if (isPlaylist(path)) {
            const QStringList nested = expandedPlaylistEntries(path, kind, visited, depth + 1);
            for (const QString &nestedPath : nested) {
                if (entries.size() >= kMaxQueueEntries)
                    break;
                entries.append(nestedPath);
            }
        } else {
            const QFileInfo info(path);
            if (info.exists() && info.isFile() && queueKindAcceptsPath(kind, path))
                entries.append(path);
        }
    }
    return entries;
}

int LocalFilesBackend::enqueue(const QString &kind, const QVariantMap &candidate)
{
    if (!isValidQueueKind(kind))
        return 0;
    QVariantList additions;
    const QString requestedPath = candidate.value(QStringLiteral("filePath")).toString();
    if (isPlaylist(requestedPath)) {
        QSet<QString> visited;
        const QStringList paths = expandedPlaylistEntries(requestedPath, kind, visited, 0);
        for (const QString &path : paths) {
            QVariantMap item = candidate;
            item[QStringLiteral("filePath")] = path;
            item.remove(QStringLiteral("entryId"));
            const QVariantMap validated = validatedQueueEntry(kind, item, true, true);
            if (!validated.isEmpty())
                additions.append(validated);
        }
    } else {
        const QVariantMap validated = validatedQueueEntry(kind, candidate, true, true);
        if (!validated.isEmpty())
            additions.append(validated);
    }

    QVariantList &target = mutableQueue(kind);
    const int available = qMax(0, kMaxQueueEntries - target.size());
    if (available == 0 || additions.isEmpty())
        return 0;
    if (additions.size() > available)
        additions = additions.mid(0, available);
    const int previousSize = target.size();
    target.append(additions);
    if (publishQueues(kind))
        return additions.size();
    while (target.size() > previousSize)
        target.removeLast();
    return 0;
}

bool LocalFilesBackend::removeQueueEntry(const QString &kind, const QString &entryId)
{
    const int index = queueIndexForEntryId(kind, entryId);
    if (index < 0)
        return false;
    QVariantList &target = mutableQueue(kind);
    const QVariant removed = target.takeAt(index);
    if (publishQueues(kind))
        return true;
    target.insert(index, removed);
    return false;
}

bool LocalFilesBackend::moveQueueEntry(const QString &kind, int fromIndex, int toIndex)
{
    if (!isValidQueueKind(kind))
        return false;
    QVariantList &target = mutableQueue(kind);
    if (fromIndex < 0 || fromIndex >= target.size() ||
        toIndex < 0 || toIndex >= target.size() || fromIndex == toIndex) {
        return false;
    }
    target.move(fromIndex, toIndex);
    if (publishQueues(kind))
        return true;
    target.move(toIndex, fromIndex);
    return false;
}

void LocalFilesBackend::clearQueue(const QString &kind)
{
    if (!isValidQueueKind(kind))
        return;
    QVariantList &target = mutableQueue(kind);
    if (target.isEmpty())
        return;
    const QVariantList previous = target;
    target.clear();
    if (!publishQueues(kind))
        target = previous;
}

bool LocalFilesBackend::failQueueEntry(const QString &kind, const QString &entryId,
                                       const QString &message)
{
    const int index = queueIndexForEntryId(kind, entryId);
    if (index < 0)
        return false;
    QVariantList &target = mutableQueue(kind);
    const QVariantMap previous = target.at(index).toMap();
    QVariantMap entry = previous;
    entry[QStringLiteral("status")] = QStringLiteral("failed");
    entry[QStringLiteral("error")] = boundedError(message);
    target[index] = entry;
    if (publishQueues(kind))
        return true;
    target[index] = previous;
    return false;
}

bool LocalFilesBackend::resetQueueEntry(const QString &kind, const QString &entryId)
{
    const int index = queueIndexForEntryId(kind, entryId);
    if (index < 0)
        return false;
    QVariantList &target = mutableQueue(kind);
    const QVariantMap previous = target.at(index).toMap();
    QVariantMap entry = previous;
    entry[QStringLiteral("status")] = QStringLiteral("queued");
    entry.remove(QStringLiteral("error"));
    target[index] = entry;
    if (publishQueues(kind))
        return true;
    target[index] = previous;
    return false;
}

void LocalFilesBackend::loadQueues()
{
    QFile file(queueFilePath());
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxStateBytes)
        return;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return;
    const QJsonObject root = document.object();
    const int schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt();
    if (schemaVersion != 1 && schemaVersion != kQueueSchemaVersion)
        return;

    auto loadKind = [this, &root](const QString &kind, const QString &key) {
        QSet<QString> entryIds;
        QVariantList &target = mutableQueue(kind);
        for (const QJsonValue &value : root.value(key).toArray()) {
            const QVariantMap entry = validatedQueueEntry(
                kind, value.toObject().toVariantMap(), false, false);
            const QString entryId = entry.value(QStringLiteral("entryId")).toString();
            if (entry.isEmpty() || entryIds.contains(entryId) ||
                target.size() >= kMaxQueueEntries) {
                continue;
            }
            entryIds.insert(entryId);
            target.append(entry);
        }
    };
    loadKind(QStringLiteral("media"), QStringLiteral("media"));
    loadKind(QStringLiteral("soundtrack"), QStringLiteral("soundtrack"));
}

bool LocalFilesBackend::saveQueues() const
{
    auto arrayFromQueue = [](const QVariantList &items) {
        QJsonArray array;
        for (const QVariant &item : items)
            array.append(QJsonObject::fromVariantMap(item.toMap()));
        return array;
    };
    const QJsonObject root{
        {QStringLiteral("schemaVersion"), kQueueSchemaVersion},
        {QStringLiteral("media"), arrayFromQueue(m_mediaQueue)},
        {QStringLiteral("soundtrack"), arrayFromQueue(m_soundtrackQueue)}
    };
    return writeOwnerOnlyAtomicFile(
        queueFilePath(), QJsonDocument(root).toJson(QJsonDocument::Compact));
}

bool LocalFilesBackend::publishQueues(const QString &changedKind)
{
    if (!saveQueues())
        return false;
    emit queueChanged(changedKind, queue(changedKind));
    return true;
}

void LocalFilesBackend::pruneQueuesForMediaRoot()
{
    auto prune = [this](const QString &kind) {
        QVariantList retained;
        for (const QVariant &value : queue(kind)) {
            const QVariantMap entry = validatedQueueEntry(kind, value.toMap(), false, false);
            if (!entry.isEmpty())
                retained.append(entry);
        }
        mutableQueue(kind) = retained;
    };
    const QVariantList oldMedia = m_mediaQueue;
    const QVariantList oldSoundtrack = m_soundtrackQueue;
    prune(QStringLiteral("media"));
    prune(QStringLiteral("soundtrack"));
    if (oldMedia != m_mediaQueue || oldSoundtrack != m_soundtrackQueue) {
        saveQueues();
        emit queueChanged(QStringLiteral("media"), m_mediaQueue);
        emit queueChanged(QStringLiteral("soundtrack"), m_soundtrackQueue);
    }
}

QString LocalFilesBackend::writePlaybackPlaylist(const QVariantList &entries) const
{
    QByteArray contents("#EXTM3U\n");
    int written = 0;
    for (const QVariant &value : entries) {
        const QString path = value.toMap().value(QStringLiteral("filePath")).toString();
        if (!queueKindAcceptsPath(QStringLiteral("media"), path))
            continue;
        contents.append(path.toUtf8());
        contents.append('\n');
        ++written;
    }
    if (written == 0 || !writeOwnerOnlyAtomicFile(playbackPlaylistFilePath(), contents))
        return {};
    return playbackPlaylistFilePath();
}

QVariantMap LocalFilesBackend::preparePlayback(const QString &startEntryId, bool shuffle)
{
    if (m_mediaQueue.isEmpty())
        return {};
    QVariantList entries = m_mediaQueue;
    int startIndex = queueIndexForEntryId(QStringLiteral("media"), startEntryId);
    if (shuffle) {
        QVariant selected;
        if (startIndex >= 0)
            selected = entries.takeAt(startIndex);
        secureShuffle(entries);
        if (selected.isValid())
            entries.prepend(selected);
        startIndex = 0;
    } else if (startIndex < 0) {
        startIndex = 0;
    }
    const QString playlistPath = writePlaybackPlaylist(entries);
    if (playlistPath.isEmpty())
        return {};
    return {{QStringLiteral("playlistPath"), playlistPath},
            {QStringLiteral("entries"), entries},
            {QStringLiteral("startIndex"), startIndex},
            {QStringLiteral("muteMainAudio"), !m_soundtrackQueue.isEmpty()}};
}

QStringList LocalFilesBackend::soundtrackPaths(bool shuffle) const
{
    QStringList paths;
    for (const QVariant &value : m_soundtrackQueue) {
        const QVariantMap entry = value.toMap();
        if (entry.value(QStringLiteral("source")).toString() == QLatin1String("youtube")) {
            const QString url = canonicalYouTubeVideoUrl(
                entry.value(QStringLiteral("videoId")).toString());
            if (!url.isEmpty())
                paths.append(url);
            continue;
        }
        const QString path = entry.value(QStringLiteral("filePath")).toString();
        if (queueKindAcceptsPath(QStringLiteral("soundtrack"), path))
            paths.append(path);
    }
    if (shuffle)
        secureShuffle(paths);
    return paths;
}

QVariantMap LocalFilesBackend::currentSoundtrack() const
{
    return m_currentSoundtrack;
}

bool LocalFilesBackend::importYouTubePlaylist(const QString &url)
{
    if (m_youtubePlaylistJob->isRunning()) {
        emit youtubePlaylistImportFailed(
            QStringLiteral("A YouTube playlist is already loading."));
        return false;
    }
    clearYouTubePlaylistProcess();

    const QString canonicalUrl = canonicalYouTubePlaylistUrl(url);
    if (canonicalUrl.isEmpty()) {
        emit youtubePlaylistImportFailed(
            QStringLiteral("Enter a valid public or unlisted YouTube playlist URL."));
        return false;
    }
    const int available = kMaxQueueEntries - m_soundtrackQueue.size();
    if (available <= 0) {
        emit youtubePlaylistImportFailed(QStringLiteral("The soundtrack queue is full."));
        return false;
    }

    const QString ytDlp = HelperResolver::ytDlp(m_appRoot);
    const QStringList arguments = YouTubePolicy::playlistInventoryArguments(
        m_appRoot, {canonicalUrl}, available);
    if (ytDlp.isEmpty() || arguments.isEmpty()) {
        emit youtubePlaylistImportFailed(
            QStringLiteral("Bundled YouTube helpers are unavailable."));
        return false;
    }

    m_youtubePlaylistOutputBuffer.clear();
    m_youtubePlaylistEntries.clear();
    m_youtubePlaylistAddedCount = 0;
    m_youtubePlaylistSaveFailed = false;

    emit youtubePlaylistImportStarted();
    if (m_youtubePlaylistJob->start(arguments, 120000,
                                    kMaxYouTubePlaylistOutputBytes, 8192)) {
        return true;
    }
    clearYouTubePlaylistProcess();
    emit youtubePlaylistImportFailed(
        QStringLiteral("Bundled YouTube helpers are unavailable."));
    return false;
}

void LocalFilesBackend::cancelYouTubePlaylistImport()
{
    if (!m_youtubePlaylistJob->isRunning())
        return;
    const int addedCount = m_youtubePlaylistAddedCount;
    clearYouTubePlaylistProcess();
    if (addedCount > 0) {
        emit youtubePlaylistImportFinished(addedCount);
    } else {
        emit youtubePlaylistImportFailed(QStringLiteral("YouTube playlist import canceled."));
    }
}

void LocalFilesBackend::consumeYouTubePlaylistOutput(bool includeRemainder)
{
    int newline = -1;
    while ((newline = m_youtubePlaylistOutputBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_youtubePlaylistOutputBuffer.left(newline).trimmed();
        m_youtubePlaylistOutputBuffer.remove(0, newline + 1);
        consumeYouTubePlaylistLine(line);
    }
    if (includeRemainder && !m_youtubePlaylistOutputBuffer.trimmed().isEmpty()) {
        consumeYouTubePlaylistLine(m_youtubePlaylistOutputBuffer.trimmed());
        m_youtubePlaylistOutputBuffer.clear();
    }
}

void LocalFilesBackend::consumeYouTubePlaylistLine(const QByteArray &line)
{
    if (m_youtubePlaylistSaveFailed || line.isEmpty() ||
        m_youtubePlaylistAddedCount + m_youtubePlaylistEntries.size() >= kMaxQueueEntries)
        return;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return;
    const QJsonObject object = document.object();
    const QString videoId = object.value(QStringLiteral("id")).toString().trimmed();
    if (!isValidYouTubeVideoId(videoId))
        return;
    const QVariantMap entry = validatedQueueEntry(
        QStringLiteral("soundtrack"),
        {{QStringLiteral("source"), QStringLiteral("youtube")},
         {QStringLiteral("videoId"), videoId},
         {QStringLiteral("displayTitle"),
          object.value(QStringLiteral("title")).toString()}},
        true, false);
    if (!entry.isEmpty())
        m_youtubePlaylistEntries.append(entry);
    if (m_youtubePlaylistAddedCount == 0 ||
        m_youtubePlaylistEntries.size() >= kYouTubePlaylistPublishBatchSize) {
        if (!flushYouTubePlaylistEntries()) {
            m_youtubePlaylistSaveFailed = true;
            m_youtubePlaylistJob->cancelSilently();
            QTimer::singleShot(0, this, [this] {
                onYouTubePlaylistJobCompleted(YouTubeJob::Failure::ProcessFailed,
                                              -1, {});
            });
        }
    }
}

bool LocalFilesBackend::flushYouTubePlaylistEntries()
{
    if (m_youtubePlaylistEntries.isEmpty())
        return true;
    QVariantList &target = mutableQueue(QStringLiteral("soundtrack"));
    const int available = qMax(0, kMaxQueueEntries - target.size());
    const QVariantList additions = m_youtubePlaylistEntries.mid(0, available);
    m_youtubePlaylistEntries.clear();
    if (additions.isEmpty())
        return true;

    const int previousSize = target.size();
    target.append(additions);
    if (!publishQueues(QStringLiteral("soundtrack"))) {
        while (target.size() > previousSize)
            target.removeLast();
        return false;
    }
    m_youtubePlaylistAddedCount += additions.size();
    emit youtubePlaylistImportProgress(m_youtubePlaylistAddedCount);
    return true;
}

void LocalFilesBackend::onYouTubePlaylistJobCompleted(
    YouTubeJob::Failure failure, int exitCode, const QString &safeError)
{
    Q_UNUSED(exitCode)
    Q_UNUSED(safeError)
    consumeYouTubePlaylistOutput(true);
    if (!m_youtubePlaylistSaveFailed && !flushYouTubePlaylistEntries())
        m_youtubePlaylistSaveFailed = true;

    const bool helperSucceeded = failure == YouTubeJob::Failure::None;
    const int addedCount = m_youtubePlaylistAddedCount;
    const bool saveFailed = m_youtubePlaylistSaveFailed;
    clearYouTubePlaylistProcess();
    if (saveFailed) {
        emit youtubePlaylistImportFailed(
            QStringLiteral("Could not save the YouTube playlist."));
        return;
    }
    if ((!helperSucceeded && addedCount == 0) || addedCount == 0) {
        emit youtubePlaylistImportFailed(
            QStringLiteral("Could not load playable videos from that YouTube playlist."));
        return;
    }
    emit youtubePlaylistImportFinished(addedCount);
}

void LocalFilesBackend::clearYouTubePlaylistProcess()
{
    if (m_youtubePlaylistJob)
        m_youtubePlaylistJob->cancelSilently();
    m_youtubePlaylistOutputBuffer.clear();
    m_youtubePlaylistEntries.clear();
    m_youtubePlaylistAddedCount = 0;
    m_youtubePlaylistSaveFailed = false;
}

void LocalFilesBackend::startAudio(const QStringList &paths, bool shuffle)
{
    stopAudio();
    QStringList validatedPaths;
    for (const QString &path : paths) {
        if (validatedPaths.size() >= kMaxQueueEntries)
            break;
        if (isCanonicalYouTubeVideoUrl(path)) {
            validatedPaths.append(path);
            continue;
        }
        const QFileInfo info(path);
        if (info.exists() && info.isFile() &&
            queueKindAcceptsPath(QStringLiteral("soundtrack"), path)) {
            validatedPaths.append(QDir::cleanPath(info.absoluteFilePath()));
        }
    }
    if (validatedPaths.isEmpty())
        return;
    if (shuffle)
        secureShuffle(validatedPaths);
    m_audioPaths = validatedPaths;
    m_audioShuffle = false;
    m_audioStopRequested = false;
    m_audioPlaybackReady = false;
    m_audioRespawnCount = 0;
    m_audioPlaylistPos = -1;
    m_audioMetadata.clear();
    m_audioMediaTitle.clear();
    m_currentSoundtrack.clear();
    ++m_audioFileSerial;
    m_publishedAudioFileSerial = 0;
    ++m_audioGeneration;
    launchAudioProcess();
}

void LocalFilesBackend::launchAudioProcess()
{
    if (m_audioStopRequested || m_audioPaths.isEmpty())
        return;
    const QString binary = HelperResolver::mpv(m_appRoot);
    if (binary.isEmpty()) {
        qWarning("[LocalFiles] mpv unavailable; soundtrack will not play");
        return;
    }
    m_audioConnectTimer->stop();
    m_audioIpc->abort();
    QFile::remove(m_audioSocketPath);

    QStringList arguments = m_audioPaths;
    arguments << QStringLiteral("--loop-playlist=inf")
              << QStringLiteral("--input-ipc-server=") + m_audioSocketPath;
    if (m_audioShuffle)
        arguments << QStringLiteral("--shuffle");
    const bool hasYouTube = std::any_of(
        m_audioPaths.cbegin(), m_audioPaths.cend(),
        [](const QString &path) {
            return LocalFilesBackend::isCanonicalYouTubeVideoUrl(path);
        });
    if (hasYouTube) {
        arguments << YouTubePolicy::mpvArguments(
            m_appRoot, YouTubePolicy::MediaProfile::AudioOnly);
    } else {
        arguments << QStringLiteral("--no-video");
    }
    arguments << QStringLiteral("--no-terminal")
              << QStringLiteral("--really-quiet");

    m_audioProcess = new QProcess(this);
    m_audioProcess->setProcessEnvironment(HelperResolver::processEnvironment(m_appRoot));
    connect(m_audioProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &LocalFilesBackend::onAudioProcessFinished);
    connect(m_audioProcess, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            onAudioProcessFinished();
    });
    m_audioProcess->start(binary, arguments);
    m_audioConnectTimer->start();
    qDebug("[LocalFiles] soundtrack process started: %lld track(s)",
           static_cast<long long>(m_audioPaths.size()));
}

void LocalFilesBackend::tryConnectAudioIpc()
{
    if (m_audioStopRequested || !m_audioProcess ||
        m_audioProcess->state() == QProcess::NotRunning) {
        m_audioConnectTimer->stop();
        return;
    }
    if (m_audioIpc->state() == QLocalSocket::ConnectedState ||
        m_audioIpc->state() == QLocalSocket::ConnectingState) {
        return;
    }
    m_audioIpc->connectToServer(m_audioSocketPath);
}

void LocalFilesBackend::onAudioIpcReadyRead()
{
    while (m_audioIpc->canReadLine()) {
        const QJsonObject message = QJsonDocument::fromJson(
            m_audioIpc->readLine().trimmed()).object();
        const QString event = message.value(QStringLiteral("event")).toString();
        if (event == QLatin1String("property-change")) {
            const QString name = message.value(QStringLiteral("name")).toString();
            const QJsonValue data = message.value(QStringLiteral("data"));
            if (name == QLatin1String("playlist-pos") && data.isDouble())
                m_audioPlaylistPos = data.toInt(-1);
            else if (name == QLatin1String("metadata") && data.isObject())
                m_audioMetadata = data.toObject().toVariantMap();
            else if (name == QLatin1String("media-title") && data.isString())
                m_audioMediaTitle = boundedDisplayTitle(data.toString());
            continue;
        }
        if (event == QLatin1String("file-loaded")) {
            ++m_audioFileSerial;
            m_audioMetadata.clear();
            m_audioMediaTitle.clear();
            continue;
        }
        if (event != QLatin1String("playback-restart"))
            continue;
        if (!m_audioPlaybackReady) {
            m_audioPlaybackReady = true;
            emit audioPlaybackStarted();
        }
        const quint64 generation = m_audioGeneration;
        const quint64 fileSerial = m_audioFileSerial;
        QTimer::singleShot(100, this, [this, generation, fileSerial] {
            publishCurrentSoundtrack(generation, fileSerial);
        });
    }
}

void LocalFilesBackend::sendAudioCommand(const QJsonArray &command)
{
    if (m_audioIpc->state() != QLocalSocket::ConnectedState)
        return;
    const QJsonObject request{{QStringLiteral("command"), command}};
    m_audioIpc->write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
}

QString LocalFilesBackend::fallbackSoundtrackTitle(const QString &path) const
{
    for (const QVariant &value : m_soundtrackQueue) {
        const QVariantMap entry = value.toMap();
        if (entry.value(QStringLiteral("source")).toString() == QLatin1String("youtube")) {
            if (canonicalYouTubeVideoUrl(entry.value(QStringLiteral("videoId")).toString()) == path)
                return entry.value(QStringLiteral("displayTitle")).toString();
            continue;
        }
        if (QDir::cleanPath(entry.value(QStringLiteral("filePath")).toString()) ==
            QDir::cleanPath(path)) {
            const QString displayTitle = entry.value(QStringLiteral("displayTitle")).toString();
            const QFileInfo displayInfo(displayTitle);
            return displayInfo.completeBaseName().isEmpty()
                ? displayTitle : displayInfo.completeBaseName();
        }
    }
    const QFileInfo info(path);
    return info.completeBaseName().isEmpty() ? info.fileName() : info.completeBaseName();
}

QVariantMap LocalFilesBackend::buildCurrentSoundtrack() const
{
    if (m_audioPaths.isEmpty())
        return {};
    const int index = qBound(0, m_audioPlaylistPos, m_audioPaths.size() - 1);
    const QString path = m_audioPaths.at(index);
    QString fallback = boundedDisplayTitle(fallbackSoundtrackTitle(path));
    if (fallback.isEmpty())
        fallback = boundedDisplayTitle(m_audioMediaTitle);
    if (fallback.isEmpty())
        fallback = QStringLiteral("Unknown song");

    QString artist = metadataValue(
        m_audioMetadata,
        {QStringLiteral("artist"), QStringLiteral("album_artist"),
         QStringLiteral("album artist"), QStringLiteral("performer")});
    QString song = metadataValue(m_audioMetadata, {QStringLiteral("title")});
    const QPair<QString, QString> parsedFallback = splitArtistAndSong(fallback);
    const QPair<QString, QString> parsedMediaTitle = splitArtistAndSong(m_audioMediaTitle);
    if (artist.isEmpty())
        artist = !parsedFallback.first.isEmpty()
            ? parsedFallback.first : parsedMediaTitle.first;
    if (song.isEmpty())
        song = !parsedFallback.second.isEmpty()
            ? parsedFallback.second : parsedMediaTitle.second;
    if (song.isEmpty())
        song = boundedDisplayTitle(m_audioMediaTitle);
    if (song.isEmpty())
        song = fallback;
    if (artist.isEmpty())
        artist = QStringLiteral("SOUNDTRACK");

    return {{QStringLiteral("artist"), boundedDisplayTitle(artist)},
            {QStringLiteral("song"), boundedDisplayTitle(song)}};
}

void LocalFilesBackend::publishCurrentSoundtrack(quint64 generation, quint64 fileSerial)
{
    if (m_audioStopRequested || generation != m_audioGeneration ||
        fileSerial != m_audioFileSerial ||
        fileSerial == m_publishedAudioFileSerial) {
        return;
    }
    const QVariantMap track = buildCurrentSoundtrack();
    if (track.isEmpty())
        return;
    m_publishedAudioFileSerial = fileSerial;
    m_currentSoundtrack = track;
    emit audioTrackStarted(track);
}

void LocalFilesBackend::stopAudio()
{
    m_audioStopRequested = true;
    m_audioRespawnCount = 0;
    ++m_audioGeneration;
    ++m_audioFileSerial;
    m_audioPlaylistPos = -1;
    m_audioMetadata.clear();
    m_audioMediaTitle.clear();
    m_currentSoundtrack.clear();
    m_publishedAudioFileSerial = 0;
    m_audioConnectTimer->stop();
    m_audioIpc->abort();
    QFile::remove(m_audioSocketPath);
    if (!m_audioProcess)
        return;
    m_audioProcess->disconnect(this);
    if (m_audioProcess->state() != QProcess::NotRunning) {
        m_audioProcess->terminate();
        m_audioProcess->waitForFinished(1000);
    }
    m_audioProcess->deleteLater();
    m_audioProcess = nullptr;
}

void LocalFilesBackend::onAudioProcessFinished()
{
    m_audioConnectTimer->stop();
    m_audioIpc->abort();
    ++m_audioFileSerial;
    m_audioPlaylistPos = -1;
    m_audioMetadata.clear();
    m_audioMediaTitle.clear();
    m_currentSoundtrack.clear();
    m_publishedAudioFileSerial = 0;
    QFile::remove(m_audioSocketPath);
    if (m_audioProcess) {
        m_audioProcess->disconnect(this);
        m_audioProcess->deleteLater();
        m_audioProcess = nullptr;
    }
    if (m_audioStopRequested || m_audioPaths.isEmpty())
        return;
    static constexpr int kMaxRespawns = 5;
    if (m_audioRespawnCount >= kMaxRespawns) {
        qWarning("[LocalFiles] soundtrack repeatedly failed; stopping retries");
        if (!m_audioPlaybackReady)
            emit audioPlaybackFailed(QStringLiteral("Soundtrack audio could not start."));
        return;
    }
    ++m_audioRespawnCount;
    const quint64 generation = m_audioGeneration;
    const int delayMs = qMin(5000, 500 * (1 << qMin(m_audioRespawnCount - 1, 3)));
    QTimer::singleShot(delayMs, this, [this, generation] {
        if (!m_audioStopRequested && generation == m_audioGeneration)
            launchAudioProcess();
    });
}

void LocalFilesBackend::get_repeat_mode_options()
{
    const QVariantList options{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("off")},
                    {QStringLiteral("label"), QStringLiteral("Off")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("queue")},
                    {QStringLiteral("label"), QStringLiteral("Repeat Queue")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("one")},
                    {QStringLiteral("label"), QStringLiteral("Repeat One")}}
    };
    emit dynamicOptionsReady(QStringLiteral("repeat_mode"), options);
}

void LocalFilesBackend::get_resume_playback_options()
{
    const QVariantList options{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("ask")},
                    {QStringLiteral("label"), QStringLiteral("Ask")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("yes")},
                    {QStringLiteral("label"), QStringLiteral("Always")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("no")},
                    {QStringLiteral("label"), QStringLiteral("Never")}}
    };
    emit dynamicOptionsReady(QStringLiteral("resume_playback"), options);
}

void LocalFilesBackend::get_shuffle_playback_options()
{
    const QVariantList options{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("ask")},
                    {QStringLiteral("label"), QStringLiteral("Ask")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("yes")},
                    {QStringLiteral("label"), QStringLiteral("Always")},
                    {QStringLiteral("old"), true}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("no")},
                    {QStringLiteral("label"), QStringLiteral("Never")},
                    {QStringLiteral("old"), false}}
    };
    emit dynamicOptionsReady(QStringLiteral("shuffle_playback"), options);
}

void LocalFilesBackend::get_auto_subtitles_options()
{
    const QVariantList options{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("forced")},
                    {QStringLiteral("label"), QStringLiteral("Forced Only")},
                    {QStringLiteral("old"), false}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("on")},
                    {QStringLiteral("label"), QStringLiteral("On")},
                    {QStringLiteral("old"), true}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("off")},
                    {QStringLiteral("label"), QStringLiteral("Off")}}
    };
    emit dynamicOptionsReady(QStringLiteral("auto_subtitles"), options);
}

void LocalFilesBackend::get_subtitle_languages()
{
    QVariantList options{QVariantMap{{QStringLiteral("id"), QStringLiteral("-")},
                                     {QStringLiteral("label"), QStringLiteral("Any")}}};
    QMap<QString, QString> labelsByCode;
    const QList<QLocale> locales = QLocale::matchingLocales(
        QLocale::AnyLanguage, QLocale::AnyScript, QLocale::AnyTerritory);
    for (const QLocale &locale : locales) {
        const QString code = QLocale::languageToCode(locale.language(), QLocale::ISO639Part1);
        const QString label = QLocale::languageToString(locale.language());
        if (code.size() == 2 && !label.isEmpty() && !labelsByCode.contains(code))
            labelsByCode.insert(code, label);
    }
    for (auto it = labelsByCode.cbegin(); it != labelsByCode.cend(); ++it)
        options << QVariantMap{{QStringLiteral("id"), it.key()},
                               {QStringLiteral("label"), it.value()}};
    emit dynamicOptionsReady(QStringLiteral("sub_lang"), options);
}

void LocalFilesBackend::get_image_duration_options()
{
    QVariantList options;
    for (const int seconds : {5, 10, 30, 60}) {
        options << QVariantMap{{QStringLiteral("id"), QString::number(seconds)},
                               {QStringLiteral("label"),
                                QStringLiteral("%1 Seconds").arg(seconds)}};
    }
    emit dynamicOptionsReady(QStringLiteral("image_duration"), options);
}

QString LocalFilesBackend::mediaRoot() const
{
    return m_mediaRoot;
}

void LocalFilesBackend::setMediaRoot(const QString &path)
{
    m_mediaRoot = expandedMediaRoot(path);
    QDir().mkpath(m_mediaRoot);
    pruneQueuesForMediaRoot();
    qDebug("[LocalFiles] media root: %s", qPrintable(m_mediaRoot));
}

void LocalFilesBackend::onSettingChanged(const QString &moduleId, const QString &key,
                                         const QVariant &value)
{
    if (moduleId == QLatin1String("com.owlswitch.local_files") &&
        key == QLatin1String("media_directory")) {
        setMediaRoot(value.toString());
    }
}

QVariantList LocalFilesBackend::getItems(const QString &path)
{
    QVariantList result;
    QDir directory(path);
    if (!directory.exists()) {
        qWarning("[LocalFiles] directory not found: %s", qPrintable(path));
        return result;
    }
    if (!isPathWithinMediaRoot(path)) {
        qWarning("[LocalFiles] path escapes media root: %s", qPrintable(path));
        return result;
    }

    for (const QString &name : directory.entryList(
             QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (isPlaylist(name)) {
            const QString innerPath = directory.absoluteFilePath(name + QLatin1Char('/') + name);
            if (QFileInfo::exists(innerPath)) {
                result.append(QVariantMap{{QStringLiteral("name"), name},
                                          {QStringLiteral("path"), innerPath},
                                          {QStringLiteral("isFolder"), false}});
                continue;
            }
        }
        result.append(QVariantMap{{QStringLiteral("name"), name},
                                  {QStringLiteral("path"), directory.absoluteFilePath(name)},
                                  {QStringLiteral("isFolder"), true}});
    }

    for (const QString &name : directory.entryList(QDir::Files, QDir::Name)) {
        if (!kMediaExts.contains(QFileInfo(name).suffix().toLower()))
            continue;
        result.append(QVariantMap{{QStringLiteral("name"), name},
                                  {QStringLiteral("path"), directory.absoluteFilePath(name)},
                                  {QStringLiteral("isFolder"), false}});
    }
    return result;
}
