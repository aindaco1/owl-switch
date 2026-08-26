#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>

#include <cstring>
#include <utility>

#include "update/UpdateManager.h"

class StubNetworkReply final : public QNetworkReply {
public:
    StubNetworkReply(const QNetworkRequest &request, const QByteArray &body,
                     QObject *parent)
        : QNetworkReply(parent), m_body(body) {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] {
            emit readyRead();
            emit finished();
        });
    }

    void abort() override {}
    qint64 bytesAvailable() const override {
        return (static_cast<qint64>(m_body.size()) - m_offset) +
               QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override {
        const qint64 remaining = static_cast<qint64>(m_body.size()) - m_offset;
        if (remaining <= 0)
            return -1;
        const qint64 count = qMin(maxSize, remaining);
        memcpy(data, m_body.constData() + m_offset, static_cast<size_t>(count));
        m_offset += count;
        return count;
    }

private:
    QByteArray m_body;
    qint64 m_offset = 0;
};

class StubNetworkAccessManager final : public QNetworkAccessManager {
public:
    explicit StubNetworkAccessManager(QByteArray response)
        : m_response(std::move(response)) {}

    int requestCount = 0;

protected:
    QNetworkReply *createRequest(Operation operation,
                                 const QNetworkRequest &request,
                                 QIODevice *outgoingData) override {
        Q_UNUSED(operation)
        Q_UNUSED(outgoingData)
        ++requestCount;
        return new StubNetworkReply(request, m_response, this);
    }

private:
    QByteArray m_response;
};

QByteArray releaseResponse(const QString &version) {
    const QJsonObject asset{
        {QStringLiteral("name"),
         QStringLiteral("240-mp-jellyfin-v%1-macOS-arm64.dmg").arg(version)},
        {QStringLiteral("browser_download_url"),
         QStringLiteral("https://github.com/aindaco1/240-mp-jellyfin/releases/download/"
                        "v%1/240-mp-jellyfin-v%1-macOS-arm64.dmg").arg(version)},
        {QStringLiteral("digest"), QStringLiteral("sha256:") + QString(64, 'a')},
        {QStringLiteral("size"), 1234}
    };
    const QJsonObject release{
        {QStringLiteral("tag_name"), QStringLiteral("v") + version},
        {QStringLiteral("body"), QStringLiteral("Release notes")},
        {QStringLiteral("assets"), QJsonArray{asset}}
    };
    return QJsonDocument(release).toJson(QJsonDocument::Compact);
}

class UpdateManagerTest final : public QObject {
    Q_OBJECT

private slots:
    void comparesVersions_data();
    void comparesVersions();
    void launchCheckSignalsForNewerRelease();
    void launchCheckStaysQuietWhenCurrent();
    void launchCheckStaysQuietWhenReleaseIsInvalid();
    void manualCheckDoesNotEmitLaunchSignal();
};

void UpdateManagerTest::comparesVersions_data() {
    QTest::addColumn<QString>("left");
    QTest::addColumn<QString>("right");
    QTest::addColumn<int>("expected");

    QTest::newRow("new patch") << QStringLiteral("1.1.1") << QStringLiteral("1.1.0") << 1;
    QTest::newRow("v prefix") << QStringLiteral("v1.1.0") << QStringLiteral("1.1") << 0;
    QTest::newRow("older minor") << QStringLiteral("1.0.9") << QStringLiteral("1.1.0") << -1;
    QTest::newRow("double digit") << QStringLiteral("1.10.0") << QStringLiteral("1.2.0") << 1;
    QTest::newRow("prerelease suffix") << QStringLiteral("1.1.0-rc1") << QStringLiteral("1.1.0") << 0;
}

void UpdateManagerTest::comparesVersions() {
    QFETCH(QString, left);
    QFETCH(QString, right);
    QFETCH(int, expected);
    QCOMPARE(UpdateManager::compareVersions(left, right), expected);
}

void UpdateManagerTest::launchCheckSignalsForNewerRelease() {
    QCoreApplication::setApplicationVersion(QStringLiteral("1.6.1"));
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    StubNetworkAccessManager network(releaseResponse(QStringLiteral("1.6.2")));
    UpdateManager manager(dataRoot.path(), &network, nullptr);
    QSignalSpy availableSpy(&manager, &UpdateManager::launchUpdateAvailable);

    manager.checkForUpdatesOnLaunch();

    QTRY_COMPARE(manager.state(), QStringLiteral("available"));
    QCOMPARE(network.requestCount, 1);
    QCOMPARE(availableSpy.count(), 1);
    QCOMPARE(availableSpy.first().first().toString(), QStringLiteral("1.6.2"));
}

void UpdateManagerTest::launchCheckStaysQuietWhenCurrent() {
    QCoreApplication::setApplicationVersion(QStringLiteral("1.6.1"));
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    StubNetworkAccessManager network(releaseResponse(QStringLiteral("1.6.1")));
    UpdateManager manager(dataRoot.path(), &network, nullptr);
    QSignalSpy availableSpy(&manager, &UpdateManager::launchUpdateAvailable);

    manager.checkForUpdatesOnLaunch();

    QTRY_COMPARE(manager.state(), QStringLiteral("upToDate"));
    QCOMPARE(network.requestCount, 1);
    QCOMPARE(availableSpy.count(), 0);
}

void UpdateManagerTest::launchCheckStaysQuietWhenReleaseIsInvalid() {
    QCoreApplication::setApplicationVersion(QStringLiteral("1.6.1"));
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    StubNetworkAccessManager network(QByteArrayLiteral("not-json"));
    UpdateManager manager(dataRoot.path(), &network, nullptr);
    QSignalSpy availableSpy(&manager, &UpdateManager::launchUpdateAvailable);

    manager.checkForUpdatesOnLaunch();

    QTRY_COMPARE(manager.state(), QStringLiteral("error"));
    QCOMPARE(network.requestCount, 1);
    QCOMPARE(availableSpy.count(), 0);
}

void UpdateManagerTest::manualCheckDoesNotEmitLaunchSignal() {
    QCoreApplication::setApplicationVersion(QStringLiteral("1.6.1"));
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    StubNetworkAccessManager network(releaseResponse(QStringLiteral("1.6.2")));
    UpdateManager manager(dataRoot.path(), &network, nullptr);
    QSignalSpy availableSpy(&manager, &UpdateManager::launchUpdateAvailable);

    manager.checkForUpdates();

    QTRY_COMPARE(manager.state(), QStringLiteral("available"));
    QCOMPARE(network.requestCount, 1);
    QCOMPARE(availableSpy.count(), 0);
}

QTEST_MAIN(UpdateManagerTest)
#include "UpdateManagerTest.moc"
