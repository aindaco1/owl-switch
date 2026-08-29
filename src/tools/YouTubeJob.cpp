#include "YouTubeJob.h"

#include "HelperResolver.h"
#include "YouTubePolicy.h"

#include <QTimer>

YouTubeJob::YouTubeJob(const QString &appRoot, QObject *parent)
    : QObject(parent)
    , m_appRoot(appRoot)
    , m_process(new QProcess(this))
    , m_timeout(new QTimer(this))
{
    m_timeout->setSingleShot(true);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        const QByteArray output = m_process->readAllStandardOutput();
        if (output.isEmpty() || m_finishing)
            return;
        if (m_outputBytes + output.size() > m_maximumOutputBytes) {
            m_forcedFailure = Failure::OutputLimit;
            m_process->kill();
            return;
        }
        m_outputBytes += output.size();
        emit outputReady(output);
    });
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        appendError(m_process->readAllStandardError());
    });
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        appendError(m_process->readAllStandardError());
        if (m_forcedFailure != Failure::None) {
            finish(m_forcedFailure, exitCode);
        } else if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            finish(Failure::ProcessFailed, exitCode);
        } else {
            finish(Failure::None, exitCode);
        }
    });
    connect(m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_finishing)
            return;
        appendError(m_process->errorString().toUtf8());
        finish(Failure::FailedToStart, -1);
    });
    connect(m_timeout, &QTimer::timeout, this, [this] {
        if (!isRunning())
            return;
        m_forcedFailure = Failure::TimedOut;
        m_process->kill();
    });
}

bool YouTubeJob::start(const QStringList &arguments, int timeoutMs,
                       qint64 maximumOutputBytes, qint64 maximumErrorBytes)
{
    if (isRunning() || arguments.isEmpty())
        return false;
    const QString executable = HelperResolver::ytDlp(m_appRoot);
    if (executable.isEmpty()) {
        QTimer::singleShot(0, this, [this] { finish(Failure::Unavailable, -1); });
        return false;
    }

    m_outputBytes = 0;
    m_maximumOutputBytes = qMax<qint64>(1, maximumOutputBytes);
    m_maximumErrorBytes = qMax<qint64>(1, maximumErrorBytes);
    m_errorTail.clear();
    m_forcedFailure = Failure::None;
    m_finishing = false;
    m_suppressCompletion = false;
    m_process->setProcessEnvironment(HelperResolver::processEnvironment(m_appRoot));
    m_process->start(executable, arguments);
    if (timeoutMs > 0)
        m_timeout->start(timeoutMs);
    return true;
}

void YouTubeJob::cancel()
{
    if (!isRunning())
        return;
    m_forcedFailure = Failure::Canceled;
    m_process->kill();
}

void YouTubeJob::cancelSilently()
{
    if (!isRunning())
        return;
    m_suppressCompletion = true;
    m_forcedFailure = Failure::Canceled;
    m_process->kill();
    m_process->waitForFinished(1000);
}

bool YouTubeJob::isRunning() const
{
    return m_process->state() != QProcess::NotRunning;
}

void YouTubeJob::finish(Failure failure, int exitCode)
{
    if (m_finishing)
        return;
    m_finishing = true;
    m_timeout->stop();
    if (m_suppressCompletion) {
        m_suppressCompletion = false;
        return;
    }
    const QString safeError = YouTubePolicy::safeHelperError(m_errorTail);
    emit completed(failure, exitCode, safeError);
}

void YouTubeJob::appendError(const QByteArray &error)
{
    if (error.isEmpty())
        return;
    m_errorTail += error;
    if (m_errorTail.size() > m_maximumErrorBytes)
        m_errorTail = m_errorTail.right(m_maximumErrorBytes);
}
