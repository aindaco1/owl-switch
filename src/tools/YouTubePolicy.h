#pragma once

#include <QString>
#include <QStringList>

namespace YouTubePolicy {

enum class MediaProfile {
    None,
    Video720p,
    AudioOnly
};

QString profileName(MediaProfile profile);
MediaProfile profileFromName(const QString &name);
QString formatSelector(MediaProfile profile);

bool isValidVideoId(const QString &videoId);
QString canonicalVideoUrl(const QString &videoId);
bool isCanonicalVideoUrl(const QString &url);
QString canonicalPlaylistUrl(const QString &input);

QStringList mpvArguments(const QString &appRoot, MediaProfile profile);
QStringList playlistInventoryArguments(const QString &appRoot,
                                       const QStringList &urls,
                                       int maximumEntries = 0);
QStringList videoPrefetchArguments(const QString &appRoot,
                                   const QString &outputTemplate,
                                   const QString &videoId);

QString safeHelperError(const QByteArray &errorOutput);

}
