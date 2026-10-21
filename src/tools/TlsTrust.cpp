#include "TlsTrust.h"
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslSocket>

bool TlsTrust::configure(const QByteArray &pem)
{
    const auto certificates = QSslCertificate::fromData(pem, QSsl::Pem);
    if (certificates.isEmpty())
        return false;
    auto configuration = QSslConfiguration::defaultConfiguration();
    configuration.setCaCertificates(certificates);
    configuration.setPeerVerifyMode(QSslSocket::VerifyPeer);
    QSslConfiguration::setDefaultConfiguration(configuration);
    return true;
}
