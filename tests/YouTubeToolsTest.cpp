#include "tools/YouTubeJob.h"
#include "tools/YouTubePolicy.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {

bool writeExecutable(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text) ||
        file.write(contents) != contents.size()) {
        return false;
    }
    file.close();
    return file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                               QFileDevice::ExeOwner);
}

QString createHelperRoot(QTemporaryDir &root, const QByteArray &ytDlpScript)
{
    const QString appRoot = root.filePath(QStringLiteral("app"));
    const QString bin = QDir(appRoot).filePath(QStringLiteral("bin"));
    if (!QDir().mkpath(bin) ||
        !writeExecutable(QDir(bin).filePath(QStringLiteral("yt-dlp")), ytDlpScript) ||
        !writeExecutable(QDir(bin).filePath(QStringLiteral("deno")),
                         QByteArrayLiteral("#!/bin/sh\nexit 0\n")) ||
        !writeExecutable(QDir(bin).filePath(QStringLiteral("ffmpeg")),
                         QByteArrayLiteral("#!/bin/sh\nexit 0\n"))) {
        return {};
    }
    return appRoot;
}

}

class YouTubeToolsTest final : public QObject {
    Q_OBJECT

private slots:
    void canonicalizesIdentities();
    void profilesOwnPlaybackAndPrefetchPolicy();
    void jobStreamsOutputAndSanitizesErrors();
    void jobEnforcesOutputLimit();
};

void YouTubeToolsTest::canonicalizesIdentities()
{
    const QString id = QStringLiteral("abcDEF123_-");
    const QString videoUrl = QStringLiteral("https://www.youtube.com/watch?v=") + id;
    QVERIFY(YouTubePolicy::isValidVideoId(id));
    QVERIFY(!YouTubePolicy::isValidVideoId(QStringLiteral("too-short")));
    QCOMPARE(YouTubePolicy::canonicalVideoUrl(id), videoUrl);
    QVERIFY(YouTubePolicy::isCanonicalVideoUrl(videoUrl));
    QVERIFY(!YouTubePolicy::isCanonicalVideoUrl(videoUrl + QStringLiteral("&feature=x")));

    QCOMPARE(YouTubePolicy::canonicalPlaylistUrl(
                 QStringLiteral("music.youtube.com/watch?v=ignored&list=PL1234567890")),
             QStringLiteral("https://www.youtube.com/playlist?list=PL1234567890"));
    QVERIFY(YouTubePolicy::canonicalPlaylistUrl(
                QStringLiteral("https://example.com/playlist?list=PL1234567890")).isEmpty());
}

void YouTubeToolsTest::profilesOwnPlaybackAndPrefetchPolicy()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString appRoot = createHelperRoot(
        root, QByteArrayLiteral("#!/bin/sh\nexit 0\n"));
    QVERIFY(!appRoot.isEmpty());

    const QString videoFormat = YouTubePolicy::formatSelector(
        YouTubePolicy::MediaProfile::Video720p);
    const QStringList videoMpv = YouTubePolicy::mpvArguments(
        appRoot, YouTubePolicy::MediaProfile::Video720p);
    QVERIFY(videoMpv.contains(QStringLiteral("--ytdl-format=") + videoFormat));
    QVERIFY(videoMpv.contains(QStringLiteral("--ytdl-raw-options-append=check-formats=")));
    QVERIFY(!videoMpv.contains(QStringLiteral("--no-video")));

    const QStringList audioMpv = YouTubePolicy::mpvArguments(
        appRoot, YouTubePolicy::MediaProfile::AudioOnly);
    QVERIFY(audioMpv.contains(QStringLiteral("--ytdl-format=bestaudio/best")));
    QVERIFY(audioMpv.contains(QStringLiteral("--no-video")));

    const QStringList prefetch = YouTubePolicy::videoPrefetchArguments(
        appRoot, root.filePath(QStringLiteral("%(id)s.%(ext)s")),
        QStringLiteral("abcDEF123_-"));
    const int formatIndex = prefetch.indexOf(QStringLiteral("--format"));
    QVERIFY(formatIndex >= 0);
    QCOMPARE(prefetch.value(formatIndex + 1), videoFormat);

    const QStringList inventory = YouTubePolicy::playlistInventoryArguments(
        appRoot, {QStringLiteral("https://www.youtube.com/playlist?list=PL1234567890")}, 12);
    QVERIFY(inventory.contains(QStringLiteral("--no-cache-dir")));
    QVERIFY(inventory.contains(QStringLiteral("--flat-playlist")));
    QCOMPARE(inventory.value(inventory.indexOf(QStringLiteral("--playlist-end")) + 1),
             QStringLiteral("12"));
}

void YouTubeToolsTest::jobStreamsOutputAndSanitizesErrors()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString appRoot = createHelperRoot(
        root, QByteArrayLiteral(
            "#!/bin/sh\n"
            "printf '{\"id\":\"abcDEF123_-\"}\\n'\n"
            "printf 'failed https://www.youtube.com/watch?v=abcDEF123_- token=secret\\n' >&2\n"
            "exit 2\n"));
    QVERIFY(!appRoot.isEmpty());

    YouTubeJob job(appRoot);
    QSignalSpy outputSpy(&job, &YouTubeJob::outputReady);
    QSignalSpy completedSpy(&job, &YouTubeJob::completed);
    QVERIFY(job.start({QStringLiteral("--version")}));
    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, 3000);
    QVERIFY(!outputSpy.isEmpty());
    const QList<QVariant> completion = completedSpy.takeFirst();
    QCOMPARE(qvariant_cast<YouTubeJob::Failure>(completion.at(0)),
             YouTubeJob::Failure::ProcessFailed);
    const QString safeError = completion.at(2).toString();
    QVERIFY(safeError.contains(QStringLiteral("[URL]")));
    QVERIFY(safeError.contains(QStringLiteral("token=[REDACTED]"),
                               Qt::CaseInsensitive));
    QVERIFY(!safeError.contains(QStringLiteral("abcDEF123_-")));
    QVERIFY(!safeError.contains(QStringLiteral("secret")));
}

void YouTubeToolsTest::jobEnforcesOutputLimit()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString appRoot = createHelperRoot(
        root, QByteArrayLiteral("#!/bin/sh\nprintf '0123456789abcdef'\n"));
    QVERIFY(!appRoot.isEmpty());

    YouTubeJob job(appRoot);
    QSignalSpy completedSpy(&job, &YouTubeJob::completed);
    QVERIFY(job.start({QStringLiteral("--version")}, 3000, 4, 128));
    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, 3000);
    QCOMPARE(qvariant_cast<YouTubeJob::Failure>(completedSpy.takeFirst().at(0)),
             YouTubeJob::Failure::OutputLimit);
}

QTEST_MAIN(YouTubeToolsTest)
#include "YouTubeToolsTest.moc"
