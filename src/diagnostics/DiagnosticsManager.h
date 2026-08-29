#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QObject>
#include <QtGlobal>

class DiagnosticsManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool submitting READ submitting NOTIFY submittingChanged)

public:
    explicit DiagnosticsManager(const QString &dataRoot, QObject *parent = nullptr);
    ~DiagnosticsManager() override;

    QString status() const { return m_status; }
    bool submitting() const { return m_submitting; }

    Q_INVOKABLE QString reportPreview() const;
    Q_INVOKABLE int eventCount() const;
    Q_INVOKABLE void clearLogs();
    Q_INVOKABLE void submitReport();

    static QString sanitizeMessage(const QString &message);
    static bool shouldRecordMessage(QtMsgType type, const QString &message);

signals:
    void statusChanged();
    void submittingChanged();
    void reportSubmitted();

private:
    static void messageHandler(QtMsgType type, const QMessageLogContext &context,
                               const QString &message);
    void append(QtMsgType type, const QString &message);
    void loadRecentEvents();
    void rotateIfNeeded();
    QJsonArray reportEvents() const;
    QJsonObject reportPayload() const;
    void setStatus(const QString &status);
    void setSubmitting(bool submitting);

    QString m_directory;
    QString m_logPath;
    mutable QMutex m_mutex;
    QJsonArray m_recentEvents;
    QNetworkAccessManager m_network;
    QString m_status;
    bool m_submitting = false;
    QtMessageHandler m_previousHandler = nullptr;

    static DiagnosticsManager *s_instance;
};
