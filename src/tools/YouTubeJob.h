#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QTimer>

class YouTubeJob final : public QObject {
    Q_OBJECT

public:
    enum class Failure {
        None,
        Unavailable,
        FailedToStart,
        TimedOut,
        OutputLimit,
        Canceled,
        ProcessFailed
    };
    Q_ENUM(Failure)

    explicit YouTubeJob(const QString &appRoot, QObject *parent = nullptr);

    bool start(const QStringList &arguments,
               int timeoutMs = 120000,
               qint64 maximumOutputBytes = 8 * 1024 * 1024,
               qint64 maximumErrorBytes = 8192);
    void cancel();
    void cancelSilently();
    bool isRunning() const;

signals:
    void outputReady(const QByteArray &output);
    void completed(YouTubeJob::Failure failure, int exitCode,
                   const QString &safeError);

private:
    void finish(Failure failure, int exitCode);
    void appendError(const QByteArray &error);

    QString m_appRoot;
    QProcess *m_process = nullptr;
    QTimer *m_timeout = nullptr;
    qint64 m_outputBytes = 0;
    qint64 m_maximumOutputBytes = 0;
    qint64 m_maximumErrorBytes = 0;
    QByteArray m_errorTail;
    Failure m_forcedFailure = Failure::None;
    bool m_finishing = false;
    bool m_suppressCompletion = false;
};

Q_DECLARE_METATYPE(YouTubeJob::Failure)
