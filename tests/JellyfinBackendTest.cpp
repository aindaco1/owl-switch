#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrlQuery>

#include "modules/jellyfin/JellyfinBackend.h"

class FakeJellyfinServer final : public QTcpServer {
    Q_OBJECT

public:
    struct Request {
        QByteArray method;
        QByteArray target;
        QByteArray headers;
        QByteArray body;
    };

    explicit FakeJellyfinServer(QObject *parent = nullptr) : QTcpServer(parent) {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    QByteArray buffer = socket->property("requestBuffer").toByteArray();
                    buffer += socket->readAll();
                    socket->setProperty("requestBuffer", buffer);
                    const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
                    if (headerEnd < 0) return;

                    const QByteArray headers = buffer.left(headerEnd);
                    qint64 contentLength = 0;
                    const QList<QByteArray> lines = headers.split('\n');
                    for (const QByteArray &rawLine : lines) {
                        const QByteArray line = rawLine.trimmed();
                        if (line.toLower().startsWith("content-length:"))
                            contentLength = line.mid(line.indexOf(':') + 1).trimmed().toLongLong();
                    }
                    if (buffer.size() < headerEnd + 4 + contentLength) return;

                    const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
                    Request request{requestLine.value(0), requestLine.value(1), headers,
                                    buffer.mid(headerEnd + 4, contentLength)};
                    requests.push_back(request);
                    const QByteArray body = responseFor(request);
                    const QByteArray response =
                        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                        QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
                    socket->write(response);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    QByteArray playbackInfoResponse;
    QList<Request> requests;

    bool sawTargetPrefix(const QByteArray &prefix) const {
        for (const Request &request : requests) {
            if (request.target.startsWith(prefix)) return true;
        }
        return false;
    }

    Request requestWithTargetPrefix(const QByteArray &prefix) const {
        for (const Request &request : requests) {
            if (request.target.startsWith(prefix)) return request;
        }
        return {};
    }

private:
    QByteArray responseFor(const Request &request) const {
        if (request.target.startsWith("/Items/item/PlaybackInfo"))
            return playbackInfoResponse;
        return QByteArrayLiteral("{}");
    }
};

class JellyfinBackendTest final : public QObject {
    Q_OBJECT

private slots:
    void directPlaybackUsesPrivateHeadersAndReportsStart();
    void transcodePlaybackPreservesTracksAndAppliesQualityLimit();

private:
    static void writeAuth(const QString &root, quint16 port);
    static void writeQuality(const QString &root, const QString &quality);
};

void JellyfinBackendTest::writeAuth(const QString &root, quint16 port) {
    QFile file(QDir(root).filePath(QStringLiteral("jellyfin_auth.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QJsonObject auth{
        {"serverUrl", QStringLiteral("http://127.0.0.1:%1").arg(port)},
        {"accessToken", "test-token"}, {"userId", "user"}, {"deviceId", "device"}
    };
    file.write(QJsonDocument(auth).toJson(QJsonDocument::Compact));
}

void JellyfinBackendTest::writeQuality(const QString &root, const QString &quality) {
    QFile file(QDir(root).filePath(QStringLiteral("config.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QJsonObject config{{"modules", QJsonObject{
        {"com.owlswitch.jellyfin", QJsonObject{{"video_quality", quality}}}
    }}};
    file.write(QJsonDocument(config).toJson(QJsonDocument::Compact));
}

void JellyfinBackendTest::directPlaybackUsesPrivateHeadersAndReportsStart() {
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    FakeJellyfinServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    writeAuth(dataRoot.path(), server.serverPort());
    server.playbackInfoResponse = R"({
        "PlaySessionId":"session",
        "MediaSources":[{"Id":"source","SupportsDirectPlay":true,"SupportsDirectStream":true}]
    })";

    JellyfinBackend backend({}, dataRoot.path());
    QSignalSpy streamSpy(&backend, &JellyfinBackend::streamUrlReady);
    backend.request_playback(QStringLiteral("item"), QStringLiteral("source"), 2, -1,
                             false, 1200000);
    QVERIFY(streamSpy.wait(3000));
    const QList<QVariant> emission = streamSpy.takeFirst();
    const QUrl streamUrl(emission.at(0).toString());
    const QUrlQuery query(streamUrl);
    QCOMPARE(streamUrl.path(), QStringLiteral("/Videos/item/stream"));
    QCOMPARE(query.queryItemValue(QStringLiteral("static")), QStringLiteral("true"));
    QVERIFY(!streamUrl.toString().contains(QStringLiteral("api_key"), Qt::CaseInsensitive));
    QVERIFY(emission.at(1).toStringList().contains(QStringLiteral("X-Emby-Token:test-token")));

    QTRY_VERIFY_WITH_TIMEOUT(server.sawTargetPrefix("/Sessions/Playing"), 3000);
    const FakeJellyfinServer::Request playbackInfo =
        server.requestWithTargetPrefix("/Items/item/PlaybackInfo");
    QVERIFY(playbackInfo.headers.contains("Token=\"test-token\""));
    const QJsonObject playbackBody = QJsonDocument::fromJson(playbackInfo.body).object();
    QCOMPARE(playbackBody.value(QStringLiteral("EnableDirectPlay")).toBool(), true);
    QCOMPARE(playbackBody.value(QStringLiteral("AudioStreamIndex")).toInt(), 2);
    QCOMPARE(playbackBody.value(QStringLiteral("MaxStreamingBitrate")).toInt(), 1000000000);

    const QJsonObject startBody = QJsonDocument::fromJson(
        server.requestWithTargetPrefix("/Sessions/Playing").body).object();
    QCOMPARE(startBody.value(QStringLiteral("PlayMethod")).toString(), QStringLiteral("DirectPlay"));
    QCOMPARE(startBody.value(QStringLiteral("StartPositionTicks")).toDouble(), 1200000.0);
}

void JellyfinBackendTest::transcodePlaybackPreservesTracksAndAppliesQualityLimit() {
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    FakeJellyfinServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    writeAuth(dataRoot.path(), server.serverPort());
    writeQuality(dataRoot.path(), QStringLiteral("720p"));
    server.playbackInfoResponse = R"({
        "PlaySessionId":"session",
        "MediaSources":[{
            "Id":"source",
            "TranscodingUrl":"/Videos/item/master.m3u8?api_key=leak&MaxHeight=999&SubtitleStreamIndex=4&SubtitleMethod=Encode"
        }]
    })";

    JellyfinBackend backend({}, dataRoot.path());
    QSignalSpy streamSpy(&backend, &JellyfinBackend::streamUrlReady);
    backend.request_playback(QStringLiteral("item"), QStringLiteral("source"), 3, 4,
                             true, 4500000);
    QVERIFY(streamSpy.wait(3000));
    const QUrl streamUrl(streamSpy.takeFirst().at(0).toString());
    const QUrlQuery query(streamUrl);
    QVERIFY(!streamUrl.toString().contains(QStringLiteral("api_key"), Qt::CaseInsensitive));
    QCOMPARE(query.queryItemValue(QStringLiteral("SubtitleStreamIndex")), QStringLiteral("4"));
    QCOMPARE(query.queryItemValue(QStringLiteral("SubtitleMethod")), QStringLiteral("Encode"));
    QCOMPARE(query.allQueryItemValues(QStringLiteral("MaxHeight")),
             QStringList{QStringLiteral("720")});

    const QJsonObject playbackBody = QJsonDocument::fromJson(
        server.requestWithTargetPrefix("/Items/item/PlaybackInfo").body).object();
    QCOMPARE(playbackBody.value(QStringLiteral("EnableDirectPlay")).toBool(), false);
    QCOMPARE(playbackBody.value(QStringLiteral("AudioStreamIndex")).toInt(), 3);
    QCOMPARE(playbackBody.value(QStringLiteral("SubtitleStreamIndex")).toInt(), 4);
    QCOMPARE(playbackBody.value(QStringLiteral("MaxHeight")).toInt(), 720);
    QCOMPARE(playbackBody.value(QStringLiteral("MaxStreamingBitrate")).toInt(), 8000000);

    QTRY_VERIFY_WITH_TIMEOUT(server.sawTargetPrefix("/Sessions/Playing"), 3000);
    const QJsonObject startBody = QJsonDocument::fromJson(
        server.requestWithTargetPrefix("/Sessions/Playing").body).object();
    QCOMPARE(startBody.value(QStringLiteral("AudioStreamIndex")).toInt(), 3);
    QCOMPARE(startBody.value(QStringLiteral("SubtitleStreamIndex")).toInt(), 4);
    QCOMPARE(startBody.value(QStringLiteral("StartPositionTicks")).toDouble(), 4500000.0);
}

QTEST_GUILESS_MAIN(JellyfinBackendTest)
#include "JellyfinBackendTest.moc"
