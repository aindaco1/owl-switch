#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrlQuery>

#include "modules/nature/NatureBackend.h"

namespace {

QJsonObject validObservation(qint64 observationId = 42, qint64 photoId = 84)
{
    return QJsonObject{
        {"id", observationId},
        {"quality_grade", "research"},
        {"captive", false},
        {"spam", false},
        {"obscured", true},
        {"geoprivacy", "obscured"},
        {"place_guess", "Maties Sport Office, Stellenbosch, Western Cape, ZA"},
        {"place_ids", QJsonArray{9001, 9002, 9003}},
        {"observed_on", "2026-08-17"},
        {"species_guess", "Fallback name"},
        {"user", QJsonObject{{"login", "field-notes"}, {"name", "Field Notes"}}},
        {"taxon", QJsonObject{
            {"id", 1234},
            {"name", "Danaus plexippus"},
            {"preferred_common_name", "Monarch"},
            {"rank", "species"},
            {"iconic_taxon_name", "Insecta"}
        }},
        {"photos", QJsonArray{
            QJsonObject{
                {"id", photoId - 1},
                {"license_code", "cc-by"},
                {"url", "https://inaturalist-open-data.s3.amazonaws.com/photos/83/square.jpg"}
            },
            QJsonObject{
                {"id", photoId},
                {"license_code", "cc0"},
                {"url", QStringLiteral(
                    "https://inaturalist-open-data.s3.amazonaws.com/photos/%1/square.jpg?x=1#ignored")
                    .arg(photoId)},
                {"attribution", "(c) Field Notes, some rights reserved"},
                {"original_dimensions", QJsonObject{{"width", 2048}, {"height", 1365}}}
            }
        }}
    };
}

QByteArray responsePayload(const QJsonArray &results = QJsonArray{validObservation()})
{
    return QJsonDocument(QJsonObject{{"total_results", results.size()}, {"results", results}})
        .toJson(QJsonDocument::Compact);
}

QByteArray placesResponsePayload()
{
    const QJsonArray results{
        QJsonObject{{"id", 9001}, {"name", "South Africa"}, {"admin_level", 0}},
        QJsonObject{{"id", 9002}, {"name", "Western Cape"}, {"admin_level", 10}},
        QJsonObject{{"id", 9003}, {"name", "Stellenbosch"}, {"admin_level", 30}}
    };
    return QJsonDocument(QJsonObject{{"total_results", results.size()}, {"results", results}})
        .toJson(QJsonDocument::Compact);
}

class FakeNatureServer final : public QTcpServer {
    Q_OBJECT

public:
    explicit FakeNatureServer(QObject *parent = nullptr) : QTcpServer(parent)
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    QByteArray request = socket->property("natureRequest").toByteArray();
                    request += socket->readAll();
                    socket->setProperty("natureRequest", request);
                    const qsizetype headerEnd = request.indexOf("\r\n\r\n");
                    if (headerEnd < 0 || socket->property("natureAnswered").toBool())
                        return;
                    socket->setProperty("natureAnswered", true);
                    requests.append(request.left(headerEnd));
                    const bool isPlacesRequest = request.startsWith("GET /v1/places/");
                    const QByteArray responsePayload = isPlacesRequest ? placesPayload : payload;
                    const QByteArray response =
                        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                        QByteArray::number(responsePayload.size()) +
                        "\r\nConnection: close\r\n\r\n" + responsePayload;
                    socket->write(response);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    QByteArray payload = responsePayload();
    QByteArray placesPayload = placesResponsePayload();
    QList<QByteArray> requests;
};

} // namespace

class NatureBackendTest final : public QObject {
    Q_OBJECT

private slots:
    void requestUsesConservativePolicy();
    void mapsOnlyEligiblePhotos();
    void resolvesEnglishPlaces();
    void filtersIneligibleObservations();
    void cacheRoundTripRejectsTampering();
    void fetchIsAnonymousAndBounded();
    void fetchesLiveObservations();
};

void NatureBackendTest::requestUsesConservativePolicy()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    NatureBackend backend(dataRoot.path(), QUrl(QStringLiteral("https://example.test/v1/observations")));

    const QUrl request = backend.requestUrl();
    const QUrlQuery query(request);
    QCOMPARE(query.queryItemValue(QStringLiteral("photos")), QStringLiteral("true"));
    QCOMPARE(query.queryItemValue(QStringLiteral("quality_grade")), QStringLiteral("research"));
    QCOMPARE(query.queryItemValue(QStringLiteral("captive")), QStringLiteral("false"));
    QCOMPARE(query.queryItemValue(QStringLiteral("photo_license")),
             QStringLiteral("cc0"));
    QCOMPARE(query.queryItemValue(QStringLiteral("order_by")), QStringLiteral("created_at"));
    QCOMPARE(query.queryItemValue(QStringLiteral("order")), QStringLiteral("desc"));
    QCOMPARE(query.queryItemValue(QStringLiteral("per_page")), QStringLiteral("100"));
    QCOMPARE(query.queryItemValue(QStringLiteral("locale")), QStringLiteral("en"));

    const QUrl places = backend.placesRequestUrl(QList<qint64>{1, 2, 3});
    QCOMPARE(places.path(), QStringLiteral("/v1/places/1,2,3"));
    const QUrlQuery placesQuery(places);
    QCOMPARE(placesQuery.queryItemValue(QStringLiteral("admin_level")),
             QStringLiteral("0,10,30"));
    QCOMPARE(placesQuery.queryItemValue(QStringLiteral("locale")), QStringLiteral("en"));

    QCOMPARE(backend.largePhotoUrl(
                 QStringLiteral("https://inaturalist-open-data.s3.amazonaws.com/photos/1/square.JPG"),
                 QStringLiteral("cc0")).toString(),
             QStringLiteral("https://inaturalist-open-data.s3.amazonaws.com/photos/1/large.jpg"));
    QVERIFY(backend.largePhotoUrl(
                QStringLiteral("https://inaturalist-open-data.s3.amazonaws.com/photos/1/square.jpg"),
                QStringLiteral("cc-by")).isEmpty());
    QVERIFY(backend.largePhotoUrl(QStringLiteral("https://static.inaturalist.org/1/square.jpg"),
                                  QStringLiteral("cc0")).isEmpty());
    QVERIFY(backend.largePhotoUrl(
                QStringLiteral("http://inaturalist-open-data.s3.amazonaws.com/photos/1/square.jpg"),
                QStringLiteral("cc0")).isEmpty());

    const QVariantMap washington = backend.locationFromPlaceGuess(
        QStringLiteral("Washington, DC 20004, USA"));
    QCOMPARE(washington.value(QStringLiteral("city")).toString(), QStringLiteral("Washington"));
    QCOMPARE(washington.value(QStringLiteral("region")).toString(), QStringLiteral("DC"));
    QCOMPARE(washington.value(QStringLiteral("country")).toString(),
             QStringLiteral("United States"));
}

void NatureBackendTest::mapsOnlyEligiblePhotos()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    NatureBackend backend(dataRoot.path());
    QString error;
    const QVariantList observations = backend.observationsFromPayload(responsePayload(), &error);

    QCOMPARE(error, QString());
    QCOMPARE(observations.size(), 1);
    const QVariantMap item = observations.constFirst().toMap();
    QCOMPARE(item.value(QStringLiteral("id")).toLongLong(), 42);
    QCOMPARE(item.value(QStringLiteral("photoId")).toLongLong(), 84);
    QCOMPARE(item.value(QStringLiteral("url")).toString(),
             QStringLiteral("https://inaturalist-open-data.s3.amazonaws.com/photos/84/large.jpg?x=1"));
    QCOMPARE(item.value(QStringLiteral("title")).toString(), QStringLiteral("Monarch"));
    QCOMPARE(item.value(QStringLiteral("scientificName")).toString(),
             QStringLiteral("Danaus plexippus"));
    QCOMPARE(item.value(QStringLiteral("city")).toString(), QStringLiteral("Stellenbosch"));
    QCOMPARE(item.value(QStringLiteral("region")).toString(), QStringLiteral("Western Cape"));
    QCOMPARE(item.value(QStringLiteral("country")).toString(), QStringLiteral("South Africa"));
    QCOMPARE(item.value(QStringLiteral("licenseCode")).toString(), QStringLiteral("cc0"));
    QCOMPARE(item.value(QStringLiteral("observationUrl")).toString(),
             QStringLiteral("https://www.inaturalist.org/observations/42"));
    QCOMPARE(item.value(QStringLiteral("placeIds")).toList().size(), 3);
}

void NatureBackendTest::resolvesEnglishPlaces()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    NatureBackend backend(dataRoot.path());

    QJsonObject observation = validObservation();
    observation[QStringLiteral("place_guess")] =
        QStringLiteral("Ленинский р-н, Махачкала, Респ. Дагестан, Россия");
    observation[QStringLiteral("place_ids")] = QJsonArray{7161, 11799, 22001};
    QString error;
    const QVariantList observations = backend.observationsFromPayload(
        responsePayload(QJsonArray{observation}), &error);
    const QJsonArray places{
        QJsonObject{{"id", 7161}, {"name", "Russia"}, {"admin_level", 0}},
        QJsonObject{{"id", 11799}, {"name", "Dagestan"}, {"admin_level", 10}},
        QJsonObject{{"id", 22001}, {"name", "Makhachkala"}, {"admin_level", 30}}
    };
    const QByteArray payload = QJsonDocument(
        QJsonObject{{"total_results", places.size()}, {"results", places}})
        .toJson(QJsonDocument::Compact);

    const QVariantList resolved = backend.observationsWithEnglishPlaces(observations, payload);
    QCOMPARE(resolved.size(), 1);
    const QVariantMap item = resolved.constFirst().toMap();
    QCOMPARE(item.value(QStringLiteral("city")).toString(), QStringLiteral("Makhachkala"));
    QCOMPARE(item.value(QStringLiteral("region")).toString(), QStringLiteral("Dagestan"));
    QCOMPARE(item.value(QStringLiteral("country")).toString(), QStringLiteral("Russia"));

    const QJsonArray placesWithoutTown{
        QJsonObject{{"id", 7161}, {"name", "Russia"}, {"admin_level", 0}},
        QJsonObject{{"id", 11799}, {"name", "Dagestan"}, {"admin_level", 10}}
    };
    const QByteArray fallbackPayload = QJsonDocument(
        QJsonObject{{"total_results", placesWithoutTown.size()},
                    {"results", placesWithoutTown}})
        .toJson(QJsonDocument::Compact);
    const QVariantMap fallbackItem = backend.observationsWithEnglishPlaces(
        observations, fallbackPayload).constFirst().toMap();
    const QString fallbackCity = fallbackItem.value(QStringLiteral("city")).toString();
    QVERIFY(!fallbackCity.isEmpty());
    QVERIFY(fallbackCity != QStringLiteral("Махачкала"));
    for (const QChar character : fallbackCity) {
        if (character.isLetter())
            QCOMPARE(character.script(), QChar::Script_Latin);
    }
}

void NatureBackendTest::filtersIneligibleObservations()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    NatureBackend backend(dataRoot.path());
    QJsonArray results;

    QJsonObject captive = validObservation(1, 101);
    captive[QStringLiteral("captive")] = true;
    results.append(captive);
    QJsonObject lowQuality = validObservation(2, 102);
    lowQuality[QStringLiteral("quality_grade")] = QStringLiteral("needs_id");
    results.append(lowQuality);
    QJsonObject spam = validObservation(3, 103);
    spam[QStringLiteral("spam")] = true;
    results.append(spam);
    QJsonObject noTaxon = validObservation(4, 104);
    noTaxon.remove(QStringLiteral("taxon"));
    results.append(noTaxon);
    QJsonObject closedPhoto = validObservation(5, 105);
    closedPhoto[QStringLiteral("photos")] = QJsonArray{QJsonObject{
        {"id", 105}, {"license_code", "cc-by"},
        {"url", "https://inaturalist-open-data.s3.amazonaws.com/photos/105/square.jpg"}}};
    results.append(closedPhoto);

    QString error;
    QVERIFY(backend.observationsFromPayload(responsePayload(results), &error).isEmpty());
    QVERIFY(error.contains(QStringLiteral("CC0"), Qt::CaseInsensitive));
}

void NatureBackendTest::cacheRoundTripRejectsTampering()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    NatureBackend backend(dataRoot.path());
    QString error;
    const QVariantList source = backend.observationsFromPayload(responsePayload(), &error);
    const QDateTime fetchedAt = QDateTime::currentDateTimeUtc();
    QVERIFY(backend.writeCache(source, fetchedAt));

    QVariantList restored;
    QDateTime restoredAt;
    QVERIFY(backend.readCache(&restored, &restoredAt));
    QCOMPARE(restored.size(), 1);
    QCOMPARE(restored.constFirst().toMap().value(QStringLiteral("photoId")).toLongLong(), 84);
    QVERIFY(backend.cacheIsFresh(restoredAt));
    QVERIFY(!backend.cacheIsFresh(fetchedAt.addSecs(-2 * 60 * 60)));

    QFile cache(QDir(dataRoot.path()).filePath(QStringLiteral("nature_observations.json")));
    QVERIFY(cache.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(cache.readAll()).object();
    cache.close();
    QJsonArray items = root.value(QStringLiteral("observations")).toArray();
    QVERIFY(!items.at(0).toObject().contains(QStringLiteral("placeIds")));
    QJsonObject localized = items.at(0).toObject();
    localized[QStringLiteral("city")] = QStringLiteral("Махачкала");
    items[0] = localized;
    root[QStringLiteral("observations")] = items;
    QVERIFY(cache.open(QIODevice::WriteOnly | QIODevice::Truncate));
    cache.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    cache.close();
    QVERIFY(backend.readCache(&restored, &restoredAt));
    QVERIFY(restored.constFirst().toMap().value(QStringLiteral("city")).toString() !=
            QStringLiteral("Махачкала"));

    QJsonObject tampered = items.at(0).toObject();
    tampered[QStringLiteral("url")] = QStringLiteral("https://example.test/tracker/large.jpg");
    items[0] = tampered;
    root[QStringLiteral("observations")] = items;
    QVERIFY(cache.open(QIODevice::WriteOnly | QIODevice::Truncate));
    cache.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    cache.close();
    QVERIFY(!backend.readCache(&restored, &restoredAt));
}

void NatureBackendTest::fetchIsAnonymousAndBounded()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    FakeNatureServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1/v1/observations")
                            .arg(server.serverPort()));
    NatureBackend backend(dataRoot.path(), endpoint);
    QSignalSpy loadedSpy(&backend, &NatureBackend::observationsLoaded);
    QSignalSpy failedSpy(&backend, &NatureBackend::loadFailed);

    backend.refreshObservations();
    QVERIFY2(loadedSpy.wait(3000), qPrintable(failedSpy.isEmpty()
        ? QStringLiteral("No response received") : failedSpy.constFirst().at(0).toString()));
    QCOMPARE(loadedSpy.constFirst().at(0).toList().size(), 1);
    QCOMPARE(server.requests.size(), 2);

    const QList<QByteArray> lines = server.requests.constFirst().split('\n');
    const QList<QByteArray> requestLine = lines.constFirst().trimmed().split(' ');
    QCOMPARE(requestLine.value(0), QByteArray("GET"));
    const QUrl requested(QString::fromLatin1(requestLine.value(1)));
    const QUrlQuery query(requested);
    QCOMPARE(query.queryItemValue(QStringLiteral("per_page")), QStringLiteral("100"));
    QVERIFY(server.requests.constFirst().contains("Accept: application/json"));
    QVERIFY(server.requests.constFirst().contains("User-Agent: OwlSwitch/"));
    QVERIFY(!server.requests.constFirst().contains("Authorization:"));

    const QList<QByteArray> placeLines = server.requests.at(1).split('\n');
    const QUrl requestedPlaces(QString::fromLatin1(
        placeLines.constFirst().trimmed().split(' ').value(1)));
    QVERIFY(requestedPlaces.path().startsWith(QStringLiteral("/v1/places/")));
    const QUrlQuery placeQuery(requestedPlaces);
    QCOMPARE(placeQuery.queryItemValue(QStringLiteral("admin_level")),
             QStringLiteral("0,10,30"));
    QCOMPARE(placeQuery.queryItemValue(QStringLiteral("locale")), QStringLiteral("en"));
    QVERIFY(server.requests.at(1).contains("Accept: application/json"));
    QVERIFY(!server.requests.at(1).contains("Authorization:"));
    QVERIFY(!loadedSpy.constFirst().at(0).toList().constFirst().toMap()
                 .contains(QStringLiteral("placeIds")));

    for (int attempt = 0; attempt < 5; ++attempt)
        backend.refreshObservations();
    QTest::qWait(150);
    QCOMPARE(server.requests.size(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 4, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(loadedSpy.size(), 2, 1000);
}

void NatureBackendTest::fetchesLiveObservations()
{
    if (!qEnvironmentVariableIsSet("NATURE_LIVE_TEST"))
        QSKIP("Set NATURE_LIVE_TEST=1 to run the iNaturalist canary");

    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    NatureBackend backend(dataRoot.path());
    QSignalSpy loadedSpy(&backend, &NatureBackend::observationsLoaded);
    QSignalSpy failedSpy(&backend, &NatureBackend::loadFailed);
    backend.refreshObservations();
    QVERIFY2(loadedSpy.wait(45000), qPrintable(failedSpy.isEmpty()
        ? QStringLiteral("No live response received") : failedSpy.constFirst().at(0).toString()));

    const QVariantList observations = loadedSpy.constFirst().at(0).toList();
    QVERIFY(!observations.isEmpty());
    QVERIFY(observations.size() <= 100);
    for (const QVariant &value : observations) {
        const QVariantMap item = value.toMap();
        QVERIFY(item.value(QStringLiteral("url")).toUrl().host() ==
                QStringLiteral("inaturalist-open-data.s3.amazonaws.com"));
        QCOMPARE(item.value(QStringLiteral("licenseCode")).toString(), QStringLiteral("cc0"));
        QVERIFY(!item.contains(QStringLiteral("placeIds")));
        for (const QString &field : {QStringLiteral("city"),
                                     QStringLiteral("region"),
                                     QStringLiteral("country")}) {
            for (const QChar character : item.value(field).toString()) {
                if (character.isLetter()) {
                    const QString failure = QStringLiteral("%1=%2 contains U+%3 (script %4)")
                        .arg(field,
                             item.value(field).toString(),
                             QString::number(character.unicode(), 16).toUpper(),
                             QString::number(character.script()));
                    const QChar::Script script = character.script();
                    QVERIFY2(script == QChar::Script_Latin ||
                                 script == QChar::Script_Common ||
                                 script == QChar::Script_Inherited,
                             qPrintable(failure));
                }
            }
        }
    }
}

QTEST_GUILESS_MAIN(NatureBackendTest)
#include "NatureBackendTest.moc"
