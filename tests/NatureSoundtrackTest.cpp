#include "modules/nature/NatureSoundtrack.h"
#include "AppCore.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QJsonObject recording()
{
    return {{"id", "538001"}, {"source", "freesound"}, {"title", "Birds in a forest"},
        {"uploader", "katjajansen1997"}, {"previewUrl", "https://cdn.freesound.org/previews/538/538001_6746400-hq.mp3"},
        {"durationSec", 131.843}, {"license", "cc0"},
        {"licenseUrl", "https://creativecommons.org/publicdomain/zero/1.0/"},
        {"lat", 40.123}, {"lng", -73.555}};
}
QByteArray payload(const QJsonArray &tracks)
{
    return QJsonDocument(QJsonObject{{"version", "v1"}, {"source", "freesound"},
                                    {"tracks", tracks}}).toJson(QJsonDocument::Compact);
}
}

class NatureSoundtrackTest final : public QObject {
    Q_OBJECT
private slots:
    void validatesEveryRecording();
    void cacheKeepsOnlyValidatedMetadata();
    void fetchIsAnonymousAndRejectsRedirects();
    void cancelsWithoutStartingAudio();
    void refreshKeepsStaleCacheAndBoundsTransfer();
    void sessionStartIsIdempotentAndReentryWorks();
    void persistedBooleanToggleDisablesRequests();
    void liveCatalog();
};

void NatureSoundtrackTest::validatesEveryRecording()
{
    const auto valid = recording();
    const auto parsed = NatureSoundtrack::parseCatalog(payload({valid, valid}));
    QCOMPARE(parsed.size(), 1);
    QVERIFY(!parsed[0].toMap().contains("lat"));
    QVERIFY(parsed[0].toMap().value("sourceUrl").toString().startsWith("https://freesound.org/people/"));
    for (const auto &change : QList<QPair<QString, QJsonValue>>{
        {"license", "cc-by"}, {"license", ""}, {"licenseUrl", "https://example.com/"},
        {"source", "other"}, {"id", "538002"}, {"uploader", "../other"},
        {"durationSec", 0.9}, {"durationSec", 601}, {"durationSec", "120"},
        {"previewUrl", "http://cdn.freesound.org/previews/538/538001_6746400-hq.mp3"},
        {"previewUrl", "https://cdn.freesound.org.evil.test/previews/538/538001_6746400-hq.mp3"},
        {"previewUrl", "https://cdn.freesound.org/previews/538/538001_6746400-hq.mp3?token=abc"},
        {"previewUrl", "https://cdn.freesound.org/previews/999/538001_6746400-hq.mp3"},
        {"previewUrl", "https://user@cdn.freesound.org/previews/538/538001_6746400-hq.mp3"},
        {"previewUrl", "file:///tmp/music.mp3"}}) {
        auto row = valid;
        row[change.first] = change.second;
        QVERIFY2(NatureSoundtrack::parseCatalog(payload({row})).isEmpty(), qPrintable(change.first));
    }
    QVERIFY(NatureSoundtrack::parseCatalog("not json").isEmpty());
    QVERIFY(NatureSoundtrack::parseCatalog(QByteArray(8 * 1024 * 1024 + 1, ' ')).isEmpty());
    QJsonArray excess;
    for (int i = 0; i < 5001; ++i) excess.append(valid);
    QVERIFY(NatureSoundtrack::parseCatalog(payload(excess)).isEmpty());
}

void NatureSoundtrackTest::cacheKeepsOnlyValidatedMetadata()
{
    QTemporaryDir root;
    NatureSoundtrack sound(root.path(), root.path(), nullptr);
    sound.m_tracks = NatureSoundtrack::parseCatalog(payload({recording()}));
    sound.m_fetchedAt = QDateTime::currentDateTimeUtc().addDays(-2);
    sound.writeCache();
    QFile file(root.filePath("nature_sounds.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto contents = file.readAll();
    QVERIFY(!contents.contains("\"lat\""));
    QVERIFY(!(file.permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther)));
    NatureSoundtrack restored(root.path(), root.path(), nullptr);
    QVERIFY(restored.readCache());
    QCOMPARE(restored.m_tracks.size(), 1);
    QCOMPARE(restored.m_fetchedAt.toSecsSinceEpoch(), sound.m_fetchedAt.toSecsSinceEpoch());
    file.close();
    QVERIFY(file.open(QIODevice::WriteOnly));
    QByteArray poisoned = contents;
    poisoned.replace("cdn.freesound.org", "evil.example.com");
    file.write(poisoned);
    file.close();
    QVERIFY(!restored.readCache());
}

void NatureSoundtrackTest::fetchIsAnonymousAndRejectsRedirects()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QByteArray request;
    int requests = 0;
    connect(&server, &QTcpServer::newConnection, &server, [&] {
        auto socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
            request += socket->readAll();
            if (!request.contains("\r\n\r\n")) return;
            ++requests;
            socket->write("HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:1/forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    });
    QTemporaryDir root;
    NatureSoundtrack sound(root.path(), root.path(), nullptr);
    sound.m_endpoint = QUrl(QString("http://127.0.0.1:%1/manifest").arg(server.serverPort()));
    QSignalSpy status(&sound, &NatureSoundtrack::statusChanged);
    sound.start();
    QTRY_COMPARE_WITH_TIMEOUT(status.count(), 1, 3000);
    QCOMPARE(requests, 1);
    QVERIFY(!request.toLower().contains("authorization:"));
    QVERIFY(!request.toLower().contains("cookie:"));
    QVERIFY(!sound.active());
    QVERIFY(sound.m_retryAfter > QDateTime::currentDateTimeUtc());
    sound.stop();
}

void NatureSoundtrackTest::cancelsWithoutStartingAudio()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTemporaryDir root;
    NatureSoundtrack sound(root.path(), root.path(), nullptr);
    sound.m_endpoint = QUrl(QString("http://127.0.0.1:%1/manifest").arg(server.serverPort()));
    sound.start();
    QVERIFY(sound.m_reply);
    sound.stop();
    QVERIFY(!sound.m_reply);
    QVERIFY(!sound.m_wanted);
    QVERIFY(!sound.active());
}

void NatureSoundtrackTest::liveCatalog()
{
    if (!qEnvironmentVariableIsSet("NATURE_AUDIO_LIVE_TEST"))
        QSKIP("Set NATURE_AUDIO_LIVE_TEST=1 for the real Earth Garden catalog");
    QTemporaryDir root;
    NatureSoundtrack sound(QStringLiteral(TEST_SOURCE_ROOT), root.path(), nullptr);
    sound.prepare();
    QTRY_VERIFY_WITH_TIMEOUT(!sound.m_tracks.isEmpty(), 20000);
    qInfo("Accepted %lld CC0 field recordings", static_cast<long long>(sound.m_tracks.size()));
}

void NatureSoundtrackTest::persistedBooleanToggleDisablesRequests()
{
    QTemporaryDir root;
    AppCore core(root.path(), root.path());
    NatureSoundtrack sound(root.path(), root.path(), &core);
    QVERIFY(sound.enabled()); // Unset uses the manifest's ON default.
    const QString module = QStringLiteral("com.owlswitch.nature");
    for (const QVariant &disabled : {QVariant(false), QVariant("OFF"), QVariant("false"), QVariant("0")}) {
        core.save_setting(module, "ambient_audio", disabled);
        QVERIFY(!sound.enabled());
        sound.start();
        QVERIFY(!sound.m_reply);
        QVERIFY(!sound.active());
        QVERIFY(sound.requested()); // Remote/idle ownership still belongs to Nature.
        sound.stop();
    }
    core.save_setting(module, "ambient_audio", true);
    QVERIFY(sound.enabled());
    core.save_setting(module, "ambient_audio", false);
    AppCore reloaded(root.path(), root.path());
    NatureSoundtrack restored(root.path(), root.path(), &reloaded);
    QVERIFY(!restored.enabled());
}

void NatureSoundtrackTest::refreshKeepsStaleCacheAndBoundsTransfer()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    int requests = 0;
    QByteArray response = payload({recording()});
    connect(&server, &QTcpServer::newConnection, &server, [&] {
        auto socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
            socket->readAll();
            if (socket->property("replied").toBool()) return;
            socket->setProperty("replied", true);
            ++requests;
            socket->write("HTTP/1.1 200 OK\r\nContent-Length: " +
                QByteArray::number(response.size()) + "\r\nConnection: close\r\n\r\n");
            socket->write(response);
            socket->disconnectFromHost();
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    });
    QTemporaryDir root;
    NatureSoundtrack sound(root.path(), root.path(), nullptr);
    sound.m_endpoint = QUrl(QString("http://127.0.0.1:%1/manifest").arg(server.serverPort()));
    sound.prepare();
    sound.prepare();
    QTRY_VERIFY_WITH_TIMEOUT(!sound.m_reply, 3000);
    QCOMPARE(requests, 1);
    QCOMPARE(sound.m_tracks.size(), 1);
    QVERIFY(!sound.active()); // Preparing a catalog never starts playback.
    sound.prepare();
    QCOMPARE(requests, 1); // A fresh cache needs no second fetch.
    sound.m_fetchedAt = QDateTime::currentDateTimeUtc().addDays(-2);
    sound.writeCache();
    const auto staleDate = sound.m_fetchedAt;
    response = QByteArray(8 * 1024 * 1024 + 1, ' ');
    sound.prepare();
    QTRY_VERIFY_WITH_TIMEOUT(!sound.m_reply, 5000);
    QCOMPARE(requests, 2);
    QCOMPARE(sound.m_tracks.size(), 1);
    QCOMPARE(sound.m_fetchedAt, staleDate);
    QVERIFY(sound.m_payload.isEmpty());
    QVERIFY(sound.m_retryAfter > QDateTime::currentDateTimeUtc());
    sound.prepare();
    QCOMPARE(requests, 2); // Backoff also applies to manual repeated prepare.
    NatureSoundtrack restored(root.path(), root.path(), nullptr);
    QVERIFY(restored.readCache());
    QCOMPARE(restored.m_fetchedAt.toSecsSinceEpoch(), staleDate.toSecsSinceEpoch());
}

void NatureSoundtrackTest::sessionStartIsIdempotentAndReentryWorks()
{
    QTemporaryDir root;
    QVERIFY(QDir().mkpath(root.filePath("bin")));
    QFile fixture(QStringLiteral(TEST_SOURCE_ROOT "/tests/fixtures/fake-mpv-audio.py"));
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    QFile helper(root.filePath("bin/mpv"));
    QVERIFY(helper.open(QIODevice::WriteOnly));
    helper.write(fixture.readAll());
    helper.close();
    QVERIFY(helper.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    NatureSoundtrack sound(root.path(), root.path(), nullptr);
    sound.m_tracks = NatureSoundtrack::parseCatalog(payload({recording()}));
    sound.m_fetchedAt = QDateTime::currentDateTimeUtc();
    QSignalSpy changes(&sound, &NatureSoundtrack::currentSoundChanged);
    sound.start();
    QTRY_VERIFY_WITH_TIMEOUT(!sound.sourceUrl().isEmpty(), 2000);
    sound.setPaused(true);
    sound.start();
    sound.prepare();
    QTest::qWait(150);
    QCOMPARE(changes.count(), 1);
    sound.stop();
    sound.start(); // Reenter during the 200 ms stop fade.
    QTRY_VERIFY_WITH_TIMEOUT(!sound.sourceUrl().isEmpty(), 2000);
    QVERIFY(sound.active());
    QCOMPARE(changes.count(), 3);
    sound.stop();
    QTRY_VERIFY_WITH_TIMEOUT(!sound.active(), 1000);
    QVERIFY(!sound.requested());
}

QTEST_GUILESS_MAIN(NatureSoundtrackTest)
#include "NatureSoundtrackTest.moc"
