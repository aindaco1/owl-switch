#include "NatureSoundtrack.h"
#include "AppCore.h"
#include "player/AudioCrossfadePlayer.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <cmath>

namespace {
constexpr qint64 kResponseLimit = 8 * 1024 * 1024;
constexpr qint64 kCacheLimit = 4 * 1024 * 1024;
const QString kModule = QStringLiteral("com.owlswitch.nature");
const QString kLicense = QStringLiteral("https://creativecommons.org/publicdomain/zero/1.0/");

QString displayText(const QJsonValue &value)
{
    if (!value.isString())
        return {};
    QString result;
    for (const QChar c : value.toString()) {
        if (c.isPrint() && !c.isNull())
            result.append(c);
        else if (c.isSpace())
            result.append(QLatin1Char(' '));
        if (result.size() >= 200)
            break;
    }
    return result.simplified();
}
}

QUrl NatureSoundtrack::catalogEndpoint()
{
    return QUrl(QStringLiteral("https://earth-garden-sound-worker.workers2000.workers.dev/api/manifest"));
}

NatureSoundtrack::NatureSoundtrack(const QString &appRoot, const QString &dataRoot,
                                 AppCore *appCore, QObject *parent)
    : QObject(parent), m_cachePath(QDir(dataRoot).filePath("nature_sounds.json")),
      m_appCore(appCore), m_endpoint(catalogEndpoint()),
      m_player(new AudioCrossfadePlayer(appRoot, this))
{
    connect(m_player, &AudioCrossfadePlayer::trackChanged, this, [this](const QVariantMap &track) {
        m_current = track;
        emit currentSoundChanged();
    });
    connect(m_player, &AudioCrossfadePlayer::activeChanged, this, [this] {
        emit activeChanged();
        if (!active())
            QTimer::singleShot(0, this, &NatureSoundtrack::tryStart);
    });
    connect(m_player, &AudioCrossfadePlayer::unavailable, this, [this] {
        m_failed = true;
        emit statusChanged(QStringLiteral("NATURE SOUND UNAVAILABLE / SLIDESHOW CONTINUES"));
    });
    if (m_appCore) {
        const QVariant volume = m_appCore->get_setting(kModule, QStringLiteral("audio_volume"));
        m_player->setVolume(volume.isValid() ? volume.toInt() : 30);
    }
}

bool NatureSoundtrack::active() const { return m_player->active(); }
QString NatureSoundtrack::sourceUrl() const { return m_current.value("sourceUrl").toString(); }
QString NatureSoundtrack::currentTitle() const { return m_current.value("title").toString(); }

bool NatureSoundtrack::enabled() const
{
    if (!m_appCore)
        return true;
    const QVariant value = m_appCore->get_setting(kModule, QStringLiteral("ambient_audio"));
    if (value.metaType().id() == QMetaType::Bool)
        return value.toBool();
    const QString text = value.toString().trimmed().toLower();
    return text != QLatin1String("off") && text != QLatin1String("false") &&
           text != QLatin1String("0");
}

QVariantList NatureSoundtrack::parseCatalog(const QByteArray &payload)
{
    if (payload.size() > kResponseLimit)
        return {};
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    const QJsonObject root = document.object();
    if (root.value("version") != QJsonValue("v1") ||
        root.value("source") != QJsonValue("freesound") || !root.value("tracks").isArray())
        return {};
    const QJsonArray tracks = root.value("tracks").toArray();
    if (tracks.size() > 5000)
        return {};
    static const QRegularExpression idPattern(QStringLiteral("^[1-9][0-9]{0,9}$"));
    static const QRegularExpression previewPattern(
        QStringLiteral("^https://cdn\\.freesound\\.org/previews/([0-9]+)/([1-9][0-9]*)_([0-9]+)-hq\\.mp3$"));
    static const QRegularExpression uploaderPattern(QStringLiteral("^[A-Za-z0-9_.-]{1,100}$"));
    QSet<QString> seen;
    QVariantList result;
    for (const QJsonValue &value : tracks) {
        const QJsonObject item = value.toObject();
        const QString id = item.value("id").toString();
        const QString preview = item.value("previewUrl").toString();
        const QString uploader = item.value("uploader").toString();
        const double duration = item.value("durationSec").toDouble(-1);
        const auto match = previewPattern.match(preview);
        if (item.value("source") != QJsonValue("freesound") ||
            item.value("license") != QJsonValue("cc0") ||
            item.value("licenseUrl") != QJsonValue(kLicense) ||
            !idPattern.match(id).hasMatch() || !match.hasMatch() ||
            match.captured(2) != id ||
            match.captured(1) != QString::number(id.toLongLong() / 1000) ||
            !uploaderPattern.match(uploader).hasMatch() ||
            !std::isfinite(duration) || duration < 30 || duration > 600 || seen.contains(id))
            continue;
        const QString title = displayText(item.value("title"));
        if (title.isEmpty())
            continue;
        seen.insert(id);
        result.append(QVariantMap{{"id", id}, {"source", "freesound"}, {"title", title},
            {"uploader", uploader}, {"previewUrl", preview}, {"durationSec", duration},
            {"license", "cc0"}, {"licenseUrl", kLicense},
            {"sourceUrl", QString("https://freesound.org/people/%1/sounds/%2/").arg(uploader, id)}});
        if (result.size() == 2500)
            break;
    }
    return result;
}

void NatureSoundtrack::prepare()
{
    if (!enabled())
        return;
    if (m_tracks.isEmpty())
        readCache();
    if (m_tracks.isEmpty() || m_fetchedAt.secsTo(QDateTime::currentDateTimeUtc()) >= 86400)
        refresh();
}

void NatureSoundtrack::start()
{
    if (m_wanted)
        return;
    m_wanted = true;
    emit requestedChanged();
    m_failed = false;
    prepare();
    tryStart();
}

void NatureSoundtrack::tryStart()
{
    if (m_wanted && enabled() && !m_failed && !active() && !m_tracks.isEmpty()) {
        m_player->setPaused(m_paused);
        m_player->start(m_tracks);
    }
}

void NatureSoundtrack::stop()
{
    if (m_wanted) {
        m_wanted = false;
        emit requestedChanged();
    }
    m_failed = false;
    cancelRequest();
    m_player->stop();
    m_current.clear();
    emit currentSoundChanged();
}

void NatureSoundtrack::setPaused(bool paused)
{
    m_paused = paused;
    m_player->setPaused(paused);
}

void NatureSoundtrack::settingChanged(const QString &key, const QVariant &value)
{
    if (key == QLatin1String("audio_volume"))
        m_player->setVolume(value.toInt());
    if (key == QLatin1String("ambient_audio")) {
        if (!enabled()) {
            cancelRequest();
            m_player->stop();
        } else {
            m_failed = false;
            prepare();
            // A quick off/on may still be in the bounded release fade.
            QTimer::singleShot(250, this, &NatureSoundtrack::tryStart);
        }
    }
}

void NatureSoundtrack::cancelRequest()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply.clear();
    }
    m_payload.clear();
}

void NatureSoundtrack::refresh()
{
    if (m_reply || QDateTime::currentDateTimeUtc() < m_retryAfter)
        return;
    QNetworkRequest request(m_endpoint);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", QStringLiteral("OwlSwitch/%1 (+https://github.com/aindaco1/owl-switch)")
        .arg(QCoreApplication::applicationVersion()).toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setTransferTimeout(15000);
    m_payload.clear();
    QNetworkReply *reply = m_network.get(request);
    m_reply = reply;
    reply->setReadBufferSize(64 * 1024);
    QTimer::singleShot(15000, reply, [reply] { if (reply->isRunning()) reply->abort(); });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        m_payload += reply->read(kResponseLimit + 1 - m_payload.size());
        if (m_payload.size() > kResponseLimit)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_reply.clear();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status == 200;
        const QByteArray retryHeader = reply->rawHeader("Retry-After");
        bool numeric = false;
        qint64 retrySeconds = retryHeader.toLongLong(&numeric);
        if (!numeric) {
            const QDateTime retryDate = QDateTime::fromString(QString::fromLatin1(retryHeader), Qt::RFC2822Date);
            retrySeconds = QDateTime::currentDateTimeUtc().secsTo(retryDate);
        }
        const qint64 retry = qBound(qint64(60), retrySeconds, qint64(86400));
        if (ok)
            m_payload += reply->read(kResponseLimit + 1 - m_payload.size());
        reply->deleteLater();
        const QVariantList tracks = ok ? parseCatalog(m_payload) : QVariantList{};
        m_payload.clear();
        if (tracks.isEmpty()) {
            m_retryAfter = QDateTime::currentDateTimeUtc().addSecs(retry);
            if (m_wanted && m_tracks.isEmpty())
                emit statusChanged(QStringLiteral("NATURE SOUND UNAVAILABLE / SLIDESHOW CONTINUES"));
            return;
        }
        m_tracks = tracks;
        m_fetchedAt = QDateTime::currentDateTimeUtc();
        writeCache();
        tryStart();
    });
}

bool NatureSoundtrack::readCache()
{
    QFile file(m_cachePath);
    if (file.size() <= 0 || file.size() > kCacheLimit || !file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray payload = file.read(kCacheLimit + 1);
    const QJsonObject root = QJsonDocument::fromJson(payload).object();
    const QDateTime fetched = QDateTime::fromString(root.value("fetchedAt").toString(), Qt::ISODate);
    if (root.value("schemaVersion").toInt() != 1 || !fetched.isValid() ||
        fetched > QDateTime::currentDateTimeUtc().addSecs(300))
        return false;
    const QVariantList tracks = parseCatalog(payload);
    if (tracks.isEmpty())
        return false;
    m_tracks = tracks;
    m_fetchedAt = fetched;
    return true;
}

void NatureSoundtrack::writeCache()
{
    const QJsonObject root{{"schemaVersion", 1}, {"version", "v1"}, {"source", "freesound"},
        {"fetchedAt", m_fetchedAt.toUTC().toString(Qt::ISODate)},
        {"tracks", QJsonArray::fromVariantList(m_tracks)}};
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (payload.size() > kCacheLimit || !QDir().mkpath(QFileInfo(m_cachePath).absolutePath()))
        return;
    QSaveFile file(m_cachePath);
    file.setDirectWriteFallback(false);
    if (file.open(QIODevice::WriteOnly)) {
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        if (file.write(payload) == payload.size())
            file.commit();
    }
}
