#include "YouTubePolicy.h"

#include "HelperResolver.h"

#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace {

QStringList directBaseArguments(const QString &appRoot)
{
    const QString deno = HelperResolver::deno(appRoot);
    if (deno.isEmpty())
        return {};

    return {
        QStringLiteral("--no-config"),
        QStringLiteral("--no-update"),
        QStringLiteral("--no-cache-dir"),
        QStringLiteral("--no-warnings"),
        QStringLiteral("--no-progress"),
        QStringLiteral("--js-runtimes"),
        QStringLiteral("deno:") + deno
    };
}

}

namespace YouTubePolicy {

QString profileName(MediaProfile profile)
{
    switch (profile) {
    case MediaProfile::Video720p:
        return QStringLiteral("video720p");
    case MediaProfile::AudioOnly:
        return QStringLiteral("audioOnly");
    case MediaProfile::None:
        return {};
    }
    return {};
}

MediaProfile profileFromName(const QString &name)
{
    const QString normalized = name.trimmed().toLower();
    if (normalized == QLatin1String("video720p"))
        return MediaProfile::Video720p;
    if (normalized == QLatin1String("audioonly"))
        return MediaProfile::AudioOnly;
    return MediaProfile::None;
}

QString formatSelector(MediaProfile profile)
{
    switch (profile) {
    case MediaProfile::Video720p:
        return QStringLiteral(
            "bestvideo[ext=mp4][height<=720]+bestaudio[ext=m4a]/"
            "best[ext=mp4][height<=720]/best[height<=720]/best");
    case MediaProfile::AudioOnly:
        return QStringLiteral("bestaudio/best");
    case MediaProfile::None:
        return {};
    }
    return {};
}

bool isValidVideoId(const QString &videoId)
{
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_-]{11}$"));
    return valid.match(videoId).hasMatch();
}

QString canonicalVideoUrl(const QString &videoId)
{
    return isValidVideoId(videoId)
        ? QStringLiteral("https://www.youtube.com/watch?v=") + videoId
        : QString{};
}

bool isCanonicalVideoUrl(const QString &url)
{
    const QUrl parsed(url, QUrl::StrictMode);
    if (!parsed.isValid() || parsed.scheme() != QLatin1String("https") ||
        parsed.host() != QLatin1String("www.youtube.com") ||
        parsed.path() != QLatin1String("/watch") || parsed.hasFragment()) {
        return false;
    }
    const auto items = QUrlQuery(parsed).queryItems(QUrl::FullyDecoded);
    return items.size() == 1 && items.first().first == QLatin1String("v") &&
           isValidVideoId(items.first().second);
}

QString canonicalPlaylistUrl(const QString &input)
{
    QString value = input.trimmed();
    if (value.startsWith(QLatin1String("www.youtube.com/"), Qt::CaseInsensitive) ||
        value.startsWith(QLatin1String("youtube.com/"), Qt::CaseInsensitive) ||
        value.startsWith(QLatin1String("m.youtube.com/"), Qt::CaseInsensitive) ||
        value.startsWith(QLatin1String("music.youtube.com/"), Qt::CaseInsensitive) ||
        value.startsWith(QLatin1String("youtu.be/"), Qt::CaseInsensitive)) {
        value.prepend(QStringLiteral("https://"));
    }
    if (value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r')))
        return {};

    const QUrl url(value, QUrl::StrictMode);
    if (!url.isValid() ||
        url.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) != 0) {
        return {};
    }
    const QString host = url.host().toLower();
    const bool youtubeHost = host == QLatin1String("youtube.com") ||
        host == QLatin1String("www.youtube.com") ||
        host == QLatin1String("m.youtube.com") ||
        host == QLatin1String("music.youtube.com");
    const bool shortHost = host == QLatin1String("youtu.be") ||
        host == QLatin1String("www.youtu.be");
    if ((!youtubeHost && !shortHost) || url.hasFragment())
        return {};
    if (youtubeHost && url.path() != QLatin1String("/playlist") &&
        url.path() != QLatin1String("/watch")) {
        return {};
    }

    const QString playlistId = QUrlQuery(url).queryItemValue(
        QStringLiteral("list"), QUrl::FullyDecoded).trimmed();
    static const QRegularExpression validPlaylistId(
        QStringLiteral("^[A-Za-z0-9_-]{10,128}$"));
    if (!validPlaylistId.match(playlistId).hasMatch())
        return {};

    QUrl canonical(QStringLiteral("https://www.youtube.com/playlist"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("list"), playlistId);
    canonical.setQuery(query);
    return canonical.toString(QUrl::FullyEncoded);
}

QStringList mpvArguments(const QString &appRoot, MediaProfile profile)
{
    if (profile == MediaProfile::None)
        return {};

    QStringList arguments{QStringLiteral("--no-config")};
    const QString ytDlp = HelperResolver::ytDlp(appRoot);
    const QString deno = HelperResolver::deno(appRoot);
    if (!ytDlp.isEmpty()) {
        arguments << QStringLiteral("--script-opts-append=ytdl_hook-ytdl_path=") + ytDlp
                  << QStringLiteral("--ytdl-raw-options-append=ignore-config=")
                  << QStringLiteral("--ytdl-raw-options-append=no-update=")
                  << QStringLiteral("--ytdl-raw-options-append=no-cache-dir=")
                  << QStringLiteral("--ytdl-raw-options-append=check-formats=");
    }
    if (!deno.isEmpty()) {
        arguments << QStringLiteral("--ytdl-raw-options-append=js-runtimes=deno:") + deno;
    }
    arguments << QStringLiteral("--ytdl-format=") + formatSelector(profile);
    if (profile == MediaProfile::AudioOnly)
        arguments << QStringLiteral("--no-video");
    return arguments;
}

QStringList playlistInventoryArguments(const QString &appRoot,
                                       const QStringList &urls,
                                       int maximumEntries)
{
    QStringList arguments = directBaseArguments(appRoot);
    if (arguments.isEmpty() || urls.isEmpty())
        return {};
    arguments << QStringLiteral("--ignore-errors")
              << QStringLiteral("--flat-playlist")
              << QStringLiteral("--lazy-playlist")
              << QStringLiteral("--dump-json");
    if (maximumEntries > 0) {
        arguments << QStringLiteral("--playlist-end")
                  << QString::number(maximumEntries);
    }
    arguments << urls;
    return arguments;
}

QStringList videoPrefetchArguments(const QString &appRoot,
                                   const QString &outputTemplate,
                                   const QString &videoId)
{
    QStringList arguments = directBaseArguments(appRoot);
    const QString ffmpeg = HelperResolver::ffmpeg(appRoot);
    const QString url = canonicalVideoUrl(videoId);
    if (arguments.isEmpty() || ffmpeg.isEmpty() || outputTemplate.isEmpty() || url.isEmpty())
        return {};
    arguments << QStringLiteral("--no-playlist")
              << QStringLiteral("--ffmpeg-location")
              << ffmpeg
              << QStringLiteral("--format")
              << formatSelector(MediaProfile::Video720p)
              << QStringLiteral("--merge-output-format")
              << QStringLiteral("mp4")
              << QStringLiteral("--output")
              << outputTemplate
              << url;
    return arguments;
}

QString safeHelperError(const QByteArray &errorOutput)
{
    QString text = QString::fromUtf8(errorOutput).trimmed();
    if (text.isEmpty())
        return {};
    text.replace(QRegularExpression(QStringLiteral("https?://\\S+")),
                 QStringLiteral("[URL]"));
    text.replace(QRegularExpression(QStringLiteral("(?i)(authorization|cookie|token)\\s*[:=]\\s*\\S+")),
                 QStringLiteral("\\1=[REDACTED]"));
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    text = text.simplified();
    if (text.size() > 320)
        text = text.left(317) + QStringLiteral("...");
    return text;
}

}
