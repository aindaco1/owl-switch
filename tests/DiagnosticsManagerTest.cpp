#include "diagnostics/DiagnosticsManager.h"

#include <QFile>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

class DiagnosticsManagerTest final : public QObject {
    Q_OBJECT

private slots:
    void sanitizesSensitiveText();
    void filtersRoutineMpvTelemetry();
    void storesBoundedReviewableEvents();
    void freezesReviewedBytesUntilExplicitRefreshAndRestoresPendingDraft();
};

void DiagnosticsManagerTest::sanitizesSensitiveText()
{
    const QString safe = DiagnosticsManager::sanitizeMessage(QStringLiteral(
        "authorization=Bearer-secret user@example.com https://example.com/watch?token=abc "
        "/Users/alice/Private Song.mov"));
    QVERIFY(!safe.contains(QStringLiteral("Bearer-secret")));
    QVERIFY(!safe.contains(QStringLiteral("user@example.com")));
    QVERIFY(!safe.contains(QStringLiteral("example.com")));
    QVERIFY(!safe.contains(QStringLiteral("alice")));
    QVERIFY(safe.contains(QStringLiteral("[redacted]")));
    QVERIFY(safe.contains(QStringLiteral("[redacted-url]")));
    QVERIFY(safe.contains(QStringLiteral("[redacted-path]")));
}

void DiagnosticsManagerTest::filtersRoutineMpvTelemetry()
{
    QVERIFY(!DiagnosticsManager::shouldRecordMessage(
        QtWarningMsg,
        QStringLiteral("[mpv] AV: 00:00:08 / 00:03:15 (5%) A-V: 0.000 Cache: 10s/1MB")));
    QVERIFY(!DiagnosticsManager::shouldRecordMessage(
        QtWarningMsg, QStringLiteral("[mpv] Exiting... (Quit)")));
    QVERIFY(DiagnosticsManager::shouldRecordMessage(
        QtWarningMsg, QStringLiteral("[mpv] Failed to open media stream")));
}

void DiagnosticsManagerTest::storesBoundedReviewableEvents()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    DiagnosticsManager diagnostics(root.path());
    qInfo("[Karaoke] catalog ready with 32000 songs in 40 ms");
    qWarning("failed path /Users/alice/Private.mov token=secret");

    QTRY_COMPARE(diagnostics.eventCount(), 2);
    const QString preview = diagnostics.reportPreview();
    QVERIFY(preview.contains(QStringLiteral("Karaoke")));
    QVERIFY(!preview.contains(QStringLiteral("alice")));
    QVERIFY(!preview.contains(QStringLiteral("secret")));

    const QString logPath = root.filePath(QStringLiteral("diagnostics/owlswitch.jsonl"));
    QFile log(logPath);
    QVERIFY(log.open(QIODevice::ReadOnly));
    const QByteArray stored = log.readAll();
    QVERIFY(!stored.contains("alice"));
    QVERIFY(!stored.contains("secret"));

    diagnostics.clearLogs();
    QCOMPARE(diagnostics.eventCount(), 0);
    QVERIFY(!QFile::exists(logPath));
}

void DiagnosticsManagerTest::freezesReviewedBytesUntilExplicitRefreshAndRestoresPendingDraft()
{
    QTemporaryDir root;
    QString reviewed;
    {
        DiagnosticsManager diagnostics(root.path());
        qWarning("synthetic first event");
        reviewed = diagnostics.reportPreview();
        const auto report = QJsonDocument::fromJson(reviewed.toUtf8()).object();
        QVERIFY(!report.value("report").toObject().value("id").toString().isEmpty());
        qWarning("synthetic later event");
        QCOMPARE(diagnostics.reportPreview(), reviewed);
    }
    DiagnosticsManager restored(root.path());
    QCOMPARE(restored.reportPreview(), reviewed);
    restored.refreshReport();
    QVERIFY(restored.reportPreview() != reviewed);
    QVERIFY(restored.reportPreview().contains("synthetic later event"));
    restored.clearLogs();
    QVERIFY(!QFile::exists(root.filePath("diagnostics/pending-report.json")));
    QCOMPARE(restored.reportPreview(), QStringLiteral("No diagnostic events have been recorded."));
}

QTEST_GUILESS_MAIN(DiagnosticsManagerTest)
#include "DiagnosticsManagerTest.moc"
