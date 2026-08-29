#include "DiagnosticsManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>

namespace {

constexpr qint64 kMaximumLogBytes = 512 * 1024;
constexpr int kMaximumLocalEvents = 120;
constexpr int kMaximumReportEvents = 20;

QString severityName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return QStringLiteral("debug");
    case QtInfoMsg: return QStringLiteral("info");
    case QtWarningMsg: return QStringLiteral("warning");
    case QtCriticalMsg: return QStringLiteral("critical");
    case QtFatalMsg: return QStringLiteral("fatal");
    }
    return QStringLiteral("unknown");
}

}

DiagnosticsManager *DiagnosticsManager::s_instance = nullptr;

DiagnosticsManager::DiagnosticsManager(const QString &dataRoot, QObject *parent)
    : QObject(parent)
    , m_directory(QDir(dataRoot).filePath(QStringLiteral("diagnostics")))
    , m_logPath(QDir(m_directory).filePath(QStringLiteral("owlswitch.jsonl")))
{
    QDir().mkpath(m_directory);
    loadRecentEvents();
    s_instance = this;
    m_previousHandler = qInstallMessageHandler(&DiagnosticsManager::messageHandler);
}

DiagnosticsManager::~DiagnosticsManager()
{
    if (s_instance == this) {
        qInstallMessageHandler(m_previousHandler);
        s_instance = nullptr;
    }
}

QString DiagnosticsManager::sanitizeMessage(const QString &message)
{
    QString sanitized = message.left(4000);
    sanitized.replace(QRegularExpression(
        QStringLiteral("(?i)(authorization|password|cookie|token|api[_-]?key)(\\s*[:=]\\s*)[^\\s,;]+")),
        QStringLiteral("\\1\\2[redacted]"));
    sanitized.replace(QRegularExpression(
        QStringLiteral("(?i)\\b(?:https?|file|asset)://[^\\s\\\"']+")),
        QStringLiteral("[redacted-url]"));
    sanitized.replace(QRegularExpression(
        QStringLiteral("[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}")),
        QStringLiteral("[redacted-email]"));
    sanitized.replace(QRegularExpression(
        QStringLiteral("[\\\"']/(?:Users|Volumes|private|tmp)/[^\\\"'\\r\\n]*[\\\"']")),
        QStringLiteral("[redacted-path]"));
    sanitized.replace(QRegularExpression(
        QStringLiteral("/(?:Users|Volumes|private|tmp)/[^\\r\\n]*")),
        QStringLiteral("[redacted-path]"));
    sanitized.replace(QRegularExpression(QStringLiteral("[\\r\\n\\t]+")),
                      QStringLiteral(" "));
    sanitized = sanitized.simplified();
    if (sanitized.size() > 600)
        sanitized = sanitized.left(588) + QStringLiteral(" [truncated]");
    return sanitized;
}

bool DiagnosticsManager::shouldRecordMessage(QtMsgType type, const QString &message)
{
    Q_UNUSED(type)
    if (!message.startsWith(QStringLiteral("[mpv]")))
        return true;

    const QString payload = message.mid(5).trimmed();
    static const QRegularExpression terminalProgress(QStringLiteral(
        "^(?:AV|A|V):\\s+[0-9:.]+\\s*/\\s*[0-9:.]+"));
    if (terminalProgress.match(payload).hasMatch())
        return false;
    if (payload.startsWith(QStringLiteral("Exiting... (Quit)"), Qt::CaseInsensitive))
        return false;
    return true;
}

void DiagnosticsManager::messageHandler(QtMsgType type,
                                        const QMessageLogContext &context,
                                        const QString &message)
{
    DiagnosticsManager *instance = s_instance;
    if (instance)
        instance->append(type, message);
    if (instance && instance->m_previousHandler)
        instance->m_previousHandler(type, context, message);
}

void DiagnosticsManager::append(QtMsgType type, const QString &message)
{
    if (!shouldRecordMessage(type, message))
        return;
    const QString safe = sanitizeMessage(message);
    if (safe.isEmpty())
        return;

    const QJsonObject event{
        {QStringLiteral("at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("severity"), severityName(type)},
        {QStringLiteral("message"), safe}
    };

    QMutexLocker locker(&m_mutex);
    m_recentEvents.append(event);
    while (m_recentEvents.size() > kMaximumLocalEvents)
        m_recentEvents.removeFirst();
    rotateIfNeeded();
    QFile file(m_logPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(event).toJson(QJsonDocument::Compact));
    file.write("\n");
}

void DiagnosticsManager::loadRecentEvents()
{
    QFile file(m_logPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    while (!file.atEnd()) {
        const QJsonDocument document = QJsonDocument::fromJson(file.readLine());
        if (document.isObject())
            m_recentEvents.append(document.object());
        while (m_recentEvents.size() > kMaximumLocalEvents)
            m_recentEvents.removeFirst();
    }
}

void DiagnosticsManager::rotateIfNeeded()
{
    if (QFileInfo(m_logPath).size() < kMaximumLogBytes)
        return;
    const QString oldest = m_logPath + QStringLiteral(".2");
    const QString previous = m_logPath + QStringLiteral(".1");
    QFile::remove(oldest);
    if (QFile::exists(previous))
        QFile::rename(previous, oldest);
    QFile::rename(m_logPath, previous);
}

QJsonArray DiagnosticsManager::reportEvents() const
{
    QMutexLocker locker(&m_mutex);
    QJsonArray events;
    const int start = qMax(0, m_recentEvents.size() - kMaximumReportEvents);
    for (int index = start; index < m_recentEvents.size(); ++index)
        events.append(m_recentEvents.at(index));
    return events;
}

QString DiagnosticsManager::reportPreview() const
{
    QStringList lines;
    const QJsonArray events = reportEvents();
    for (const QJsonValue &value : events) {
        const QJsonObject event = value.toObject();
        lines.append(QStringLiteral("%1  %2  %3")
                         .arg(event.value(QStringLiteral("at")).toString(),
                              event.value(QStringLiteral("severity")).toString().toUpper(),
                              event.value(QStringLiteral("message")).toString()));
    }
    return lines.isEmpty() ? QStringLiteral("No diagnostic events have been recorded.")
                           : lines.join(QLatin1Char('\n'));
}

int DiagnosticsManager::eventCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_recentEvents.size();
}

QJsonObject DiagnosticsManager::reportPayload() const
{
    return QJsonObject{
        {QStringLiteral("app"), QJsonObject{
            {QStringLiteral("name"), QStringLiteral("OwlSwitch")},
            {QStringLiteral("version"), QCoreApplication::applicationVersion()},
            {QStringLiteral("identifier"), QString::fromLatin1(APP_BUNDLE_IDENTIFIER)},
            {QStringLiteral("channel"), QStringLiteral("production")},
            {QStringLiteral("buildProfile"), QStringLiteral("release")},
            {QStringLiteral("os"), QSysInfo::productType() + QLatin1Char(' ') + QSysInfo::productVersion()},
            {QStringLiteral("arch"), QSysInfo::currentCpuArchitecture()}
        }},
        {QStringLiteral("report"), QJsonObject{
            {QStringLiteral("id"), QStringLiteral("owlswitch-%1").arg(
                 QDateTime::currentMSecsSinceEpoch())},
            {QStringLiteral("kind"), QStringLiteral("native-output-error")},
            {QStringLiteral("surface"), QStringLiteral("native-output")},
            {QStringLiteral("message"), QStringLiteral("User-submitted OwlSwitch diagnostics")},
            {QStringLiteral("stack"), QString()},
            {QStringLiteral("capturedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {QStringLiteral("context"), QJsonObject{
                {QStringLiteral("recentEvents"), reportEvents()},
                {QStringLiteral("eventCount"), eventCount()}
            }}
        }}
    };
}

void DiagnosticsManager::clearLogs()
{
    {
        QMutexLocker locker(&m_mutex);
        m_recentEvents = {};
        QFile::remove(m_logPath);
        QFile::remove(m_logPath + QStringLiteral(".1"));
        QFile::remove(m_logPath + QStringLiteral(".2"));
    }
    setStatus(QStringLiteral("LOCAL DIAGNOSTICS CLEARED"));
}

void DiagnosticsManager::submitReport()
{
    if (m_submitting)
        return;
    if (eventCount() == 0) {
        setStatus(QStringLiteral("NO DIAGNOSTIC EVENTS TO SEND"));
        return;
    }

    setSubmitting(true);
    setStatus(QStringLiteral("SENDING SANITIZED REPORT..."));
    QNetworkRequest request(QUrl(
        QStringLiteral("https://owlswitch-crash.dustwave.xyz/v1/reports")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("OwlSwitch/%1").arg(QCoreApplication::applicationVersion()));
    request.setTransferTimeout(8000);
    QNetworkReply *reply = m_network.post(
        request, QJsonDocument(reportPayload()).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const bool successful = reply->error() == QNetworkReply::NoError &&
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 200 &&
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() < 300;
        if (successful) {
            setStatus(QStringLiteral("REPORT SENT - THANK YOU"));
            emit reportSubmitted();
        } else {
            setStatus(QStringLiteral("REPORT COULD NOT BE SENT - TRY AGAIN LATER"));
        }
        reply->deleteLater();
        setSubmitting(false);
    });
}

void DiagnosticsManager::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void DiagnosticsManager::setSubmitting(bool submitting)
{
    if (m_submitting == submitting)
        return;
    m_submitting = submitting;
    emit submittingChanged();
}
