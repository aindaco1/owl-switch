#include "tools/TlsTrust.h"
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QScopeGuard>
#include <QSslConfiguration>
#include <QTimer>
#include <QtTest>
#include <algorithm>

class TlsTrustTest : public QObject {
    Q_OBJECT
private slots:
    void replacesDiscoveredAnchorsAndRejectsInvalidInput() {
        const auto previous = QSslConfiguration::defaultConfiguration();
        const auto restore = qScopeGuard([&] { QSslConfiguration::setDefaultConfiguration(previous); });
        QFile file(QString::fromUtf8(TEST_TLS_CA));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto certificates = QSslCertificate::fromData(file.readAll());
        QVERIFY(!certificates.isEmpty());
        // A single chosen anchor must replace, not augment, the system store.
        QVERIFY(TlsTrust::configure(certificates.first().toPem()));
        const auto configured = QSslConfiguration::defaultConfiguration();
        QCOMPARE(configured.caCertificates(), QList<QSslCertificate>{certificates.first()});
        QCOMPARE(configured.peerVerifyMode(), QSslSocket::VerifyPeer);
        QVERIFY(!TlsTrust::configure({}));
        QVERIFY(!TlsTrust::configure("not a certificate"));
        QCOMPARE(QSslConfiguration::defaultConfiguration(), configured);
    }

    void publicCompromisedChains() {
        if (qEnvironmentVariable("OWL_TLS_ACCEPTANCE") != QStringLiteral("1"))
            QSKIP("Opt-in live release acceptance: OWL_TLS_ACCEPTANCE=1");
        const auto previous = QSslConfiguration::defaultConfiguration();
        const auto restore = qScopeGuard([&] { QSslConfiguration::setDefaultConfiguration(previous); });
        QFile file(QString::fromUtf8(TEST_TLS_CA));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(TlsTrust::configure(file.readAll()));
        QNetworkAccessManager network;
        QStringList hosts{QStringLiteral("example.com")};
        for (const auto *issuer : {"dv-g3b", "dv-g3c", "ov-g3b", "ov-g3c", "ev-g3b", "ev-g3c"})
            for (const auto *cross : {"", "-aaa"})
                hosts.append(QString("%1%2-emudhra.we-have-a-problem.com").arg(issuer, cross));
        for (const auto &host : hosts) {
            QNetworkRequest request{QUrl("https://" + host + "/")};
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
            request.setTransferTimeout(15000);
            auto *reply = network.get(request);
            QSignalSpy complete(reply, &QNetworkReply::finished);
            QList<QSslError> errors;
            connect(reply, &QNetworkReply::sslErrors, reply, [&](const auto &received) { errors = received; });
            QVERIFY2(complete.wait(20000), qPrintable(host + ": timed out"));
            if (host == "example.com") {
                QCOMPARE(reply->error(), QNetworkReply::NoError);
                QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
            } else {
                QCOMPARE(reply->error(), QNetworkReply::SslHandshakeFailedError);
                const bool distrust = std::any_of(errors.cbegin(), errors.cend(), [](const auto &error) {
                    return error.error() == QSslError::UnableToGetLocalIssuerCertificate
                        || error.error() == QSslError::UnableToVerifyFirstCertificate
                        || error.error() == QSslError::CertificateUntrusted;
                });
                QVERIFY2(distrust, qPrintable(host + ": no trust-chain rejection"));
            }
            qInfo().noquote() << host << "accepted control or rejected chain" << errors;
            delete reply;
        }
    }
};
QTEST_GUILESS_MAIN(TlsTrustTest)
#include "TlsTrustTest.moc"
