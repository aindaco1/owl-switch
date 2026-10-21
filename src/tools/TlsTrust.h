#pragma once
#include <QByteArray>

namespace TlsTrust {
// Replace platform-discovered anchors before constructing any network client.
// Invalid/empty input fails without changing the existing configuration.
bool configure(const QByteArray &pem);
}
