#include "HelperResolver.h"
#include "YouTubePolicy.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#ifndef DEV_YT_DLP_EXECUTABLE
#define DEV_YT_DLP_EXECUTABLE ""
#endif

#ifndef DEV_DENO_EXECUTABLE
#define DEV_DENO_EXECUTABLE ""
#endif

namespace {

QString executablePathIfUsable(const QString &path)
{
    const QFileInfo info(path);
    if (info.isFile() && info.isExecutable())
        return info.absoluteFilePath();
    return {};
}

void prependPath(QStringList &paths, const QString &directory)
{
    const QString cleaned = QDir::cleanPath(directory);
    if (!cleaned.isEmpty() && cleaned != QStringLiteral(".") && !paths.contains(cleaned))
        paths.prepend(cleaned);
}

}

namespace HelperResolver {

QString resolve(const QString &appRoot, const QString &name,
                const QString &developmentFallback)
{
    QString path;
#ifdef Q_OS_MACOS
    const QDir appDir(QCoreApplication::applicationDirPath());
    path = executablePathIfUsable(appDir.filePath(QStringLiteral("../Resources/bin/") + name));
    if (!path.isEmpty())
        return path;
#endif

    path = executablePathIfUsable(QDir(appRoot).filePath(QStringLiteral("bin/") + name));
    if (!path.isEmpty())
        return path;

    path = executablePathIfUsable(developmentFallback);
    if (!path.isEmpty())
        return path;

    path = QStandardPaths::findExecutable(name);
    if (!path.isEmpty())
        return path;

#ifdef Q_OS_MACOS
    return QStandardPaths::findExecutable(name, {
        QStringLiteral("/opt/homebrew/bin"),
        QStringLiteral("/usr/local/bin")
    });
#else
    return {};
#endif
}

QString mpv(const QString &appRoot)
{
    return resolve(appRoot, QStringLiteral("mpv"));
}

QString ffprobe(const QString &appRoot)
{
    return resolve(appRoot, QStringLiteral("ffprobe"));
}

QString ffmpeg(const QString &appRoot)
{
    return resolve(appRoot, QStringLiteral("ffmpeg"));
}

QString ytDlp(const QString &appRoot)
{
    return resolve(appRoot, QStringLiteral("yt-dlp"),
                   QString::fromUtf8(DEV_YT_DLP_EXECUTABLE));
}

QString deno(const QString &appRoot)
{
    return resolve(appRoot, QStringLiteral("deno"),
                   QString::fromUtf8(DEV_DENO_EXECUTABLE));
}

QStringList youtubeMpvArguments(const QString &appRoot)
{
    return YouTubePolicy::mpvArguments(appRoot,
                                       YouTubePolicy::MediaProfile::Video720p);
}

QProcessEnvironment processEnvironment(const QString &appRoot)
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    QStringList paths = environment.value(QStringLiteral("PATH")).split(':', Qt::SkipEmptyParts);

    const QStringList helpers = {
        deno(appRoot),
        ytDlp(appRoot),
        ffmpeg(appRoot),
        ffprobe(appRoot),
        mpv(appRoot)
    };
    for (auto it = helpers.crbegin(); it != helpers.crend(); ++it) {
        if (!it->isEmpty())
            prependPath(paths, QFileInfo(*it).absolutePath());
    }

    environment.insert(QStringLiteral("PATH"), paths.join(':'));
    environment.insert(QStringLiteral("APP_ROOT"), appRoot);
    return environment;
}

}
