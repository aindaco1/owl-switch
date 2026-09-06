#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

namespace HelperResolver {

QString resolve(const QString &appRoot, const QString &name,
                const QString &developmentFallback = {});
QString mpv(const QString &appRoot);
QString ffmpeg(const QString &appRoot);
QString ffprobe(const QString &appRoot);
QString ytDlp(const QString &appRoot);
QString deno(const QString &appRoot);
QString tlsCaBundle(const QString &appRoot);
QStringList youtubeMpvArguments(const QString &appRoot);
QProcessEnvironment processEnvironment(const QString &appRoot);

}
