#include "modules/local_files/LocalFilesBackend.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

class LocalFilesBackendTest final : public QObject {
    Q_OBJECT

private slots:
    void recognizesSupportedTypes();
    void enforcesPathBoundariesButAllowsRootSymlinks();
    void detectsRelativeImagesInPlaylists();
    void persistsDuplicateQueuesAndExpandsLocalPlaylists();
    void loadsLegacySchemaOneLocalQueues();
    void retainsFailedEntriesAndPreparesSelectedPlayback();
    void mediaQueuePlanMutesWheneverSoundtrackIsNonEmpty();
    void importsYouTubePlaylistInOrderAndPersistsEntries();
    void importsLiveYouTubePlaylist();
    void rejectsNonYouTubePlaylistUrls();
    void changingMediaRootPrunesOutOfRootQueueItems();
    void usesBundledMpvForSeparateAudio();
    void streamsYouTubeSoundtrackWithBundledHelpers();
    void reportsWhenSeparateAudioActuallyStarts();
    void reportsTaggedAndFallbackSoundtrackMetadata();
};

static bool writeFile(const QString &path, const QByteArray &contents = {})
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    return file.write(contents) == contents.size();
}

static bool writeExecutable(const QString &path, const QByteArray &contents)
{
    if (!writeFile(path, contents))
        return false;
    return QFile::setPermissions(path, QFileDevice::ReadOwner |
                                 QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

static bool writeLocalConfig(const QString &dataRoot, const QString &mediaRoot)
{
    const QJsonObject config{{QStringLiteral("modules"), QJsonObject{
        {QStringLiteral("com.owlswitch.local_files"), QJsonObject{
            {QStringLiteral("media_directory"), mediaRoot}
        }},
        {QStringLiteral("com.owlswitch.ambient_mode"), QJsonObject{
            {QStringLiteral("media_directory"), QStringLiteral("/ignored/legacy/loop")}
        }}
    }}};
    return writeFile(QDir(dataRoot).filePath(QStringLiteral("config.json")),
                     QJsonDocument(config).toJson(QJsonDocument::Compact));
}

void LocalFilesBackendTest::recognizesSupportedTypes()
{
    QTemporaryDir app;
    QTemporaryDir data;
    LocalFilesBackend backend(app.path(), data.path());
    QVERIFY(backend.isImage(QStringLiteral("photo.WEBP")));
    QVERIFY(backend.isAudio(QStringLiteral("soundtrack.FLAC")));
    QVERIFY(backend.isPlaylist(QStringLiteral("queue.m3u8")));
    QVERIFY(!backend.isImage(QStringLiteral("movie.mkv")));
}

void LocalFilesBackendTest::enforcesPathBoundariesButAllowsRootSymlinks()
{
    QTemporaryDir app;
    QTemporaryDir data;
    QTemporaryDir media;
    QTemporaryDir external;
    QVERIFY(app.isValid() && data.isValid() && media.isValid() && external.isValid());

    QFile externalMovie(external.filePath(QStringLiteral("linked.mp4")));
    QVERIFY(externalMovie.open(QIODevice::WriteOnly));
    externalMovie.close();
    QVERIFY(QFile::link(external.path(), media.filePath(QStringLiteral("linked"))));

    LocalFilesBackend backend(app.path(), data.path());
    backend.setMediaRoot(media.path());
    const QVariantList linked = backend.getItems(media.filePath(QStringLiteral("linked")));
    QCOMPARE(linked.size(), 1);
    QCOMPARE(linked.first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("linked.mp4"));

    QTemporaryDir sibling;
    QVERIFY(sibling.isValid());
    QCOMPARE(backend.getItems(sibling.path()).size(), 0);
}

void LocalFilesBackendTest::detectsRelativeImagesInPlaylists()
{
    QTemporaryDir app;
    QTemporaryDir data;
    QTemporaryDir media;
    QVERIFY(app.isValid() && data.isValid() && media.isValid());

    QFile image(media.filePath(QStringLiteral("still.png")));
    QVERIFY(image.open(QIODevice::WriteOnly));
    image.close();
    QFile playlist(media.filePath(QStringLiteral("show.m3u")));
    QVERIFY(playlist.open(QIODevice::WriteOnly | QIODevice::Text));
    playlist.write("#EXTM3U\nstill.png\n");
    playlist.close();

    LocalFilesBackend backend(app.path(), data.path());
    backend.setMediaRoot(media.path());
    QVERIFY(backend.playlistContainsImages(playlist.fileName()));
}

void LocalFilesBackendTest::persistsDuplicateQueuesAndExpandsLocalPlaylists()
{
    QTemporaryDir app;
    QTemporaryDir data;
    QTemporaryDir media;
    QTemporaryDir outside;
    QVERIFY(app.isValid() && data.isValid() && media.isValid() && outside.isValid());
    QVERIFY(writeLocalConfig(data.path(), media.path()));

    const QString first = media.filePath(QStringLiteral("first.mp4"));
    const QString second = media.filePath(QStringLiteral("second.png"));
    const QString audio = media.filePath(QStringLiteral("bed.flac"));
    const QString outsideFile = outside.filePath(QStringLiteral("outside.mp4"));
    QVERIFY(writeFile(first));
    QVERIFY(writeFile(second));
    QVERIFY(writeFile(audio));
    QVERIFY(writeFile(outsideFile));

    const QString nestedPath = media.filePath(QStringLiteral("nested.m3u8"));
    QVERIFY(writeFile(nestedPath, QByteArrayLiteral("#EXTM3U\nsecond.png\n")));
    const QString playlistPath = media.filePath(QStringLiteral("queue.m3u"));
    const QByteArray playlist = QByteArrayLiteral("#EXTM3U\nfirst.mp4\nnested.m3u8\n")
        + QFile::encodeName(outsideFile) + QByteArrayLiteral("\nhttps://example.com/remote.mp4\n");
    QVERIFY(writeFile(playlistPath, playlist));

    {
        LocalFilesBackend backend(app.path(), data.path());
        QCOMPARE(backend.mediaRoot(), media.path());
        QCOMPARE(backend.getItems(media.path()).size(), 5); // 3 media files + 2 playlists
        QCOMPARE(backend.enqueue(QStringLiteral("media"),
                                 {{QStringLiteral("filePath"), playlistPath}}), 2);
        QCOMPARE(backend.enqueue(QStringLiteral("media"),
                                 {{QStringLiteral("filePath"), first}}), 1);
        QCOMPARE(backend.enqueue(QStringLiteral("soundtrack"),
                                 {{QStringLiteral("filePath"), audio}}), 1);
        QCOMPARE(backend.enqueue(QStringLiteral("soundtrack"),
                                 {{QStringLiteral("filePath"), first}}), 0);
        const QVariantList queue = backend.getQueue(QStringLiteral("media"));
        QCOMPARE(queue.size(), 3);
        QCOMPARE(queue.at(0).toMap().value(QStringLiteral("filePath")).toString(), first);
        QCOMPARE(queue.at(2).toMap().value(QStringLiteral("filePath")).toString(), first);
        QVERIFY(queue.at(0).toMap().value(QStringLiteral("entryId")) !=
                queue.at(2).toMap().value(QStringLiteral("entryId")));
    }

    LocalFilesBackend reloaded(app.path(), data.path());
    QCOMPARE(reloaded.getQueue(QStringLiteral("media")).size(), 3);
    QCOMPARE(reloaded.getQueue(QStringLiteral("soundtrack")).size(), 1);
    const QFileInfo queueFile(data.filePath(QStringLiteral("local_queue.json")));
    QVERIFY(queueFile.permission(QFileDevice::ReadOwner));
    QVERIFY(queueFile.permission(QFileDevice::WriteOwner));
    QVERIFY(!queueFile.permission(QFileDevice::ReadGroup));
    QVERIFY(!queueFile.permission(QFileDevice::ReadOther));
}

void LocalFilesBackendTest::loadsLegacySchemaOneLocalQueues()
{
    QTemporaryDir app;
    QTemporaryDir data;
    QTemporaryDir media;
    QVERIFY(app.isValid() && data.isValid() && media.isValid());
    QVERIFY(writeLocalConfig(data.path(), media.path()));
    const QString video = media.filePath(QStringLiteral("legacy.mp4"));
    const QString audio = media.filePath(QStringLiteral("legacy.flac"));
    QVERIFY(writeFile(video));
    QVERIFY(writeFile(audio));

    const QJsonObject legacyEntry{
        {QStringLiteral("entryId"),
         QStringLiteral("12345678-1234-4234-8234-123456789abc")},
        {QStringLiteral("filePath"), video},
        {QStringLiteral("displayTitle"), QStringLiteral("legacy.mp4")},
        {QStringLiteral("status"), QStringLiteral("queued")}
    };
    const QJsonObject legacyState{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("media"), QJsonArray{legacyEntry}},
        {QStringLiteral("soundtrack"), QJsonArray{}}
    };
    QVERIFY(writeFile(data.filePath(QStringLiteral("local_queue.json")),
                      QJsonDocument(legacyState).toJson(QJsonDocument::Compact)));

    LocalFilesBackend backend(app.path(), data.path());
    QCOMPARE(backend.getQueue(QStringLiteral("media")).size(), 1);
    QCOMPARE(backend.enqueue(QStringLiteral("soundtrack"),
                             {{QStringLiteral("filePath"), audio}}), 1);

    QFile stateFile(data.filePath(QStringLiteral("local_queue.json")));
    QVERIFY(stateFile.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(stateFile.readAll()).object()
                 .value(QStringLiteral("schemaVersion")).toInt(), 2);
}

void LocalFilesBackendTest::retainsFailedEntriesAndPreparesSelectedPlayback()
{
    QTemporaryDir app;
    QTemporaryDir data;
    QTemporaryDir media;
    QVERIFY(app.isValid() && data.isValid() && media.isValid());
    QVERIFY(writeLocalConfig(data.path(), media.path()));
    const QString first = media.filePath(QStringLiteral("first.mp4"));
    const QString second = media.filePath(QStringLiteral("second.mp4"));
    QVERIFY(writeFile(first));
    QVERIFY(writeFile(second));

    LocalFilesBackend backend(app.path(), data.path());
    QCOMPARE(backend.enqueue(QStringLiteral("media"),
                             {{QStringLiteral("filePath"), first}}), 1);
    QCOMPARE(backend.enqueue(QStringLiteral("media"),
                             {{QStringLiteral("filePath"), second}}), 1);
    const QVariantList queue = backend.getQueue(QStringLiteral("media"));
    const QString selectedId = queue.at(1).toMap().value(QStringLiteral("entryId")).toString();
    QVERIFY(backend.failQueueEntry(QStringLiteral("media"), selectedId,
                                   QStringLiteral("decoder failed\nretry")));

    const QVariantMap plan = backend.preparePlayback(selectedId, true);
    QVERIFY(!plan.value(QStringLiteral("playlistPath")).toString().isEmpty());
    QCOMPARE(plan.value(QStringLiteral("startIndex")).toInt(), 0);
    const QVariantList playbackEntries = plan.value(QStringLiteral("entries")).toList();
    QCOMPARE(playbackEntries.first().toMap().value(QStringLiteral("entryId")).toString(),
             selectedId);

    LocalFilesBackend reloaded(app.path(), data.path());
    const QVariantList persisted = reloaded.getQueue(QStringLiteral("media"));
    QCOMPARE(persisted.size(), 2);
    QCOMPARE(persisted.at(1).toMap().value(QStringLiteral("status")).toString(),
             QStringLiteral("failed"));
    QVERIFY(reloaded.resetQueueEntry(QStringLiteral("media"), selectedId));
    QCOMPARE(reloaded.getQueue(QStringLiteral("media")).at(1).toMap()
                 .value(QStringLiteral("status")).toString(), QStringLiteral("queued"));
}

void LocalFilesBackendTest::mediaQueuePlanMutesWheneverSoundtrackIsNonEmpty()
{
    QTemporaryDir app;
    QTemporaryDir data;
    QTemporaryDir media;
    QVERIFY(app.isValid() && data.isValid() && media.isValid());
    QVERIFY(writeLocalConfig(data.path(), media.path()));
    const QString video = media.filePath(QStringLiteral("video.mp4"));
    const QString audio = media.filePath(QStringLiteral("audio.flac"));
    QVERIFY(writeFile(video));
    QVERIFY(writeFile(audio));

    LocalFilesBackend backend(app.path(), data.path());
    QCOMPARE(backend.enqueue(QStringLiteral("media"),
                             {{QStringLiteral("filePath"), video}}), 1);
    QVERIFY(!backend.preparePlayback(QString{}, false)
                 .value(QStringLiteral("muteMainAudio")).toBool());

    QCOMPARE(backend.enqueue(QStringLiteral("soundtrack"),
                             {{QStringLiteral("filePath"), audio}}), 1);
    QVERIFY(backend.preparePlayback(QString{}, false)
                .value(QStringLiteral("muteMainAudio")).toBool());

    backend.clearQueue(QStringLiteral("soundtrack"));
    QVERIFY(!backend.preparePlayback(QString{}, false)
                 .value(QStringLiteral("muteMainAudio")).toBool());
}

void LocalFilesBackendTest::importsYouTubePlaylistInOrderAndPersistsEntries()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString appRoot = root.filePath(QStringLiteral("app"));
    const QString dataRoot = root.filePath(QStringLiteral("data"));
    const QString mediaRoot = root.filePath(QStringLiteral("media"));
    const QString binDirectory = QDir(appRoot).filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkpath(binDirectory));
    QVERIFY(QDir().mkpath(dataRoot));
    QVERIFY(QDir().mkpath(mediaRoot));
    QVERIFY(writeLocalConfig(dataRoot, mediaRoot));

    const QString markerPath = root.filePath(QStringLiteral("yt-dlp-arguments.txt"));
    const QByteArray fakeYtDlp = QByteArrayLiteral(
        "#!/bin/sh\nprintf '%s\\n' \"$@\" > \"") + QFile::encodeName(markerPath) +
        QByteArrayLiteral(
            "\"\nprintf '%s\\n' "
            "'{\"id\":\"abcDEF123_-\",\"title\":\"First song\"}' "
            "'{\"id\":\"ZYXwvu987_0\",\"title\":\"Second song\"}' "
            "'{\"id\":\"oneTWO34567\",\"title\":\"Third song\"}'\n");
    QVERIFY(writeExecutable(QDir(binDirectory).filePath(QStringLiteral("yt-dlp")),
                            fakeYtDlp));
    QVERIFY(writeExecutable(QDir(binDirectory).filePath(QStringLiteral("deno")),
                            QByteArrayLiteral("#!/bin/sh\nexit 0\n")));

    {
        LocalFilesBackend backend(appRoot, dataRoot);
        QSignalSpy finishedSpy(&backend,
            &LocalFilesBackend::youtubePlaylistImportFinished);
        QSignalSpy failedSpy(&backend,
            &LocalFilesBackend::youtubePlaylistImportFailed);
        QVERIFY(backend.importYouTubePlaylist(QStringLiteral(
            "https://music.youtube.com/playlist?si=ignored&list=PLtest_playlist_123")));
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.size(), 1, 3000);
        QCOMPARE(failedSpy.size(), 0);
        QCOMPARE(finishedSpy.first().first().toInt(), 3);

        const QVariantList queue = backend.getQueue(QStringLiteral("soundtrack"));
        QCOMPARE(queue.size(), 3);
        QCOMPARE(queue.at(0).toMap().value(QStringLiteral("displayTitle")).toString(),
                 QStringLiteral("First song"));
        QCOMPARE(queue.at(1).toMap().value(QStringLiteral("videoId")).toString(),
                 QStringLiteral("ZYXwvu987_0"));
        QCOMPARE(queue.at(2).toMap().value(QStringLiteral("source")).toString(),
                 QStringLiteral("youtube"));
        QCOMPARE(backend.soundtrackPaths(false), QStringList({
            QStringLiteral("https://www.youtube.com/watch?v=abcDEF123_-"),
            QStringLiteral("https://www.youtube.com/watch?v=ZYXwvu987_0"),
            QStringLiteral("https://www.youtube.com/watch?v=oneTWO34567")
        }));

        QVERIFY(backend.moveQueueEntry(QStringLiteral("soundtrack"), 2, 0));
        QCOMPARE(backend.soundtrackPaths(false).first(),
                 QStringLiteral("https://www.youtube.com/watch?v=oneTWO34567"));
    }

    LocalFilesBackend reloaded(appRoot, dataRoot);
    const QVariantList persisted = reloaded.getQueue(QStringLiteral("soundtrack"));
    QCOMPARE(persisted.size(), 3);
    QCOMPARE(persisted.first().toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Third song"));
    QFile queueFile(QDir(dataRoot).filePath(QStringLiteral("local_queue.json")));
    QVERIFY(queueFile.open(QIODevice::ReadOnly));
    const QByteArray persistedJson = queueFile.readAll();
    QVERIFY(!persistedJson.contains("music.youtube.com"));
    QVERIFY(!persistedJson.contains("PLtest_playlist_123"));

    QFile marker(markerPath);
    QVERIFY(marker.open(QIODevice::ReadOnly | QIODevice::Text));
    const QStringList arguments = QString::fromUtf8(marker.readAll())
                                      .split('\n', Qt::SkipEmptyParts);
    QVERIFY(arguments.contains(QStringLiteral("--flat-playlist")));
    QVERIFY(arguments.contains(QStringLiteral("--lazy-playlist")));
    QVERIFY(arguments.contains(
        QStringLiteral("https://www.youtube.com/playlist?list=PLtest_playlist_123")));
    for (const QString &argument : arguments)
        QVERIFY(!argument.contains(QStringLiteral("si=ignored")));
}

void LocalFilesBackendTest::importsLiveYouTubePlaylist()
{
    if (qEnvironmentVariable("LOCAL_FILES_LIVE_TEST") != QLatin1String("1"))
        QSKIP("Set LOCAL_FILES_LIVE_TEST=1 to exercise a public YouTube playlist.");

    QTemporaryDir app;
    QTemporaryDir data;
    QTemporaryDir media;
    QVERIFY(app.isValid() && data.isValid() && media.isValid());
    QVERIFY(writeLocalConfig(data.path(), media.path()));
    LocalFilesBackend backend(app.path(), data.path());
    QSignalSpy finishedSpy(&backend,
        &LocalFilesBackend::youtubePlaylistImportFinished);
    QSignalSpy failedSpy(&backend,
        &LocalFilesBackend::youtubePlaylistImportFailed);
    QVERIFY(backend.importYouTubePlaylist(QStringLiteral(
        "https://www.youtube.com/playlist?list=PLt5yu3-wZAlSLRHmI1qNm0wjyVNWw1pCU")));
    QTRY_VERIFY_WITH_TIMEOUT(!finishedSpy.isEmpty() || !failedSpy.isEmpty(), 30000);
    QCOMPARE(failedSpy.size(), 0);
    QCOMPARE(finishedSpy.size(), 1);
    const QVariantList queue = backend.getQueue(QStringLiteral("soundtrack"));
    QCOMPARE(queue.size(), 1);
    QCOMPARE(queue.first().toMap().value(QStringLiteral("videoId")).toString(),
             QStringLiteral("aqz-KE-bpKQ"));
}

void LocalFilesBackendTest::rejectsNonYouTubePlaylistUrls()
{
    QTemporaryDir app;
    QTemporaryDir data;
    LocalFilesBackend backend(app.path(), data.path());
    QSignalSpy failedSpy(&backend, &LocalFilesBackend::youtubePlaylistImportFailed);
    QVERIFY(!backend.importYouTubePlaylist(
        QStringLiteral("https://example.com/playlist?list=PLtest_playlist_123")));
    QCOMPARE(failedSpy.size(), 1);
    QCOMPARE(backend.getQueue(QStringLiteral("soundtrack")).size(), 0);
}

void LocalFilesBackendTest::changingMediaRootPrunesOutOfRootQueueItems()
{
    QTemporaryDir app;
    QTemporaryDir data;
    QTemporaryDir firstRoot;
    QTemporaryDir secondRoot;
    QVERIFY(app.isValid() && data.isValid() && firstRoot.isValid() && secondRoot.isValid());
    QVERIFY(writeLocalConfig(data.path(), firstRoot.path()));
    const QString clip = firstRoot.filePath(QStringLiteral("clip.mp4"));
    QVERIFY(writeFile(clip));

    LocalFilesBackend backend(app.path(), data.path());
    QCOMPARE(backend.enqueue(QStringLiteral("media"),
                             {{QStringLiteral("filePath"), clip}}), 1);
    QSignalSpy queueSpy(&backend, &LocalFilesBackend::queueChanged);
    backend.setMediaRoot(secondRoot.path());
    QCOMPARE(backend.getQueue(QStringLiteral("media")).size(), 0);
    QVERIFY(queueSpy.size() >= 1);
}

void LocalFilesBackendTest::usesBundledMpvForSeparateAudio()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString appRoot = root.filePath(QStringLiteral("app"));
    const QString dataRoot = root.filePath(QStringLiteral("data"));
    const QString binDirectory = QDir(appRoot).filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkpath(binDirectory));
    QVERIFY(QDir().mkpath(dataRoot));
    QVERIFY(writeLocalConfig(dataRoot, root.path()));

    const QString markerPath = root.filePath(QStringLiteral("mpv-arguments.txt"));
    const QString fakeMpvPath = QDir(binDirectory).filePath(QStringLiteral("mpv"));
    const QByteArray script = QByteArrayLiteral("#!/bin/sh\nprintf '%s\\n' \"$@\" > \"")
        + QFile::encodeName(markerPath) + QByteArrayLiteral("\"\n");
    QVERIFY(writeFile(fakeMpvPath, script));
    QVERIFY(QFile::setPermissions(fakeMpvPath, QFileDevice::ReadOwner |
                                  QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    const QByteArray originalPath = qgetenv("PATH");
    qputenv("PATH", QByteArray());
    const QString audioPath = root.filePath(QStringLiteral("separate audio.flac"));
    QVERIFY(writeFile(audioPath));
    LocalFilesBackend backend(appRoot, dataRoot);
    backend.startAudio({root.filePath(QStringLiteral("outside.mp3")), audioPath,
                        root.filePath(QStringLiteral("not-audio.txt"))});
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(markerPath), 3000);
    backend.stopAudio();
    if (originalPath.isNull())
        qunsetenv("PATH");
    else
        qputenv("PATH", originalPath);

    QFile marker(markerPath);
    QVERIFY(marker.open(QIODevice::ReadOnly | QIODevice::Text));
    const QStringList arguments = QString::fromUtf8(marker.readAll())
                                      .split('\n', Qt::SkipEmptyParts);
    QCOMPARE(arguments.value(0), audioPath);
    QVERIFY(arguments.contains(QStringLiteral("--no-video")));
    QVERIFY(arguments.contains(QStringLiteral("--loop-playlist=inf")));
    QVERIFY(arguments.contains(QStringLiteral("--no-terminal")));
    QVERIFY(arguments.contains(QStringLiteral("--really-quiet")));
}

void LocalFilesBackendTest::streamsYouTubeSoundtrackWithBundledHelpers()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString appRoot = root.filePath(QStringLiteral("app"));
    const QString dataRoot = root.filePath(QStringLiteral("data"));
    const QString binDirectory = QDir(appRoot).filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkpath(binDirectory));
    QVERIFY(QDir().mkpath(dataRoot));
    QVERIFY(writeLocalConfig(dataRoot, root.path()));

    const QString markerPath = root.filePath(QStringLiteral("mpv-youtube-arguments.txt"));
    const QByteArray fakeMpv = QByteArrayLiteral("#!/bin/sh\nprintf '%s\\n' \"$@\" > \"")
        + QFile::encodeName(markerPath) + QByteArrayLiteral("\"\n");
    const QString fakeMpvPath = QDir(binDirectory).filePath(QStringLiteral("mpv"));
    const QString fakeYtDlpPath = QDir(binDirectory).filePath(QStringLiteral("yt-dlp"));
    const QString fakeDenoPath = QDir(binDirectory).filePath(QStringLiteral("deno"));
    QVERIFY(writeExecutable(fakeMpvPath, fakeMpv));
    QVERIFY(writeExecutable(fakeYtDlpPath, QByteArrayLiteral("#!/bin/sh\nexit 0\n")));
    QVERIFY(writeExecutable(fakeDenoPath, QByteArrayLiteral("#!/bin/sh\nexit 0\n")));

    LocalFilesBackend backend(appRoot, dataRoot);
    const QString videoUrl = QStringLiteral(
        "https://www.youtube.com/watch?v=abcDEF123_-");
    backend.startAudio({videoUrl});
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(markerPath), 3000);
    backend.stopAudio();

    QFile marker(markerPath);
    QVERIFY(marker.open(QIODevice::ReadOnly | QIODevice::Text));
    const QStringList arguments = QString::fromUtf8(marker.readAll())
                                      .split('\n', Qt::SkipEmptyParts);
    QCOMPARE(arguments.first(), videoUrl);
    QVERIFY(arguments.contains(QStringLiteral("--no-video")));
    QVERIFY(arguments.contains(QStringLiteral("--loop-playlist=inf")));
    QVERIFY(std::any_of(arguments.cbegin(), arguments.cend(), [](const QString &argument) {
        return argument.startsWith(QStringLiteral("--input-ipc-server="));
    }));
    QVERIFY(arguments.contains(
        QStringLiteral("--script-opts-append=ytdl_hook-ytdl_path=") + fakeYtDlpPath));
    QVERIFY(arguments.contains(
        QStringLiteral("--ytdl-raw-options-append=js-runtimes=deno:") + fakeDenoPath));
    QVERIFY(arguments.contains(QStringLiteral("--ytdl-raw-options-append=no-cache-dir=")));
}

void LocalFilesBackendTest::reportsWhenSeparateAudioActuallyStarts()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString appRoot = root.filePath(QStringLiteral("app"));
    const QString dataRoot = root.filePath(QStringLiteral("data"));
    const QString binDirectory = QDir(appRoot).filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkpath(binDirectory));
    QVERIFY(QDir().mkpath(dataRoot));
    QVERIFY(writeLocalConfig(dataRoot, root.path()));

    const QByteArray fakeMpv = QByteArrayLiteral(
        "#!/bin/sh\n"
        "socket_path=''\n"
        "for argument in \"$@\"; do\n"
        "  case \"$argument\" in\n"
        "    --input-ipc-server=*) socket_path=${argument#*=} ;;\n"
        "  esac\n"
        "done\n"
        "exec /usr/bin/python3 - \"$socket_path\" <<'PY'\n"
        "import os, socket, sys, time\n"
        "socket_path = sys.argv[1]\n"
        "try:\n"
        "    os.unlink(socket_path)\n"
        "except FileNotFoundError:\n"
        "    pass\n"
        "server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)\n"
        "server.bind(socket_path)\n"
        "server.listen(1)\n"
        "connection, _ = server.accept()\n"
        "connection.sendall(b'{\"event\":\"file-loaded\"}\\n')\n"
        "time.sleep(0.2)\n"
        "connection.sendall(b'{\"event\":\"playback-restart\"}\\n')\n"
        "time.sleep(5)\n"
        "PY\n");
    QVERIFY(writeExecutable(QDir(binDirectory).filePath(QStringLiteral("mpv")), fakeMpv));

    const QString audioPath = root.filePath(QStringLiteral("soundtrack.mp3"));
    QVERIFY(writeFile(audioPath));
    LocalFilesBackend backend(appRoot, dataRoot);
    QSignalSpy readySpy(&backend, &LocalFilesBackend::audioPlaybackStarted);
    backend.startAudio({audioPath});
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.count(), 1, 5000);
    backend.stopAudio();
}

void LocalFilesBackendTest::reportsTaggedAndFallbackSoundtrackMetadata()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString appRoot = root.filePath(QStringLiteral("app"));
    const QString dataRoot = root.filePath(QStringLiteral("data"));
    const QString binDirectory = QDir(appRoot).filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkpath(binDirectory));
    QVERIFY(QDir().mkpath(dataRoot));
    QVERIFY(writeLocalConfig(dataRoot, root.path()));

    const QByteArray fakeMpv = QByteArrayLiteral(
        "#!/bin/sh\n"
        "socket_path=''\n"
        "for argument in \"$@\"; do\n"
        "  case \"$argument\" in\n"
        "    --input-ipc-server=*) socket_path=${argument#*=} ;;\n"
        "  esac\n"
        "done\n"
        "exec /usr/bin/python3 - \"$socket_path\" <<'PY'\n"
        "import os, socket, sys, time\n"
        "socket_path = sys.argv[1]\n"
        "try:\n"
        "    os.unlink(socket_path)\n"
        "except FileNotFoundError:\n"
        "    pass\n"
        "server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)\n"
        "server.bind(socket_path)\n"
        "server.listen(1)\n"
        "connection, _ = server.accept()\n"
        "events = [\n"
        " b'{\"event\":\"file-loaded\"}\\n',\n"
        " b'{\"event\":\"property-change\",\"name\":\"playlist-pos\",\"data\":0}\\n',\n"
        " b'{\"event\":\"property-change\",\"name\":\"metadata\",\"data\":{\"ARTIST\":\"Tagged Artist\",\"TITLE\":\"Tagged Song\"}}\\n',\n"
        " b'{\"event\":\"playback-restart\"}\\n',\n"
        " b'{\"event\":\"playback-restart\"}\\n',\n"
        " ]\n"
        "for event in events:\n"
        "    connection.sendall(event)\n"
        "time.sleep(0.35)\n"
        "events = [\n"
        " b'{\"event\":\"file-loaded\"}\\n',\n"
        " b'{\"event\":\"property-change\",\"name\":\"playlist-pos\",\"data\":1}\\n',\n"
        " b'{\"event\":\"property-change\",\"name\":\"metadata\",\"data\":{}}\\n',\n"
        " b'{\"event\":\"playback-restart\"}\\n',\n"
        " ]\n"
        "for event in events:\n"
        "    connection.sendall(event)\n"
        "time.sleep(0.35)\n"
        "events = [\n"
        " b'{\"event\":\"file-loaded\"}\\n',\n"
        " b'{\"event\":\"property-change\",\"name\":\"playlist-pos\",\"data\":2}\\n',\n"
        " b'{\"event\":\"property-change\",\"name\":\"metadata\",\"data\":{}}\\n',\n"
        " b'{\"event\":\"playback-restart\"}\\n',\n"
        " ]\n"
        "for event in events:\n"
        "    connection.sendall(event)\n"
        "time.sleep(5)\n"
        "PY\n");
    QVERIFY(writeExecutable(QDir(binDirectory).filePath(QStringLiteral("mpv")), fakeMpv));

    const QString taggedPath = root.filePath(QStringLiteral("tagged.mp3"));
    const QString parsedPath = root.filePath(QStringLiteral("Fallback Artist - Fallback Song.flac"));
    const QString genericPath = root.filePath(QStringLiteral("Ambient Bed.wav"));
    QVERIFY(writeFile(taggedPath));
    QVERIFY(writeFile(parsedPath));
    QVERIFY(writeFile(genericPath));

    LocalFilesBackend backend(appRoot, dataRoot);
    QSignalSpy trackSpy(&backend, &LocalFilesBackend::audioTrackStarted);
    backend.startAudio({taggedPath, parsedPath, genericPath});
    QTRY_COMPARE_WITH_TIMEOUT(trackSpy.count(), 3, 5000);

    const QVariantMap tagged = trackSpy.at(0).at(0).toMap();
    QCOMPARE(tagged.value(QStringLiteral("artist")).toString(), QStringLiteral("Tagged Artist"));
    QCOMPARE(tagged.value(QStringLiteral("song")).toString(), QStringLiteral("Tagged Song"));

    const QVariantMap parsed = trackSpy.at(1).at(0).toMap();
    QCOMPARE(parsed.value(QStringLiteral("artist")).toString(), QStringLiteral("Fallback Artist"));
    QCOMPARE(parsed.value(QStringLiteral("song")).toString(), QStringLiteral("Fallback Song"));

    const QVariantMap generic = trackSpy.at(2).at(0).toMap();
    QCOMPARE(generic.value(QStringLiteral("artist")).toString(), QStringLiteral("SOUNDTRACK"));
    QCOMPARE(generic.value(QStringLiteral("song")).toString(), QStringLiteral("Ambient Bed"));
    QCOMPARE(backend.currentSoundtrack(), generic);
    backend.stopAudio();
    QVERIFY(backend.currentSoundtrack().isEmpty());
}

QTEST_MAIN(LocalFilesBackendTest)
#include "LocalFilesBackendTest.moc"
