#include "TumblrScreensaverBackend.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QVariantMap>
#include <QDebug>
#include <QCoreApplication>

namespace {

constexpr int kPageSize = 50;

struct ImageCandidate {
    QString url;
    int width = 0;
    int height = 0;
};

QString cleanedUrl(QString url)
{
    url = url.trimmed();
    url.replace(QStringLiteral("&amp;"), QStringLiteral("&"));

    QUrl parsed(url);
    QString path = parsed.path();
    if (path.endsWith(QLatin1String(".gifv"), Qt::CaseInsensitive)) {
        path.chop(1);
        parsed.setPath(path);
        url = parsed.toString();
    }
    return url;
}

bool isSupportedImageUrl(const QString &url)
{
    QUrl parsed(url);
    if (!parsed.isValid() || parsed.host().isEmpty())
        return false;
    const QString scheme = parsed.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        return false;

    const QString path = parsed.path().toLower();
    return path.endsWith(QLatin1String(".jpg")) ||
           path.endsWith(QLatin1String(".jpeg")) ||
           path.endsWith(QLatin1String(".png")) ||
           path.endsWith(QLatin1String(".gif")) ||
           path.endsWith(QLatin1String(".webp"));
}

QString slugToTitle(QString slug)
{
    slug.replace(QLatin1Char('-'), QLatin1Char(' '));
    return slug.trimmed();
}

QString attributeValue(const QString &tag, const QString &attribute)
{
    const QRegularExpression re(
        QStringLiteral("\\b%1\\s*=\\s*([\"'])(.*?)\\1").arg(QRegularExpression::escape(attribute)),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = re.match(tag);
    return match.hasMatch() ? match.captured(2) : QString{};
}

int intAttribute(const QString &tag, const QString &attribute)
{
    bool ok = false;
    const int value = attributeValue(tag, attribute).toInt(&ok);
    return ok ? value : 0;
}

bool isGifUrl(const QString &url)
{
    return QUrl(url).path().endsWith(QLatin1String(".gif"), Qt::CaseInsensitive);
}

ImageCandidate bestSrcsetCandidate(const QString &srcset)
{
    ImageCandidate best;
    const QStringList parts = srcset.split(QLatin1Char(','), Qt::SkipEmptyParts);
    const QRegularExpression candidateRe(QStringLiteral("^(\\S+)\\s+(\\d+)w$"));

    for (const QString &rawPart : parts) {
        const QString part = rawPart.trimmed();
        const QRegularExpressionMatch match = candidateRe.match(part);
        if (!match.hasMatch())
            continue;

        const QString url = cleanedUrl(match.captured(1));
        if (!isSupportedImageUrl(url))
            continue;

        const int width = match.captured(2).toInt();
        if (width > best.width) {
            best.url = url;
            best.width = width;
        }
    }

    return best;
}

QVector<ImageCandidate> imagesFromHtml(const QString &html)
{
    QVector<ImageCandidate> result;
    const QRegularExpression imgRe(QStringLiteral("<img\\b[^>]*>"),
                                   QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = imgRe.globalMatch(html);

    while (it.hasNext()) {
        const QString tag = it.next().captured(0);
        const QString src = cleanedUrl(attributeValue(tag, QStringLiteral("src")));
        const QString srcset = attributeValue(tag, QStringLiteral("srcset"));

        ImageCandidate candidate;
        if (isGifUrl(src)) {
            candidate.url = src;
        } else {
            candidate = bestSrcsetCandidate(srcset);
            if (candidate.url.isEmpty())
                candidate.url = src;
        }

        if (!isSupportedImageUrl(candidate.url))
            continue;

        if (candidate.width <= 0)
            candidate.width = intAttribute(tag, QStringLiteral("data-orig-width"));
        if (candidate.height <= 0)
            candidate.height = intAttribute(tag, QStringLiteral("data-orig-height"));
        result.append(candidate);
    }

    return result;
}

QVector<ImageCandidate> imagesFromPhotoFields(const QJsonObject &post)
{
    QVector<ImageCandidate> result;
    ImageCandidate best;
    ImageCandidate bestGif;

    for (auto it = post.constBegin(); it != post.constEnd(); ++it) {
        const QString key = it.key();
        if (!key.startsWith(QLatin1String("photo-url-")) || !it.value().isString())
            continue;

        bool ok = false;
        const int width = key.mid(QStringLiteral("photo-url-").size()).toInt(&ok);
        if (!ok)
            continue;

        const QString url = cleanedUrl(it.value().toString());
        if (!isSupportedImageUrl(url))
            continue;

        if (isGifUrl(url) && width > bestGif.width) {
            bestGif.url = url;
            bestGif.width = width;
        }
        if (width > best.width) {
            best.url = url;
            best.width = width;
        }
    }

    if (!bestGif.url.isEmpty())
        result.append(bestGif);
    else if (!best.url.isEmpty())
        result.append(best);

    const QJsonArray photos = post.value(QStringLiteral("photos")).toArray();
    for (const QJsonValue &photoValue : photos) {
        if (!photoValue.isObject())
            continue;
        const QVector<ImageCandidate> nested = imagesFromPhotoFields(photoValue.toObject());
        for (const ImageCandidate &candidate : nested)
            result.append(candidate);
    }

    return result;
}

QStringList htmlFieldsFromPost(const QJsonObject &post)
{
    static const QStringList kHtmlKeys = {
        QStringLiteral("regular-body"),
        QStringLiteral("photo-caption"),
        QStringLiteral("body"),
        QStringLiteral("caption"),
        QStringLiteral("description"),
        QStringLiteral("answer"),
        QStringLiteral("question")
    };

    QStringList fields;
    for (const QString &key : kHtmlKeys) {
        const QString value = post.value(key).toString();
        if (value.contains(QLatin1String("<img"), Qt::CaseInsensitive))
            fields.append(value);
    }
    return fields;
}

QJsonObject parseTumblrJsonp(const QByteArray &payload, QString *error)
{
    const QString text = QString::fromUtf8(payload).trimmed();
    const qsizetype objectStart = text.indexOf(QLatin1Char('{'));
    const qsizetype objectEnd = text.lastIndexOf(QLatin1Char('}'));
    if (objectStart < 0 || objectEnd <= objectStart) {
        if (error)
            *error = QStringLiteral("Tumblr returned an unreadable feed.");
        return {};
    }

    QJsonParseError parseError;
    const QByteArray json = text.mid(objectStart, objectEnd - objectStart + 1).toUtf8();
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("Tumblr feed JSON could not be parsed.");
        return {};
    }

    return doc.object();
}

} // namespace

TumblrScreensaverBackend::TumblrScreensaverBackend(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void TumblrScreensaverBackend::loadImages(const QString &tumblrUrl)
{
    ++m_generation;
    m_postsSeen = 0;
    m_totalPosts = -1;
    m_blogTitle.clear();
    m_images.clear();
    m_seenUrls.clear();

    const QUrl blogUrl = normalizedBlogUrl(tumblrUrl);
    if (!blogUrl.isValid() || blogUrl.host().isEmpty()) {
        failLoad(QStringLiteral("Enter a valid Tumblr URL."));
        return;
    }

    emit loadStarted(blogUrl.toString(QUrl::RemoveQuery | QUrl::RemoveFragment));
    fetchPage(blogUrl, 0, m_generation);
}

QString TumblrScreensaverBackend::normalizeBlogUrl(const QString &tumblrUrl) const
{
    const QUrl normalized = normalizedBlogUrl(tumblrUrl);
    return normalized.isValid() && !normalized.host().isEmpty()
        ? normalized.toString(QUrl::RemoveQuery | QUrl::RemoveFragment)
        : QString();
}

QUrl TumblrScreensaverBackend::normalizedBlogUrl(const QString &tumblrUrl) const
{
    QString input = tumblrUrl.trimmed();
    if (input.isEmpty())
        return {};

    if (!input.contains(QLatin1String("://")))
        input.prepend(QStringLiteral("https://"));

    QUrl url(input);
    QString host = url.host().toLower();
    if (host.isEmpty())
        return {};

    if (!host.contains(QLatin1Char('.'))) {
        host.append(QStringLiteral(".tumblr.com"));
    } else if (host == QLatin1String("tumblr.com") || host == QLatin1String("www.tumblr.com")) {
        QString path = url.path();
        while (path.startsWith(QLatin1Char('/')))
            path.remove(0, 1);
        const QString blogName = path.section(QLatin1Char('/'), 0, 0).trimmed();
        if (blogName.isEmpty())
            return {};
        host = blogName + QStringLiteral(".tumblr.com");
    }

    QUrl normalized;
    normalized.setScheme(QStringLiteral("https"));
    normalized.setHost(host);
    normalized.setPath(QStringLiteral("/"));
    return normalized;
}

QUrl TumblrScreensaverBackend::apiUrlForPage(const QUrl &blogUrl, int start) const
{
    QUrl apiUrl = blogUrl;
    apiUrl.setPath(QStringLiteral("/api/read/json"));

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("num"), QString::number(kPageSize));
    query.addQueryItem(QStringLiteral("start"), QString::number(start));
    apiUrl.setQuery(query);
    return apiUrl;
}

void TumblrScreensaverBackend::fetchPage(const QUrl &blogUrl, int start, int generation)
{
    QNetworkRequest request(apiUrlForPage(blogUrl, start));
    request.setRawHeader("Accept", "application/json,text/javascript,*/*");
    request.setRawHeader("User-Agent",
                         QStringLiteral("OwlSwitch/%1")
                             .arg(QCoreApplication::applicationVersion()).toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, blogUrl, start, generation]() {
        handlePage(reply, blogUrl, start, generation);
    });
}

void TumblrScreensaverBackend::handlePage(QNetworkReply *reply, const QUrl &blogUrl, int start, int generation)
{
    reply->deleteLater();

    if (generation != m_generation)
        return;

    if (reply->error() != QNetworkReply::NoError) {
        qWarning("[TumblrScreensaver] Feed request failed: %s", qPrintable(reply->errorString()));
        failLoad(QStringLiteral("Could not load that Tumblr feed."));
        return;
    }

    QString parseError;
    const QJsonObject root = parseTumblrJsonp(reply->readAll(), &parseError);
    if (root.isEmpty()) {
        failLoad(parseError);
        return;
    }

    if (start == 0) {
        m_blogTitle = root.value(QStringLiteral("tumblelog")).toObject()
                          .value(QStringLiteral("title")).toString();
        m_totalPosts = root.value(QStringLiteral("posts-total")).toInt(-1);
    }

    const QJsonArray posts = root.value(QStringLiteral("posts")).toArray();
    for (const QJsonValue &postValue : posts) {
        if (!postValue.isObject())
            continue;

        const QVariantList found = imagesFromPost(postValue.toObject());
        for (const QVariant &imageValue : found) {
            const QVariantMap image = imageValue.toMap();
            const QString url = image.value(QStringLiteral("url")).toString();
            if (url.isEmpty() || m_seenUrls.contains(url))
                continue;
            m_seenUrls.insert(url);
            m_images.append(image);
        }
    }

    m_postsSeen += posts.size();
    emit loadProgress(m_images.size(), m_postsSeen, m_totalPosts);

    if (posts.isEmpty() || (m_totalPosts >= 0 && m_postsSeen >= m_totalPosts)) {
        finishLoad();
        return;
    }

    fetchPage(blogUrl, start + posts.size(), generation);
}

QVariantList TumblrScreensaverBackend::imagesFromPost(const QJsonObject &post) const
{
    QVariantList result;

    QString title = post.value(QStringLiteral("regular-title")).toString().trimmed();
    if (title.isEmpty())
        title = slugToTitle(post.value(QStringLiteral("slug")).toString());

    const QString postUrl = post.value(QStringLiteral("url-with-slug")).toString(
        post.value(QStringLiteral("url")).toString());

    QVector<ImageCandidate> candidates;
    const QStringList htmlFields = htmlFieldsFromPost(post);
    for (const QString &html : htmlFields) {
        const QVector<ImageCandidate> htmlImages = imagesFromHtml(html);
        for (const ImageCandidate &candidate : htmlImages)
            candidates.append(candidate);
    }

    if (candidates.isEmpty())
        candidates = imagesFromPhotoFields(post);

    for (const ImageCandidate &candidate : candidates) {
        QVariantMap image;
        image[QStringLiteral("url")] = candidate.url;
        image[QStringLiteral("postUrl")] = postUrl;
        image[QStringLiteral("title")] = title;
        image[QStringLiteral("width")] = candidate.width;
        image[QStringLiteral("height")] = candidate.height;
        image[QStringLiteral("animated")] = isGifUrl(candidate.url);
        result.append(image);
    }

    return result;
}

void TumblrScreensaverBackend::finishLoad()
{
    if (m_images.isEmpty()) {
        failLoad(QStringLiteral("No images were found on that Tumblr."));
        return;
    }

    emit imagesLoaded(m_images, m_blogTitle, m_totalPosts);
}

void TumblrScreensaverBackend::failLoad(const QString &message)
{
    emit errorOccurred(message);
}
