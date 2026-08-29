#include "NatureBackend.h"

#include <CoreFoundation/CoreFoundation.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QUrlQuery>
#include <QVariantMap>
#include <QDebug>

namespace {

constexpr int kCacheSchemaVersion = 3;
constexpr int kMaximumObservations = 100;
constexpr int kMaximumPlaceIds = 512;
constexpr int kMaximumPlaceIdsPerObservation = 64;
constexpr qint64 kMaximumPayloadBytes = 16 * 1024 * 1024;
constexpr qint64 kMaximumCacheBytes = 2 * 1024 * 1024;
constexpr qint64 kFreshCacheSeconds = 60 * 60;
constexpr int kTransferTimeoutMs = 30 * 1000;
constexpr int kMinimumRequestIntervalMs = 1000;

const QUrl kDefaultApiEndpoint(QStringLiteral("https://api.inaturalist.org/v1/observations"));

const QSet<QString> kAllowedPhotoLicenses = {
    QStringLiteral("cc0")
};

QString limited(QString value, int maximumLength)
{
    value = value.simplified();
    if (value.size() > maximumLength)
        value = value.left(maximumLength - 1) + QChar(0x2026);
    return value;
}

QString englishFriendlyPlaceName(QString value)
{
    value = limited(value, 120);
    bool hasNonLatinLetters = false;
    for (const QChar character : value) {
        const QChar::Script script = character.script();
        if (character.isLetter() && script != QChar::Script_Latin &&
            script != QChar::Script_Common && script != QChar::Script_Inherited) {
            hasNonLatinLetters = true;
            break;
        }
    }
    if (!hasNonLatinLetters)
        return value;

    CFMutableStringRef transformed = CFStringCreateMutable(kCFAllocatorDefault, 0);
    if (!transformed)
        return value;
    CFStringAppendCharacters(
        transformed,
        reinterpret_cast<const UniChar *>(value.utf16()),
        value.size());
    const Boolean didTransform = CFStringTransform(
        transformed, nullptr, kCFStringTransformToLatin, false);
    if (!didTransform) {
        CFRelease(transformed);
        return value;
    }

    const CFIndex length = CFStringGetLength(transformed);
    QString result(static_cast<qsizetype>(length), QChar());
    CFStringGetCharacters(
        transformed,
        CFRangeMake(0, length),
        reinterpret_cast<UniChar *>(result.data()));
    CFRelease(transformed);
    return limited(result, 120);
}

QString normalizedCountry(QString country)
{
    country = limited(country, 100);
    QString code = country.toUpper();
    if (code == QLatin1String("USA"))
        code = QStringLiteral("US");
    else if (code == QLatin1String("UK"))
        code = QStringLiteral("GB");
    if (code.size() == 2) {
        const QLocale::Territory territory = QLocale::codeToTerritory(code);
        if (territory != QLocale::AnyTerritory)
            return QLocale::territoryToString(territory);
    }
    return country;
}

} // namespace

NatureBackend::NatureBackend(const QString &dataRoot, QObject *parent)
    : NatureBackend(dataRoot, kDefaultApiEndpoint, parent)
{
}

NatureBackend::NatureBackend(const QString &dataRoot,
                             const QUrl &apiEndpoint,
                             QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_apiEndpoint(apiEndpoint)
    , m_dataRoot(dataRoot)
    , m_cachePath(QDir(dataRoot).filePath(QStringLiteral("nature_observations.json")))
{
}

void NatureBackend::loadLatestObservations()
{
    QVariantList cached;
    QDateTime fetchedAt;
    if (readCache(&cached, &fetchedAt)) {
        m_lastObservations = cached;
        const bool stale = !cacheIsFresh(fetchedAt);
        emit observationsLoaded(cached, true, stale);
        if (!stale)
            return;
        beginRefresh(true);
        return;
    }

    m_lastObservations.clear();
    beginRefresh(false);
}

void NatureBackend::refreshObservations()
{
    beginRefresh(!m_lastObservations.isEmpty());
}

QUrl NatureBackend::requestUrl() const
{
    QUrl url = m_apiEndpoint;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("photos"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("quality_grade"), QStringLiteral("research"));
    query.addQueryItem(QStringLiteral("captive"), QStringLiteral("false"));
    query.addQueryItem(QStringLiteral("photo_license"), QStringLiteral("cc0"));
    query.addQueryItem(QStringLiteral("order_by"), QStringLiteral("created_at"));
    query.addQueryItem(QStringLiteral("order"), QStringLiteral("desc"));
    query.addQueryItem(QStringLiteral("locale"), QStringLiteral("en"));
    query.addQueryItem(QStringLiteral("per_page"), QString::number(kMaximumObservations));
    query.addQueryItem(QStringLiteral("ttl"), QStringLiteral("300"));
    url.setQuery(query);
    return url;
}

void NatureBackend::beginRefresh(bool hasCachedObservations)
{
    if (m_activeReply)
        return;

    if (m_lastRequestStarted.isValid() &&
        m_lastRequestStarted.elapsed() < kMinimumRequestIntervalMs) {
        m_queuedRefreshHasCache = m_queuedRefreshHasCache || hasCachedObservations;
        if (!m_refreshQueued) {
            m_refreshQueued = true;
            const int delay = kMinimumRequestIntervalMs -
                              static_cast<int>(m_lastRequestStarted.elapsed());
            QTimer::singleShot(qMax(1, delay), this, [this] {
                const bool queuedHasCache = m_queuedRefreshHasCache;
                m_refreshQueued = false;
                m_queuedRefreshHasCache = false;
                beginRefresh(queuedHasCache || !m_lastObservations.isEmpty());
            });
        }
        return;
    }

    m_lastRequestStarted.restart();

    emit refreshStarted(hasCachedObservations);

    QNetworkReply *reply = getJson(requestUrl());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleReply(reply);
    });
}

QNetworkReply *NatureBackend::getJson(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader(
        "User-Agent",
        QStringLiteral("OwlSwitch/%1 (+https://github.com/aindaco1/240-mp-jellyfin)")
            .arg(QCoreApplication::applicationVersion()).toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kTransferTimeoutMs);

    QNetworkReply *reply = m_network->get(request);
    m_activeReply = reply;
    connect(reply, &QNetworkReply::metaDataChanged, this, [reply]() {
        bool ok = false;
        const qint64 declaredLength = reply->header(QNetworkRequest::ContentLengthHeader)
                                          .toLongLong(&ok);
        if (ok && declaredLength > kMaximumPayloadBytes) {
            reply->setProperty("naturePayloadTooLarge", true);
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [reply](qint64 received, qint64) {
        if (received > kMaximumPayloadBytes) {
            reply->setProperty("naturePayloadTooLarge", true);
            reply->abort();
        }
    });
    return reply;
}

void NatureBackend::handleReply(QNetworkReply *reply)
{
    if (m_activeReply == reply)
        m_activeReply.clear();

    const bool hasCachedObservations = !m_lastObservations.isEmpty();
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool payloadTooLarge = reply->property("naturePayloadTooLarge").toBool();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QByteArray payload = payloadTooLarge ? QByteArray{} : reply->readAll();
    const QString networkErrorText = reply->errorString();
    reply->deleteLater();

    if (payloadTooLarge || payload.size() > kMaximumPayloadBytes) {
        qWarning("[Nature] Rejected oversized API response");
        emit loadFailed(QStringLiteral("iNaturalist returned too much data."),
                        hasCachedObservations);
        return;
    }

    if (networkError != QNetworkReply::NoError) {
        qWarning("[Nature] API request failed (HTTP %d): %s",
                 statusCode, qPrintable(networkErrorText));
        if (statusCode == 429) {
            emit loadFailed(QStringLiteral("iNaturalist is rate limiting requests. Try again later."),
                            hasCachedObservations);
        } else if (networkError == QNetworkReply::TimeoutError) {
            emit loadFailed(QStringLiteral("iNaturalist did not respond in time."),
                            hasCachedObservations);
        } else {
            emit loadFailed(QStringLiteral("Could not refresh iNaturalist observations."),
                            hasCachedObservations);
        }
        return;
    }

    QString parseError;
    const QVariantList observations = observationsFromPayload(payload, &parseError);
    if (observations.isEmpty()) {
        if (parseError.isEmpty())
            parseError = QStringLiteral("No usable Nature observations were returned.");
        emit loadFailed(parseError, hasCachedObservations);
        return;
    }

    beginPlaceResolution(observations);
}

QUrl NatureBackend::placesRequestUrl(const QList<qint64> &placeIds) const
{
    if (placeIds.isEmpty())
        return {};

    QStringList encodedIds;
    encodedIds.reserve(placeIds.size());
    for (const qint64 placeId : placeIds)
        encodedIds.append(QString::number(placeId));

    QUrl url = m_apiEndpoint;
    QString path = url.path();
    const qsizetype observationsSegment = path.indexOf(QStringLiteral("/observations"));
    if (observationsSegment >= 0)
        path.truncate(observationsSegment);
    else
        path = QStringLiteral("/v1");
    url.setPath(path + QStringLiteral("/places/") + encodedIds.join(QLatin1Char(',')));

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("admin_level"), QStringLiteral("0,10,30"));
    query.addQueryItem(QStringLiteral("locale"), QStringLiteral("en"));
    url.setQuery(query);
    return url;
}

void NatureBackend::beginPlaceResolution(const QVariantList &observations)
{
    QList<qint64> placeIds;
    QSet<qint64> seen;
    for (const QVariant &value : observations) {
        const QVariantList itemPlaceIds = value.toMap().value(QStringLiteral("placeIds")).toList();
        for (const QVariant &placeValue : itemPlaceIds) {
            const qint64 placeId = placeValue.toLongLong();
            if (placeId <= 0 || seen.contains(placeId))
                continue;
            seen.insert(placeId);
            placeIds.append(placeId);
            if (placeIds.size() >= kMaximumPlaceIds)
                break;
        }
        if (placeIds.size() >= kMaximumPlaceIds)
            break;
    }

    if (placeIds.isEmpty()) {
        finishRefresh(observations);
        return;
    }

    m_pendingObservations = observations;
    QNetworkReply *reply = getJson(placesRequestUrl(placeIds));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handlePlacesReply(reply);
    });
}

void NatureBackend::handlePlacesReply(QNetworkReply *reply)
{
    if (m_activeReply == reply)
        m_activeReply.clear();

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool payloadTooLarge = reply->property("naturePayloadTooLarge").toBool();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QByteArray payload = payloadTooLarge ? QByteArray{} : reply->readAll();
    const QString networkErrorText = reply->errorString();
    reply->deleteLater();

    QVariantList observations = m_pendingObservations;
    m_pendingObservations.clear();
    if (payloadTooLarge || payload.size() > kMaximumPayloadBytes) {
        qWarning("[Nature] Rejected oversized Places API response; using location fallback");
    } else if (networkError != QNetworkReply::NoError) {
        qWarning("[Nature] Places API request failed (HTTP %d): %s; using location fallback",
                 statusCode, qPrintable(networkErrorText));
    } else {
        observations = observationsWithEnglishPlaces(observations, payload);
    }
    finishRefresh(observations);
}

void NatureBackend::finishRefresh(QVariantList observations)
{
    for (QVariant &value : observations) {
        QVariantMap item = value.toMap();
        item.remove(QStringLiteral("placeIds"));
        item[QStringLiteral("city")] = englishFriendlyPlaceName(
            item.value(QStringLiteral("city")).toString());
        item[QStringLiteral("region")] = englishFriendlyPlaceName(
            item.value(QStringLiteral("region")).toString());
        item[QStringLiteral("country")] = englishFriendlyPlaceName(
            item.value(QStringLiteral("country")).toString());
        value = item;
    }

    const QDateTime fetchedAt = QDateTime::currentDateTimeUtc();
    if (!writeCache(observations, fetchedAt))
        qWarning("[Nature] Could not update the observations cache");

    m_lastObservations = observations;
    emit observationsLoaded(observations, false, false);
}

QVariantList NatureBackend::observationsWithEnglishPlaces(
    const QVariantList &observations,
    const QByteArray &payload) const
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning("[Nature] Ignored unreadable Places API response");
        return observations;
    }

    QHash<qint64, QJsonObject> places;
    const QJsonArray results = document.object().value(QStringLiteral("results")).toArray();
    for (const QJsonValue &value : results) {
        if (!value.isObject())
            continue;
        const QJsonObject place = value.toObject();
        const qint64 placeId = place.value(QStringLiteral("id")).toVariant().toLongLong();
        const int adminLevel = place.value(QStringLiteral("admin_level")).toInt(999);
        const QString name = limited(place.value(QStringLiteral("name")).toString(), 120);
        if (placeId > 0 && !name.isEmpty() &&
            (adminLevel == 0 || adminLevel == 10 || adminLevel == 30)) {
            places.insert(placeId, place);
        }
    }

    QVariantList resolved;
    resolved.reserve(observations.size());
    for (const QVariant &value : observations) {
        QVariantMap item = value.toMap();
        QString city;
        QString region;
        QString country;
        const QVariantList itemPlaceIds = item.value(QStringLiteral("placeIds")).toList();
        for (const QVariant &placeValue : itemPlaceIds) {
            const QJsonObject place = places.value(placeValue.toLongLong());
            if (place.isEmpty())
                continue;
            const int adminLevel = place.value(QStringLiteral("admin_level")).toInt(999);
            const QString name = limited(place.value(QStringLiteral("name")).toString(), 120);
            if (adminLevel == 0 && country.isEmpty())
                country = normalizedCountry(name);
            else if (adminLevel == 10 && region.isEmpty())
                region = name;
            else if (adminLevel == 30 && city.isEmpty())
                city = name;
        }
        if (!city.isEmpty())
            item[QStringLiteral("city")] = city;
        if (!region.isEmpty())
            item[QStringLiteral("region")] = region;
        if (!country.isEmpty())
            item[QStringLiteral("country")] = country;
        item[QStringLiteral("city")] = englishFriendlyPlaceName(
            item.value(QStringLiteral("city")).toString());
        item[QStringLiteral("region")] = englishFriendlyPlaceName(
            item.value(QStringLiteral("region")).toString());
        item[QStringLiteral("country")] = englishFriendlyPlaceName(
            item.value(QStringLiteral("country")).toString());
        resolved.append(item);
    }
    return resolved;
}

QVariantList NatureBackend::observationsFromPayload(const QByteArray &payload, QString *error) const
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("iNaturalist returned unreadable observation data.");
        return {};
    }

    const QJsonArray results = document.object().value(QStringLiteral("results")).toArray();
    QVariantList observations;
    QSet<qint64> seenObservationIds;
    QSet<qint64> seenPhotoIds;
    for (const QJsonValue &value : results) {
        if (!value.isObject())
            continue;
        const QVariantMap item = itemFromObservation(value.toObject());
        if (item.isEmpty())
            continue;

        const qint64 observationId = item.value(QStringLiteral("id")).toLongLong();
        const qint64 photoId = item.value(QStringLiteral("photoId")).toLongLong();
        if (seenObservationIds.contains(observationId) || seenPhotoIds.contains(photoId))
            continue;
        seenObservationIds.insert(observationId);
        seenPhotoIds.insert(photoId);
        observations.append(item);
        if (observations.size() >= kMaximumObservations)
            break;
    }

    if (observations.isEmpty() && error)
        *error = QStringLiteral("No CC0 research-grade photos were available.");
    return observations;
}

QVariantMap NatureBackend::itemFromObservation(const QJsonObject &observation) const
{
    if (observation.value(QStringLiteral("spam")).toBool() ||
        observation.value(QStringLiteral("captive")).toBool() ||
        observation.value(QStringLiteral("quality_grade")).toString() != QLatin1String("research")) {
        return {};
    }

    const qint64 observationId = observation.value(QStringLiteral("id")).toVariant().toLongLong();
    const QJsonObject taxon = observation.value(QStringLiteral("taxon")).toObject();
    const qint64 taxonId = taxon.value(QStringLiteral("id")).toVariant().toLongLong();
    const QString scientificName = limited(taxon.value(QStringLiteral("name")).toString(), 160);
    if (observationId <= 0 || taxonId <= 0 || scientificName.isEmpty())
        return {};

    QJsonObject selectedPhoto;
    QUrl selectedUrl;
    QString selectedLicense;
    const QJsonArray photos = observation.value(QStringLiteral("photos")).toArray();
    for (const QJsonValue &photoValue : photos) {
        if (!photoValue.isObject())
            continue;
        const QJsonObject photo = photoValue.toObject();
        const QString licenseCode = photo.value(QStringLiteral("license_code"))
                                        .toString().trimmed().toLower();
        const QUrl photoUrl = largePhotoUrl(photo.value(QStringLiteral("url")).toString(),
                                            licenseCode);
        if (!photoUrl.isValid())
            continue;
        const qint64 photoId = photo.value(QStringLiteral("id")).toVariant().toLongLong();
        if (photoId <= 0)
            continue;
        selectedPhoto = photo;
        selectedUrl = photoUrl;
        selectedLicense = licenseCode;
        break;
    }
    if (selectedPhoto.isEmpty())
        return {};

    const QString commonName = limited(
        taxon.value(QStringLiteral("preferred_common_name")).toString(), 160);
    const QString speciesGuess = limited(
        observation.value(QStringLiteral("species_guess")).toString(), 160);
    const QString title = !commonName.isEmpty() ? commonName
                         : !speciesGuess.isEmpty() ? speciesGuess
                         : scientificName;
    const QVariantMap location = locationFromPlaceGuess(
        observation.value(QStringLiteral("place_guess")).toString());
    QVariantList placeIds;
    const QJsonArray storedPlaceIds = observation.value(QStringLiteral("place_ids")).toArray();
    for (const QJsonValue &placeValue : storedPlaceIds) {
        const qint64 placeId = placeValue.toVariant().toLongLong();
        if (placeId > 0 && !placeIds.contains(placeId))
            placeIds.append(placeId);
        if (placeIds.size() >= kMaximumPlaceIdsPerObservation)
            break;
    }

    QVariantMap item;
    item[QStringLiteral("id")] = observationId;
    item[QStringLiteral("photoId")] = selectedPhoto.value(QStringLiteral("id")).toVariant().toLongLong();
    item[QStringLiteral("taxonId")] = taxonId;
    item[QStringLiteral("url")] = selectedUrl.toString(QUrl::FullyEncoded);
    item[QStringLiteral("animated")] = false;
    item[QStringLiteral("title")] = title;
    item[QStringLiteral("scientificName")] = scientificName;
    item[QStringLiteral("city")] = location.value(QStringLiteral("city"));
    item[QStringLiteral("region")] = location.value(QStringLiteral("region"));
    item[QStringLiteral("country")] = location.value(QStringLiteral("country"));
    item[QStringLiteral("placeIds")] = placeIds;
    item[QStringLiteral("licenseCode")] = selectedLicense;
    item[QStringLiteral("observationUrl")] = QStringLiteral(
        "https://www.inaturalist.org/observations/%1").arg(observationId);
    return item;
}

QUrl NatureBackend::largePhotoUrl(const QString &url, const QString &licenseCode) const
{
    if (!kAllowedPhotoLicenses.contains(licenseCode))
        return {};

    QUrl parsed(url.trimmed());
    if (!parsed.isValid() || parsed.scheme() != QLatin1String("https") ||
        parsed.host().toLower() != QLatin1String("inaturalist-open-data.s3.amazonaws.com")) {
        return {};
    }

    QString path = parsed.path();
    static const QRegularExpression sizePattern(
        QStringLiteral("/(?:square|thumb|small|medium|large|original)\\.([A-Za-z0-9]+)$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = sizePattern.match(path);
    if (!match.hasMatch())
        return {};
    path.replace(match.capturedStart(0), match.capturedLength(0),
                 QStringLiteral("/large.%1").arg(match.captured(1).toLower()));
    parsed.setPath(path);
    parsed.setFragment(QString());
    return parsed;
}

QVariantMap NatureBackend::locationFromPlaceGuess(const QString &placeGuess) const
{
    const QString simplified = limited(placeGuess, 512);
    QStringList normalizedParts;
    const QStringList parts = simplified.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString normalized = part.simplified();
        if (normalized.isEmpty())
            continue;
        if (!normalizedParts.isEmpty() &&
            normalizedParts.constLast().compare(normalized, Qt::CaseInsensitive) == 0) {
            continue;
        }
        normalizedParts.append(normalized);
    }

    QVariantMap location{{QStringLiteral("city"), QString()},
                         {QStringLiteral("region"), QString()},
                         {QStringLiteral("country"), QString()}};
    if (normalizedParts.isEmpty())
        return location;

    location[QStringLiteral("country")] = normalizedCountry(normalizedParts.constLast());
    if (normalizedParts.size() == 2) {
        location[QStringLiteral("region")] = limited(normalizedParts.at(0), 120);
    } else if (normalizedParts.size() >= 3) {
        location[QStringLiteral("city")] = limited(
            normalizedParts.at(normalizedParts.size() - 3), 120);
        QString region = limited(normalizedParts.at(normalizedParts.size() - 2), 120);
        static const QRegularExpression usPostalCode(
            QStringLiteral("\\s+\\d{5}(?:-\\d{4})?$"));
        region.remove(usPostalCode);
        location[QStringLiteral("region")] = region.trimmed();
    }
    return location;
}

QVariantMap NatureBackend::validatedCachedItem(const QJsonObject &object) const
{
    const qint64 observationId = object.value(QStringLiteral("id")).toVariant().toLongLong();
    const qint64 photoId = object.value(QStringLiteral("photoId")).toVariant().toLongLong();
    const qint64 taxonId = object.value(QStringLiteral("taxonId")).toVariant().toLongLong();
    const QString licenseCode = object.value(QStringLiteral("licenseCode"))
                                    .toString().trimmed().toLower();
    const QUrl photoUrl = largePhotoUrl(object.value(QStringLiteral("url")).toString(),
                                        licenseCode);
    const QString scientificName = limited(
        object.value(QStringLiteral("scientificName")).toString(), 160);
    const QString title = limited(object.value(QStringLiteral("title")).toString(), 160);
    if (observationId <= 0 || photoId <= 0 || taxonId <= 0 || !photoUrl.isValid() ||
        scientificName.isEmpty() || title.isEmpty()) {
        return {};
    }

    QVariantMap item;
    item[QStringLiteral("id")] = observationId;
    item[QStringLiteral("photoId")] = photoId;
    item[QStringLiteral("taxonId")] = taxonId;
    item[QStringLiteral("url")] = photoUrl.toString(QUrl::FullyEncoded);
    item[QStringLiteral("animated")] = false;
    item[QStringLiteral("title")] = title;
    item[QStringLiteral("scientificName")] = scientificName;
    item[QStringLiteral("city")] = englishFriendlyPlaceName(
        object.value(QStringLiteral("city")).toString());
    item[QStringLiteral("region")] = englishFriendlyPlaceName(
        object.value(QStringLiteral("region")).toString());
    item[QStringLiteral("country")] = englishFriendlyPlaceName(normalizedCountry(
        object.value(QStringLiteral("country")).toString()));
    item[QStringLiteral("licenseCode")] = licenseCode;
    item[QStringLiteral("observationUrl")] = QStringLiteral(
        "https://www.inaturalist.org/observations/%1").arg(observationId);
    return item;
}

bool NatureBackend::readCache(QVariantList *observations, QDateTime *fetchedAt) const
{
    QFileInfo info(m_cachePath);
    if (!info.isFile() || info.size() <= 0 || info.size() > kMaximumCacheBytes)
        return false;

    QFile file(m_cachePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return false;

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt() != kCacheSchemaVersion)
        return false;
    const QDateTime cacheTime = QDateTime::fromString(
        root.value(QStringLiteral("fetchedAt")).toString(), Qt::ISODate);
    if (!cacheTime.isValid() || cacheTime > QDateTime::currentDateTimeUtc().addSecs(5 * 60))
        return false;

    const QJsonArray storedItems = root.value(QStringLiteral("observations")).toArray();
    if (storedItems.isEmpty() || storedItems.size() > kMaximumObservations)
        return false;

    QVariantList validated;
    QSet<qint64> observationIds;
    QSet<qint64> photoIds;
    for (const QJsonValue &value : storedItems) {
        if (!value.isObject())
            return false;
        const QVariantMap item = validatedCachedItem(value.toObject());
        if (item.isEmpty())
            return false;
        const qint64 observationId = item.value(QStringLiteral("id")).toLongLong();
        const qint64 photoId = item.value(QStringLiteral("photoId")).toLongLong();
        if (observationIds.contains(observationId) || photoIds.contains(photoId))
            return false;
        observationIds.insert(observationId);
        photoIds.insert(photoId);
        validated.append(item);
    }

    if (observations)
        *observations = validated;
    if (fetchedAt)
        *fetchedAt = cacheTime.toUTC();
    return true;
}

bool NatureBackend::writeCache(const QVariantList &observations,
                               const QDateTime &fetchedAt) const
{
    if (observations.isEmpty() || observations.size() > kMaximumObservations ||
        !QDir().mkpath(m_dataRoot)) {
        return false;
    }

    QVariantList cacheObservations;
    cacheObservations.reserve(observations.size());
    for (const QVariant &value : observations) {
        QVariantMap item = value.toMap();
        item.remove(QStringLiteral("placeIds"));
        cacheObservations.append(item);
    }

    QJsonObject queryPolicy;
    queryPolicy[QStringLiteral("count")] = kMaximumObservations;
    queryPolicy[QStringLiteral("qualityGrade")] = QStringLiteral("research");
    queryPolicy[QStringLiteral("captive")] = false;
    queryPolicy[QStringLiteral("photoLicenses")] = QJsonArray{QStringLiteral("cc0")};

    QJsonObject root;
    root[QStringLiteral("schemaVersion")] = kCacheSchemaVersion;
    root[QStringLiteral("fetchedAt")] = fetchedAt.toUTC().toString(Qt::ISODate);
    root[QStringLiteral("queryPolicy")] = queryPolicy;
    root[QStringLiteral("observations")] = QJsonArray::fromVariantList(cacheObservations);
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (payload.size() > kMaximumCacheBytes)
        return false;

    QSaveFile file(m_cachePath);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (file.write(payload) != payload.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

bool NatureBackend::cacheIsFresh(const QDateTime &fetchedAt) const
{
    if (!fetchedAt.isValid())
        return false;
    const qint64 age = fetchedAt.toUTC().secsTo(QDateTime::currentDateTimeUtc());
    return age >= 0 && age <= kFreshCacheSeconds;
}
