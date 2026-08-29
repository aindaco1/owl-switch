#include "JellyfinBackend.h"

#include <QFile>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QNetworkRequest>
#include <QSysInfo>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QVariantList>
#include <QVariantMap>
#include <QDebug>
#include <QRegularExpression>

static constexpr int kItemsPageSize = 250;

static QString browseItemFields(const QString &itemType) {
    QString fields = QStringLiteral("RunTimeTicks,ProductionYear,UserData,SeriesName,SeasonName,IndexNumber,ParentIndexNumber");
    if (itemType == "Series")
        fields += QStringLiteral(",Overview,RecursiveItemCount,ChildCount,Status,Genres,CommunityRating,OfficialRating");
    return fields;
}

static QString detailItemFields() {
    return browseItemFields(QStringLiteral("Episode")) +
           QStringLiteral(",Overview,MediaSources,MediaStreams,Genres,CommunityRating,OfficialRating,Status");
}

static QString normalizedBrowseItemType(const QString &itemType) {
    const QString trimmed = itemType.trimmed();
    if (trimmed.compare("Movie", Qt::CaseInsensitive) == 0)
        return QStringLiteral("Movie");
    if (trimmed.compare("Series", Qt::CaseInsensitive) == 0)
        return QStringLiteral("Series");
    if (trimmed.compare("Season", Qt::CaseInsensitive) == 0)
        return QStringLiteral("Season");
    if (trimmed.compare("Episode", Qt::CaseInsensitive) == 0)
        return QStringLiteral("Episode");
    if (trimmed.compare("BoxSet", Qt::CaseInsensitive) == 0)
        return QStringLiteral("BoxSet");
    if (trimmed.compare("Folder", Qt::CaseInsensitive) == 0)
        return QStringLiteral("Folder");
    if (trimmed.compare("Video", Qt::CaseInsensitive) == 0)
        return QStringLiteral("Video");
    return {};
}

static QString itemCacheKey(const QString &parentId, const QString &itemType, bool recursive) {
    return QString("%1|%2|%3").arg(parentId, itemType, recursive ? QStringLiteral("recursive")
                                                                 : QStringLiteral("direct"));
}

JellyfinBackend::JellyfinBackend(const QString &appRoot, const QString &dataRoot, QObject *parent)
    : QObject(parent)
    , m_dataRoot(dataRoot)
    , m_nam(new QNetworkAccessManager(this))
    , m_quickConnectTimer(new QTimer(this))
{
    Q_UNUSED(appRoot);
    m_quickConnectTimer->setInterval(2000);
    connect(m_quickConnectTimer, &QTimer::timeout, this, &JellyfinBackend::pollQuickConnect);
    const QJsonObject auth = loadAuth();
    m_lastAudioLanguage = auth.value("lastAudioLanguage").toString();
    m_lastSubtitleLanguage = auth.value("lastSubtitleLanguage").toString("__off__");
}

QJsonObject JellyfinBackend::moduleConfig() const {
    QFile file(m_dataRoot + QStringLiteral("/config.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object()
        .value("modules").toObject().value("com.240mp.jellyfin").toObject();
}

int JellyfinBackend::videoQualityBitrate() const {
    const QString quality = moduleConfig().value("video_quality").toString("direct");
    if (quality == "480p") return 4000000;
    if (quality == "576p") return 5000000;
    if (quality == "720p") return 8000000;
    if (quality == "1080p") return 16000000;
    return 1000000000;
}

int JellyfinBackend::videoQualityMaxHeight() const {
    const QString quality = moduleConfig().value("video_quality").toString("direct");
    if (quality == "480p") return 480;
    if (quality == "576p") return 576;
    if (quality == "720p") return 720;
    if (quality == "1080p") return 1080;
    return 0;
}

QString JellyfinBackend::authFilePath() const {
    return m_dataRoot + "/jellyfin_auth.json";
}

QJsonObject JellyfinBackend::loadAuth() const {
    QFile f(authFilePath());
    if (!f.open(QIODevice::ReadOnly))
        return {};

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return {};

    return doc.object();
}

void JellyfinBackend::saveAuth(const QJsonObject &auth) const {
    QFile f(authFilePath());
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("[JellyfinBackend] Could not write jellyfin_auth.json: %s", qPrintable(f.errorString()));
        return;
    }
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(QJsonDocument(auth).toJson(QJsonDocument::Indented));
    f.close();
    QFile::setPermissions(authFilePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

QString JellyfinBackend::normalizedServerUrl(const QString &serverUrl) const {
    QString trimmed = serverUrl.trimmed();
    while (trimmed.endsWith('/'))
        trimmed.chop(1);
    if (!trimmed.isEmpty() && !trimmed.startsWith("http://") && !trimmed.startsWith("https://"))
        trimmed.prepend("https://");
    return trimmed;
}

QString JellyfinBackend::serverUrl() const {
    return loadAuth()["serverUrl"].toString();
}

QString JellyfinBackend::accessToken() const {
    return loadAuth()["accessToken"].toString();
}

QString JellyfinBackend::userId() const {
    return loadAuth()["userId"].toString();
}

QString JellyfinBackend::deviceId() const {
    QJsonObject auth = loadAuth();
    QString existing = auth["deviceId"].toString();
    if (!existing.isEmpty())
        return existing;

    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auth["deviceId"] = id;
    saveAuth(auth);
    return id;
}

QString JellyfinBackend::authorizationValue(const QString &token) const {
    QStringList parts;
    parts << "MediaBrowser Client=\"OwlSwitch\"";
    parts << QString("Device=\"%1\"").arg(QSysInfo::prettyProductName().isEmpty()
                                             ? QStringLiteral("macOS")
                                             : QSysInfo::prettyProductName());
    parts << QString("DeviceId=\"%1\"").arg(deviceId());
    parts << QString("Version=\"%1\"").arg(QCoreApplication::applicationVersion());
    if (!token.isEmpty())
        parts << QString("Token=\"%1\"").arg(token);
    return parts.join(", ");
}

QNetworkRequest JellyfinBackend::jellyfinRequest(const QUrl &url, const QString &token,
                                                 bool includeSavedToken) const {
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");

    const QString effectiveToken = token.isEmpty() && includeSavedToken ? accessToken() : token;
    const QString authValue = authorizationValue(effectiveToken);
    req.setRawHeader("Authorization", authValue.toUtf8());
    req.setRawHeader("X-Emby-Authorization", authValue.toUtf8());
    return req;
}

QNetworkReply *JellyfinBackend::jellyfinGet(const QUrl &url) {
    return m_nam->get(jellyfinRequest(url));
}

QNetworkReply *JellyfinBackend::jellyfinPostJson(const QUrl &url, const QByteArray &body,
                                                 const QString &token, bool includeSavedToken) {
    return m_nam->post(jellyfinRequest(url, token, includeSavedToken), body);
}

QString JellyfinBackend::get_auth_state() {
    QJsonObject auth = loadAuth();
    return (!auth["serverUrl"].toString().isEmpty() &&
            !auth["accessToken"].toString().isEmpty() &&
            !auth["userId"].toString().isEmpty())
        ? QStringLiteral("authed")
        : QStringLiteral("unauthenticated");
}

QString JellyfinBackend::get_active_user_name() {
    return loadAuth()["username"].toString();
}

QString JellyfinBackend::get_active_server_name() {
    const QJsonObject auth = loadAuth();
    QString name = auth["serverName"].toString();
    return name.isEmpty() ? auth["serverUrl"].toString() : name;
}

QString JellyfinBackend::get_last_server_url() {
    return loadAuth()["serverUrl"].toString();
}

void JellyfinBackend::authenticate(const QString &serverUrlInput, const QString &username, const QString &password) {
    cancel_quick_connect();

    const QString base = normalizedServerUrl(serverUrlInput);
    if (base.isEmpty() || username.trimmed().isEmpty() || password.isEmpty()) {
        emit errorOccurred("Server URL, username, and password are required.");
        return;
    }

    QJsonObject existing = loadAuth();
    if (existing["deviceId"].toString().isEmpty()) {
        existing["deviceId"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        saveAuth(existing);
    }

    QJsonObject body;
    body["Username"] = username.trimmed();
    body["Pw"] = password;

    QNetworkReply *reply = jellyfinPostJson(QUrl(base + "/Users/AuthenticateByName"),
                                            QJsonDocument(body).toJson(QJsonDocument::Compact),
                                            QString(),
                                            false);
    connect(reply, &QNetworkReply::finished, this, [this, reply, base]() {
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError error = reply->error();
        reply->deleteLater();

        if (error != QNetworkReply::NoError || status < 200 || status >= 300) {
            emit errorOccurred(QString("Jellyfin sign-in failed (%1).").arg(status > 0 ? status : int(error)));
            return;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            emit errorOccurred("Jellyfin sign-in returned an invalid response.");
            return;
        }

        if (!saveAuthenticationResult(doc.object(), base)) {
            emit errorOccurred("Jellyfin sign-in did not return a usable access token.");
            return;
        }

        emit authSuccess();
        emit authStateChanged();
    });
}

void JellyfinBackend::start_quick_connect(const QString &serverUrlInput) {
    cancel_quick_connect();

    const QString base = normalizedServerUrl(serverUrlInput);
    if (base.isEmpty()) {
        emit errorOccurred("Server URL is required for Quick Connect.");
        return;
    }

    QJsonObject existing = loadAuth();
    if (existing["deviceId"].toString().isEmpty()) {
        existing["deviceId"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        saveAuth(existing);
    }

    QNetworkReply *reply = jellyfinPostJson(QUrl(base + "/QuickConnect/Initiate"),
                                            QByteArray{},
                                            QString(),
                                            false);
    connect(reply, &QNetworkReply::finished, this, [this, reply, base]() {
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError error = reply->error();
        reply->deleteLater();

        if (error != QNetworkReply::NoError || status < 200 || status >= 300) {
            emit errorOccurred(QString("Quick Connect could not start (%1).").arg(status > 0 ? status : int(error)));
            return;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            emit errorOccurred("Quick Connect returned an invalid response.");
            return;
        }

        const QJsonObject result = doc.object();
        m_quickConnectSecret = result["Secret"].toString();
        const QString code = result["Code"].toString();
        if (m_quickConnectSecret.isEmpty() || code.isEmpty()) {
            emit errorOccurred("Quick Connect did not return a usable code.");
            return;
        }

        m_quickConnectServerUrl = base;
        emit quickConnectCodeReady(code);
        m_quickConnectTimer->start();
    });
}

void JellyfinBackend::cancel_quick_connect() {
    m_quickConnectTimer->stop();
    m_quickConnectServerUrl.clear();
    m_quickConnectSecret.clear();
}

void JellyfinBackend::logout() {
    cancel_quick_connect();
    ++m_itemsLoadGeneration;
    m_itemsLoadAccumulator.clear();
    m_itemCache.clear();
    QFile::remove(authFilePath());
    emit logoutComplete();
    emit authStateChanged();
}

void JellyfinBackend::handleAuthFailure(const QString &message) {
    emit errorOccurred(message);
    emit authStateChanged();
}

void JellyfinBackend::load_libraries() {
    const QString base = serverUrl();
    const QString uid = userId();
    if (base.isEmpty() || uid.isEmpty()) {
        handleAuthFailure("Jellyfin is not signed in.");
        return;
    }

    QUrl url(base + "/UserViews");
    QUrlQuery query;
    query.addQueryItem("userId", uid);
    query.addQueryItem("includeExternalContent", "false");
    query.addQueryItem("includeHidden", "false");
    url.setQuery(query);

    QNetworkReply *reply = jellyfinGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError error = reply->error();
        reply->deleteLater();

        if (error != QNetworkReply::NoError || status < 200 || status >= 300) {
            emit errorOccurred(QString("Could not load Jellyfin libraries (%1).").arg(status > 0 ? status : int(error)));
            return;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            emit errorOccurred("Jellyfin libraries response was invalid.");
            return;
        }

        QVariantList libraries;
        const QJsonObject enabledLibraries = moduleConfig().value("libraries").toObject();
        const QJsonArray items = doc.object()["Items"].toArray();
        for (const QJsonValue &value : items) {
            const QJsonObject item = value.toObject();
            const QString id = item.value("Id").toString();
            if (!enabledLibraries.isEmpty() && !enabledLibraries.value(id).toBool(true))
                continue;
            const QVariantMap formatted = formatLibrary(item);
            if (!formatted["id"].toString().isEmpty())
                libraries.append(formatted);
        }
        emit librariesLoaded(libraries);
    });
}

void JellyfinBackend::load_items(const QString &libraryId) {
    load_items_for_type(libraryId, QStringLiteral("Movie"), true);
}

void JellyfinBackend::load_items_for_type(const QString &parentId, const QString &itemType, bool recursive) {
    const QString base = serverUrl();
    const QString uid = userId();
    if (base.isEmpty() || uid.isEmpty() || parentId.isEmpty()) {
        emit errorOccurred("Jellyfin library selection is invalid.");
        return;
    }

    const QString normalizedItemType = normalizedBrowseItemType(itemType);
    if (normalizedItemType.isEmpty()) {
        emit errorOccurred("Jellyfin item type is not supported.");
        return;
    }

    const QString cacheKey = itemCacheKey(parentId, normalizedItemType, recursive);
    const int generation = ++m_itemsLoadGeneration;
    m_itemsLoadAccumulator.clear();
    if (m_itemCache.contains(cacheKey)) {
        const QVariantList cached = m_itemCache.value(cacheKey);
        emit itemsPageLoaded(cached, true, true);
        return;
    }

    load_items_page(parentId, normalizedItemType, recursive, 0, generation);
}

void JellyfinBackend::load_items_page(const QString &parentId, const QString &itemType, bool recursive,
                                      int startIndex, int generation) {
    if (generation != m_itemsLoadGeneration)
        return;

    const QString base = serverUrl();
    const QString uid = userId();
    if (base.isEmpty() || uid.isEmpty() || parentId.isEmpty()) {
        emit errorOccurred("Jellyfin library selection is invalid.");
        return;
    }

    QUrl url(base + "/Items");
    QUrlQuery query;
    query.addQueryItem("userId", uid);
    query.addQueryItem("parentId", parentId);
    query.addQueryItem("recursive", recursive ? "true" : "false");
    query.addQueryItem("includeItemTypes", itemType);
    query.addQueryItem("fields", browseItemFields(itemType));
    query.addQueryItem("enableImages", "false");
    query.addQueryItem("enableTotalRecordCount", "false");
    if (itemType == "Episode") {
        query.addQueryItem("mediaTypes", "Video");
        query.addQueryItem("excludeLocationTypes", "Virtual");
        query.addQueryItem("isMissing", "false");
        query.addQueryItem("isPlaceHolder", "false");
        query.addQueryItem("isUnaired", "false");
    }
    query.addQueryItem("startIndex", QString::number(startIndex));
    query.addQueryItem("limit", QString::number(kItemsPageSize));
    query.addQueryItem("sortBy", "SortName");
    query.addQueryItem("sortOrder", "Ascending");
    url.setQuery(query);

    QNetworkReply *reply = jellyfinGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply, parentId, itemType, recursive, startIndex, generation]() {
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError error = reply->error();
        reply->deleteLater();

        if (generation != m_itemsLoadGeneration)
            return;

        if (error != QNetworkReply::NoError || status < 200 || status >= 300) {
            emit errorOccurred(QString("Could not load Jellyfin items (%1).").arg(status > 0 ? status : int(error)));
            return;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            emit errorOccurred("Jellyfin items response was invalid.");
            return;
        }

        QVariantList pageItems;
        const QJsonArray rawItems = doc.object()["Items"].toArray();
        for (const QJsonValue &value : rawItems) {
            const QJsonObject item = value.toObject();
            if (item["Id"].toString().isEmpty())
                continue;

            const QVariantMap formatted = formatItem(item);
            if (!formatted["id"].toString().isEmpty()) {
                pageItems.append(formatted);
                m_itemsLoadAccumulator.append(formatted);
            }
        }

        const bool finished = rawItems.size() < kItemsPageSize;
        emit itemsPageLoaded(pageItems, startIndex == 0, finished);

        if (finished) {
            m_itemCache.insert(itemCacheKey(parentId, itemType, recursive), m_itemsLoadAccumulator);
            m_itemsLoadAccumulator.clear();
            return;
        }

        load_items_page(parentId, itemType, recursive, startIndex + rawItems.size(), generation);
    });
}

void JellyfinBackend::load_item_detail(const QString &itemId) {
    const QString base = serverUrl();
    const QString uid = userId();
    if (base.isEmpty() || uid.isEmpty() || itemId.isEmpty()) {
        emit errorOccurred("Jellyfin item selection is invalid.");
        return;
    }

    QUrl url(base + "/Items/" + itemId);
    QUrlQuery query;
    query.addQueryItem("userId", uid);
    query.addQueryItem("fields", detailItemFields());
    url.setQuery(query);

    QNetworkReply *reply = jellyfinGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError error = reply->error();
        reply->deleteLater();

        if (error != QNetworkReply::NoError || status < 200 || status >= 300) {
            emit errorOccurred(QString("Could not load Jellyfin item (%1).").arg(status > 0 ? status : int(error)));
            return;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            emit errorOccurred("Jellyfin item response was invalid.");
            return;
        }

        emit itemLoaded(formatItem(doc.object()));
    });
}

void JellyfinBackend::load_special_items(const QUrl &url, const QString &errorPrefix) {
    QNetworkReply *reply = jellyfinGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply, errorPrefix]() {
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto error = reply->error();
        reply->deleteLater();
        if (error != QNetworkReply::NoError || status < 200 || status >= 300) {
            emit errorOccurred(QString("%1 (%2).").arg(errorPrefix).arg(status > 0 ? status : int(error)));
            return;
        }
        const QJsonArray rawItems = QJsonDocument::fromJson(data).object().value("Items").toArray();
        QVariantList items;
        for (const QJsonValue &value : rawItems)
            items << formatItem(value.toObject());
        emit itemsPageLoaded(items, true, true);
    });
}

void JellyfinBackend::load_continue_watching() {
    QUrl url(serverUrl() + "/Users/" + userId() + "/Items/Resume");
    QUrlQuery query;
    query.addQueryItem("limit", "50");
    query.addQueryItem("fields", detailItemFields());
    query.addQueryItem("mediaTypes", "Video");
    url.setQuery(query);
    load_special_items(url, QStringLiteral("Could not load Continue Watching"));
}

void JellyfinBackend::load_up_next() {
    QUrl url(serverUrl() + "/Shows/NextUp");
    QUrlQuery query;
    query.addQueryItem("userId", userId());
    query.addQueryItem("limit", "50");
    query.addQueryItem("fields", detailItemFields());
    query.addQueryItem("enableUserData", "true");
    url.setQuery(query);
    load_special_items(url, QStringLiteral("Could not load Up Next"));
}

void JellyfinBackend::load_boxset_children(const QString &parentId) {
    QUrl url(serverUrl() + "/Users/" + userId() + "/Items");
    QUrlQuery query;
    query.addQueryItem("parentId", parentId);
    query.addQueryItem("recursive", "false");
    query.addQueryItem("fields", detailItemFields() + ",PremiereDate,DateCreated,IsFolder");
    query.addQueryItem("sortBy", "PremiereDate,ProductionYear,SortName");
    query.addQueryItem("sortOrder", "Ascending");
    url.setQuery(query);
    load_special_items(url, QStringLiteral("Could not load collection"));
}

void JellyfinBackend::load_folder_children(const QString &parentId) {
    QUrl url(serverUrl() + "/Users/" + userId() + "/Items");
    QUrlQuery query;
    query.addQueryItem("parentId", parentId);
    query.addQueryItem("recursive", "false");
    query.addQueryItem("fields", detailItemFields() + ",IsFolder");
    query.addQueryItem("sortBy", "SortName");
    query.addQueryItem("sortOrder", "Ascending");
    url.setQuery(query);
    load_special_items(url, QStringLiteral("Could not load folder"));
}

void JellyfinBackend::build_stream_url(const QString &itemId, const QString &mediaSourceId) {
    request_playback(itemId, mediaSourceId, -1, -1, false, 0);
}

void JellyfinBackend::request_playback(const QString &itemId, const QString &mediaSourceId,
                                       int audioStreamIndex, int subtitleStreamIndex,
                                       bool forceTranscode, qint64 startPositionTicks) {
    const QString base = serverUrl();
    const QString token = accessToken();
    if (base.isEmpty() || token.isEmpty() || itemId.isEmpty()) {
        emit errorOccurred("Jellyfin stream request is missing authentication.");
        return;
    }

    const QString quality = moduleConfig().value("video_quality").toString("direct");
    const bool wantsDirectPlay = !forceTranscode && quality == QLatin1String("direct");

    QJsonObject body;
    body["UserId"] = userId();
    if (!mediaSourceId.isEmpty()) body["MediaSourceId"] = mediaSourceId;
    if (audioStreamIndex >= 0) body["AudioStreamIndex"] = audioStreamIndex;
    if (subtitleStreamIndex >= 0) body["SubtitleStreamIndex"] = subtitleStreamIndex;
    if (videoQualityBitrate() > 0) body["MaxStreamingBitrate"] = videoQualityBitrate();
    if (videoQualityMaxHeight() > 0) body["MaxHeight"] = videoQualityMaxHeight();
    body["EnableDirectPlay"] = wantsDirectPlay;
    body["EnableDirectStream"] = wantsDirectPlay;

    QJsonArray subtitleProfiles;
    const QStringList subtitleFormats = {
        "subrip", "srt", "ass", "ssa", "vtt", "webvtt", "mov_text",
        "pgssub", "dvbsub", "dvdsub"
    };
    for (const QString &format : subtitleFormats) {
        subtitleProfiles.append(QJsonObject{
            {"Format", format},
            {"Method", wantsDirectPlay ? QStringLiteral("Embed") : QStringLiteral("Encode")}
        });
    }
    QJsonObject profile;
    profile["SubtitleProfiles"] = subtitleProfiles;
    profile["TranscodingProfiles"] = QJsonArray{QJsonObject{
        {"Container", "ts"}, {"Type", "Video"}, {"VideoCodec", "h264"},
        {"AudioCodec", "aac,mp3"}, {"Protocol", "hls"}
    }};
    if (wantsDirectPlay)
        profile["DirectPlayProfiles"] = QJsonArray{QJsonObject{{"Type", "Video"}}};
    else
        profile["DirectPlayProfiles"] = QJsonArray{};
    body["DeviceProfile"] = profile;

    QUrl playbackInfoUrl(base + "/Items/" + itemId + "/PlaybackInfo");
    QNetworkReply *reply = jellyfinPostJson(
        playbackInfoUrl, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, base, token, itemId, mediaSourceId, audioStreamIndex,
             subtitleStreamIndex, wantsDirectPlay, startPositionTicks]() {
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto error = reply->error();
        reply->deleteLater();
        if (error != QNetworkReply::NoError || status < 200 || status >= 300) {
            emit errorOccurred(QString("Jellyfin playback negotiation failed (%1).")
                                   .arg(status > 0 ? status : int(error)));
            return;
        }

        const QJsonObject response = QJsonDocument::fromJson(data).object();
        const QJsonArray sources = response.value("MediaSources").toArray();
        if (sources.isEmpty()) {
            emit errorOccurred("Jellyfin did not return a playable source.");
            return;
        }

        const QJsonObject source = sources.first().toObject();
        const QString playSessionId = response.value("PlaySessionId").toString();
        m_currentPlaySessionId = playSessionId;
        QString urlString;

        if (wantsDirectPlay && (source.value("SupportsDirectPlay").toBool() ||
                                source.value("SupportsDirectStream").toBool())) {
            QUrl directUrl(base + "/Videos/" + itemId + "/stream");
            QUrlQuery query;
            query.addQueryItem("static", "true");
            query.addQueryItem("deviceId", deviceId());
            query.addQueryItem("mediaSourceId", source.value("Id").toString(mediaSourceId));
            if (!playSessionId.isEmpty()) query.addQueryItem("PlaySessionId", playSessionId);
            directUrl.setQuery(query);
            urlString = directUrl.toString();
            m_currentPlayMethod = QStringLiteral("DirectPlay");
        } else {
            QString transcodePath = source.value("TranscodingUrl").toString();
            if (transcodePath.isEmpty()) {
                emit errorOccurred("Jellyfin did not return a transcode URL.");
                return;
            }
            QUrl transcodeUrl = QUrl(transcodePath).isRelative()
                ? QUrl(base + transcodePath) : QUrl(transcodePath);
            QUrlQuery query(transcodeUrl);
            for (const auto &item : query.queryItems()) {
                if (item.first.compare("api_key", Qt::CaseInsensitive) == 0 ||
                    item.first.compare("apikey", Qt::CaseInsensitive) == 0)
                    query.removeAllQueryItems(item.first);
            }
            if (subtitleStreamIndex < 0) {
                query.removeAllQueryItems("SubtitleStreamIndex");
                query.removeAllQueryItems("SubtitleMethod");
            }
            const int maxHeight = videoQualityMaxHeight();
            if (maxHeight > 0) {
                query.removeAllQueryItems("MaxHeight");
                query.addQueryItem("MaxHeight", QString::number(maxHeight));
            }
            transcodeUrl.setQuery(query);
            urlString = transcodeUrl.toString();
            m_currentPlayMethod = QStringLiteral("Transcode");
        }

        report_playback_start(itemId, mediaSourceId, audioStreamIndex,
                              subtitleStreamIndex, startPositionTicks);
        emit streamUrlReady(urlString,
            QVariantList{QString("X-Emby-Token:%1").arg(token)});
    });
}

void JellyfinBackend::report_playback_start(const QString &itemId, const QString &mediaSourceId,
                                            int audioStreamIndex, int subtitleStreamIndex,
                                            qint64 startPositionTicks) {
    QJsonObject body{{"ItemId", itemId}, {"MediaSourceId", mediaSourceId},
                     {"PlayMethod", m_currentPlayMethod}, {"IsPaused", false},
                     {"CanSeek", true}};
    if (!m_currentPlaySessionId.isEmpty()) body["PlaySessionId"] = m_currentPlaySessionId;
    if (audioStreamIndex >= 0) body["AudioStreamIndex"] = audioStreamIndex;
    if (subtitleStreamIndex >= 0) body["SubtitleStreamIndex"] = subtitleStreamIndex;
    if (startPositionTicks > 0) body["StartPositionTicks"] = double(startPositionTicks);
    QNetworkReply *reply = jellyfinPostJson(
        QUrl(serverUrl() + "/Sessions/Playing"),
        QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

void JellyfinBackend::update_playback_progress(const QString &itemId,
                                               const QString &mediaSourceId,
                                               qint64 positionTicks, bool isPaused) {
    if (get_auth_state() != QLatin1String("authed")) return;
    QJsonObject body{{"ItemId", itemId}, {"MediaSourceId", mediaSourceId},
                     {"PositionTicks", double(positionTicks)}, {"IsPaused", isPaused},
                     {"PlayMethod", m_currentPlayMethod}, {"CanSeek", true}};
    if (!m_currentPlaySessionId.isEmpty()) body["PlaySessionId"] = m_currentPlaySessionId;
    QNetworkReply *reply = jellyfinPostJson(
        QUrl(serverUrl() + "/Sessions/Playing/Progress"),
        QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

void JellyfinBackend::report_playback_stopped(const QString &itemId,
                                              const QString &mediaSourceId,
                                              qint64 positionTicks, bool failed) {
    if (get_auth_state() != QLatin1String("authed")) return;
    const QString session = m_currentPlaySessionId;
    QJsonObject body{{"ItemId", itemId}, {"MediaSourceId", mediaSourceId},
                     {"PositionTicks", double(positionTicks)}, {"Failed", failed},
                     {"PlayMethod", m_currentPlayMethod}};
    if (!session.isEmpty()) body["PlaySessionId"] = session;
    QNetworkReply *reply = jellyfinPostJson(
        QUrl(serverUrl() + "/Sessions/Playing/Stopped"),
        QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, session]() {
        reply->deleteLater();
        if (m_currentPlaySessionId == session)
            m_currentPlaySessionId.clear();
    });
}

void JellyfinBackend::load_next_episode(const QString &currentItemId) {
    QUrl currentUrl(serverUrl() + "/Users/" + userId() + "/Items/" + currentItemId);
    QUrlQuery currentQuery;
    currentQuery.addQueryItem("fields", "SeriesId,IndexNumber,ParentIndexNumber");
    currentUrl.setQuery(currentQuery);
    QNetworkReply *currentReply = jellyfinGet(currentUrl);
    connect(currentReply, &QNetworkReply::finished, this, [this, currentReply]() {
        const QJsonObject current = QJsonDocument::fromJson(currentReply->readAll()).object();
        const auto error = currentReply->error();
        currentReply->deleteLater();
        const QString seriesId = current.value("SeriesId").toString();
        if (error != QNetworkReply::NoError || seriesId.isEmpty()) {
            emit nextEpisodeReady({});
            return;
        }

        const int currentSeason = current.value("ParentIndexNumber").toInt(-1);
        const int currentEpisode = current.value("IndexNumber").toInt(-1);
        QUrl episodesUrl(serverUrl() + "/Shows/" + seriesId + "/Episodes");
        QUrlQuery query;
        query.addQueryItem("userId", userId());
        query.addQueryItem("fields", detailItemFields() + ",SeriesId");
        query.addQueryItem("enableUserData", "true");
        query.addQueryItem("sortBy", "AiredEpisodeOrder");
        episodesUrl.setQuery(query);
        QNetworkReply *episodesReply = jellyfinGet(episodesUrl);
        connect(episodesReply, &QNetworkReply::finished, this,
                [this, episodesReply, currentSeason, currentEpisode]() {
            const QJsonArray episodes = QJsonDocument::fromJson(episodesReply->readAll())
                                            .object().value("Items").toArray();
            const auto error = episodesReply->error();
            episodesReply->deleteLater();
            if (error != QNetworkReply::NoError) {
                emit nextEpisodeReady({});
                return;
            }
            for (const QJsonValue &value : episodes) {
                const QJsonObject episode = value.toObject();
                const int season = episode.value("ParentIndexNumber").toInt(-1);
                const int index = episode.value("IndexNumber").toInt(-1);
                if (season > currentSeason || (season == currentSeason && index > currentEpisode)) {
                    emit nextEpisodeReady(formatItem(episode));
                    return;
                }
            }
            emit nextEpisodeReady({});
        });
    });
}

void JellyfinBackend::probeCapabilities() {
    if (m_capabilitiesProbed) {
        emit dynamicOptionsReady("_capabilities",
            m_hasMediaSegments ? QVariantList{QStringLiteral("mediasegments")} : QVariantList{});
        return;
    }
    QNetworkReply *reply = jellyfinGet(
        QUrl(serverUrl() + "/MediaSegments/00000000-0000-0000-0000-000000000000"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        if (status > 0) {
            m_capabilitiesProbed = true;
            m_hasMediaSegments = status != 404;
        }
        emit dynamicOptionsReady("_capabilities",
            m_hasMediaSegments ? QVariantList{QStringLiteral("mediasegments")} : QVariantList{});
    });
}

void JellyfinBackend::fetchSegments(const QString &itemId) {
    if (itemId.isEmpty()) return;
    QNetworkReply *reply = jellyfinGet(QUrl(serverUrl() + "/MediaSegments/" + itemId));
    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId]() {
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto error = reply->error();
        reply->deleteLater();
        if (status == 404) {
            m_capabilitiesProbed = true;
            m_hasMediaSegments = false;
            emit segmentsReady(itemId, {});
            return;
        }
        if (error != QNetworkReply::NoError) {
            emit segmentsReady(itemId, {});
            return;
        }
        m_capabilitiesProbed = true;
        m_hasMediaSegments = true;
        QVariantList segments;
        const QJsonArray items = QJsonDocument::fromJson(data).object().value("Items").toArray();
        for (const QJsonValue &value : items) {
            const QJsonObject item = value.toObject();
            const QString type = item.value("Type").toString();
            if (type != QLatin1String("Intro") && type != QLatin1String("Outro"))
                continue;
            segments << QVariantMap{
                {"type", type},
                {"startMs", item.value("StartTicks").toVariant().toLongLong() / 10000},
                {"endMs", item.value("EndTicks").toVariant().toLongLong() / 10000}
            };
        }
        emit segmentsReady(itemId, segments);
    });
}

void JellyfinBackend::set_last_track_languages(const QString &audioLanguage,
                                               const QString &subtitleLanguage) {
    m_lastAudioLanguage = audioLanguage;
    m_lastSubtitleLanguage = subtitleLanguage.isEmpty() ? QStringLiteral("__off__")
                                                        : subtitleLanguage;
    QJsonObject auth = loadAuth();
    auth["lastAudioLanguage"] = m_lastAudioLanguage;
    auth["lastSubtitleLanguage"] = m_lastSubtitleLanguage;
    saveAuth(auth);
}

bool JellyfinBackend::saveAuthenticationResult(const QJsonObject &result, const QString &baseUrl) {
    const QJsonObject user = result["User"].toObject();
    const QString token = result["AccessToken"].toString();
    const QString id = user["Id"].toString();
    if (token.isEmpty() || id.isEmpty())
        return false;

    QJsonObject auth = loadAuth();
    ++m_itemsLoadGeneration;
    m_itemsLoadAccumulator.clear();
    m_itemCache.clear();
    auth["serverUrl"] = baseUrl;
    auth["accessToken"] = token;
    auth["userId"] = id;
    auth["username"] = user["Name"].toString();
    auth["serverId"] = result["ServerId"].toString();
    auth["serverName"] = QUrl(baseUrl).host();
    saveAuth(auth);
    return true;
}

void JellyfinBackend::pollQuickConnect() {
    if (m_quickConnectServerUrl.isEmpty() || m_quickConnectSecret.isEmpty()) {
        cancel_quick_connect();
        return;
    }

    m_quickConnectTimer->stop();

    QUrl url(m_quickConnectServerUrl + "/QuickConnect/Connect");
    QUrlQuery query;
    query.addQueryItem("secret", m_quickConnectSecret);
    url.setQuery(query);

    QNetworkReply *reply = m_nam->get(jellyfinRequest(url, QString(), false));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError error = reply->error();
        reply->deleteLater();

        if (error != QNetworkReply::NoError || status < 200 || status >= 300) {
            cancel_quick_connect();
            emit errorOccurred(QString("Quick Connect polling failed (%1).").arg(status > 0 ? status : int(error)));
            return;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            cancel_quick_connect();
            emit errorOccurred("Quick Connect polling returned an invalid response.");
            return;
        }

        if (doc.object()["Authenticated"].toBool(false)) {
            authenticateWithQuickConnectSecret();
        } else if (!m_quickConnectSecret.isEmpty()) {
            m_quickConnectTimer->start();
        }
    });
}

void JellyfinBackend::authenticateWithQuickConnectSecret() {
    if (m_quickConnectServerUrl.isEmpty() || m_quickConnectSecret.isEmpty()) {
        emit errorOccurred("Quick Connect authentication is missing a secret.");
        return;
    }

    QJsonObject body;
    body["Secret"] = m_quickConnectSecret;

    const QString base = m_quickConnectServerUrl;
    QNetworkReply *reply = jellyfinPostJson(QUrl(base + "/Users/AuthenticateWithQuickConnect"),
                                            QJsonDocument(body).toJson(QJsonDocument::Compact),
                                            QString(),
                                            false);
    connect(reply, &QNetworkReply::finished, this, [this, reply, base]() {
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError error = reply->error();
        reply->deleteLater();

        if (error != QNetworkReply::NoError || status < 200 || status >= 300) {
            cancel_quick_connect();
            emit errorOccurred(QString("Quick Connect sign-in failed (%1).").arg(status > 0 ? status : int(error)));
            return;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            cancel_quick_connect();
            emit errorOccurred("Quick Connect sign-in returned an invalid response.");
            return;
        }

        if (!saveAuthenticationResult(doc.object(), base)) {
            cancel_quick_connect();
            emit errorOccurred("Quick Connect sign-in did not return a usable access token.");
            return;
        }

        cancel_quick_connect();
        emit authSuccess();
        emit authStateChanged();
    });
}

void JellyfinBackend::getVideoQualities() {
    QVariantList opts;
    opts.append(QVariantMap{{"id", "direct"}, {"label", "Direct Play"}});
    opts.append(QVariantMap{{"id", "480p"}, {"label", "480p"}});
    opts.append(QVariantMap{{"id", "576p"}, {"label", "576p"}});
    opts.append(QVariantMap{{"id", "720p"}, {"label", "720p"}});
    opts.append(QVariantMap{{"id", "1080p"}, {"label", "1080p"}});
    emit dynamicOptionsReady("video_quality", opts);
}

void JellyfinBackend::getLibraries() {
    QUrl url(serverUrl() + "/UserViews");
    QUrlQuery query;
    query.addQueryItem("userId", userId());
    query.addQueryItem("includeExternalContent", "false");
    query.addQueryItem("includeHidden", "false");
    url.setQuery(query);
    QNetworkReply *reply = jellyfinGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QJsonArray items = QJsonDocument::fromJson(reply->readAll())
                                     .object().value("Items").toArray();
        const auto error = reply->error();
        reply->deleteLater();
        QVariantList options;
        if (error == QNetworkReply::NoError) {
            for (const QJsonValue &value : items) {
                const QJsonObject item = value.toObject();
                options << QVariantMap{{"id", item.value("Id").toString()},
                                       {"label", item.value("Name").toString()}};
            }
        }
        emit dynamicOptionsReady("libraries", options);
    });
}

void JellyfinBackend::get_resume_playback_options() {
    QVariantList opts;
    opts.append(QVariantMap{{"id", "ask"}, {"label", "Ask"}});
    opts.append(QVariantMap{{"id", "yes"}, {"label", "Always Resume"}});
    opts.append(QVariantMap{{"id", "no"}, {"label", "Start Over"}});
    emit dynamicOptionsReady("resume_playback", opts);
}

QVariantMap JellyfinBackend::formatLibrary(const QJsonObject &item) const {
    QVariantMap out;
    out["id"] = item["Id"].toString();
    out["title"] = item["Name"].toString();
    out["collectionType"] = item["CollectionType"].toString();
    return out;
}

QString JellyfinBackend::itemTitle(const QJsonObject &item) {
    const QString type = item["Type"].toString();
    const QString name = item["Name"].toString();

    if (type == "Season") {
        if (!name.isEmpty())
            return name;

        const int seasonNumber = item["IndexNumber"].toInt(-1);
        if (seasonNumber == 0)
            return QStringLiteral("Specials");
        if (seasonNumber > 0)
            return QString("Season %1").arg(seasonNumber);
    }

    if (type == "Episode") {
        const QString code = episodeCode(item);
        if (!code.isEmpty() && !name.isEmpty())
            return QString("%1 - %2").arg(code, name);
        if (!code.isEmpty())
            return code;
    }

    return name;
}

QString JellyfinBackend::episodeCode(const QJsonObject &item) {
    const int seasonNumber = item["ParentIndexNumber"].toInt(-1);
    const int episodeNumber = item["IndexNumber"].toInt(-1);

    QString code;
    if (seasonNumber >= 0)
        code += QString("S%1").arg(seasonNumber, 2, 10, QChar('0'));
    if (episodeNumber >= 0)
        code += QString("E%1").arg(episodeNumber, 2, 10, QChar('0'));
    return code;
}

static QString jellyfinStreamLabel(const QJsonObject &stream, const QString &fallbackPrefix, int trackNumber) {
    QString label = stream["DisplayTitle"].toString().trimmed();
    if (label.isEmpty()) {
        const QString language = stream["Language"].toString().toUpper();
        const QString title = stream["Title"].toString().trimmed();
        const QString codec = stream["Codec"].toString().toUpper();
        const int channels = stream["Channels"].toInt();

        if (!language.isEmpty() && !title.isEmpty())
            label = language + " - " + title;
        else if (!title.isEmpty())
            label = title;
        else if (!language.isEmpty())
            label = language;
        else
            label = QString("%1 %2").arg(fallbackPrefix).arg(trackNumber);

        QStringList details;
        if (!codec.isEmpty())
            details << codec;
        if (channels > 0)
            details << QString("%1CH").arg(channels);
        if (!details.isEmpty())
            label += " (" + details.join(", ") + ")";
    }

    QStringList flags;
    if (stream["IsDefault"].toBool(false))
        flags << "DEFAULT";
    if (stream["IsForced"].toBool(false))
        flags << "FORCED";
    if (stream["IsExternal"].toBool(false))
        flags << "EXTERNAL";
    if (!flags.isEmpty())
        label += " [" + flags.join(", ") + "]";

    return label.toUpper();
}

static QString jellyfinSubtitleUrl(const QString &base, const QJsonObject &stream) {
    QString deliveryUrl = stream["DeliveryUrl"].toString();
    if (deliveryUrl.isEmpty())
        return {};

    QUrl url(deliveryUrl);
    if (url.isRelative()) {
        if (!deliveryUrl.startsWith('/'))
            deliveryUrl.prepend('/');
        url = QUrl(base + deliveryUrl);
    }

    QUrlQuery query(url);
    if (query.hasQueryItem("api_key")) {
        query.removeAllQueryItems("api_key");
        url.setQuery(query);
    }
    return url.toString();
}

QVariantMap JellyfinBackend::formatItem(const QJsonObject &item) const {
    QVariantMap out;
    const QString itemType = item["Type"].toString();
    out["id"] = item["Id"].toString();
    out["title"] = itemTitle(item);
    out["name"] = item["Name"].toString();
    out["type"] = itemType.toLower();
    out["summary"] = item["Overview"].toString();
    out["year"] = item["ProductionYear"].toInt();
    out["duration"] = ticksToMs(item["RunTimeTicks"].toVariant().toLongLong());
    out["officialRating"] = item["OfficialRating"].toString();
    out["communityRating"] = item["CommunityRating"].toVariant();
    out["status"] = item["Status"].toString();
    out["genres"] = item["Genres"].toArray().toVariantList();
    out["childCount"] = item["ChildCount"].toInt();
    out["recursiveItemCount"] = item["RecursiveItemCount"].toInt();
    out["seriesName"] = item["SeriesName"].toString();
    out["seasonName"] = item["SeasonName"].toString();
    out["index"] = item["IndexNumber"].toInt(-1);
    out["indexNumber"] = item["IndexNumber"].toInt(-1);
    out["parentIndex"] = item["ParentIndexNumber"].toInt(-1);
    out["parentIndexNumber"] = item["ParentIndexNumber"].toInt(-1);
    out["episodeCode"] = itemType == "Episode" ? episodeCode(item) : QString();
    out["seriesId"] = item["SeriesId"].toString();
    out["isFolder"] = item["IsFolder"].toBool(false);
    out["premiereDate"] = item["PremiereDate"].toString();

    const QJsonObject userData = item["UserData"].toObject();
    out["viewOffset"] = ticksToMs(userData["PlaybackPositionTicks"].toVariant().toLongLong());
    out["played"] = userData["Played"].toBool(false);

    const QJsonArray mediaSources = item["MediaSources"].toArray();
    QJsonArray streams = item["MediaStreams"].toArray();
    if (!mediaSources.isEmpty()) {
        const QJsonObject mediaSource = mediaSources.first().toObject();
        out["mediaSourceId"] = mediaSource["Id"].toString();
        const QJsonArray mediaSourceStreams = mediaSource["MediaStreams"].toArray();
        if (!mediaSourceStreams.isEmpty())
            streams = mediaSourceStreams;
    }

    QVariantList audioStreams;
    QVariantList subtitleStreams;
    int audioTrack = 0;
    int embeddedSubtitleTrack = 0;
    int subtitleTrack = 0;
    QString base;

    for (const QJsonValue &value : streams) {
        const QJsonObject stream = value.toObject();
        const QString type = stream["Type"].toString();
        if (type.compare("Audio", Qt::CaseInsensitive) == 0) {
            ++audioTrack;
            audioStreams.append(QVariantMap{
                {"id", QString::number(stream["Index"].toInt(audioTrack))},
                {"streamIndex", stream["Index"].toInt(-1)},
                {"mpvTrack", audioTrack},
                {"isDefault", stream["IsDefault"].toBool(false)},
                {"language", stream["Language"].toString()},
                {"title", stream["Title"].toString()},
                {"codec", stream["Codec"].toString()},
                {"channels", stream["Channels"].toInt()},
                {"displayTitle", jellyfinStreamLabel(stream, "AUDIO", audioTrack)}
            });
        } else if (type.compare("Subtitle", Qt::CaseInsensitive) == 0) {
            ++subtitleTrack;
            const bool isExternal = stream["IsExternal"].toBool(false);
            QString subUrl;
            int mpvTrack = 0;
            if (isExternal) {
                if (base.isEmpty())
                    base = serverUrl();
                subUrl = jellyfinSubtitleUrl(base, stream);
                if (subUrl.isEmpty())
                    continue;
            } else {
                ++embeddedSubtitleTrack;
                mpvTrack = embeddedSubtitleTrack;
            }

            subtitleStreams.append(QVariantMap{
                {"id", QString::number(stream["Index"].toInt(subtitleTrack))},
                {"streamIndex", stream["Index"].toInt(-1)},
                {"mpvTrack", mpvTrack},
                {"displayTitle", jellyfinStreamLabel(stream, "SUBTITLE", subtitleTrack)},
                {"isExternal", isExternal},
                {"isDefault", stream["IsDefault"].toBool(false)},
                {"isForced", stream["IsForced"].toBool(false)},
                {"language", stream["Language"].toString()},
                {"title", stream["Title"].toString()},
                {"codec", stream["Codec"].toString()},
                {"subFile", subUrl}
            });
        }
    }

    out["audioStreams"] = audioStreams;
    out["subtitleStreams"] = subtitleStreams;

    return out;
}

int JellyfinBackend::ticksToMs(qint64 ticks) {
    if (ticks <= 0)
        return 0;
    return int(ticks / 10000);
}
