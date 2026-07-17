#include "tcpportparser.h"

#include <QRegularExpression>
#include <QSet>

BEGIN_NAMESPACE_CIQTEK

TcpPortParseResult parseTcpPortList(const QString &text)
{
    TcpPortParseResult result;
    QSet<quint16> seen;
    const QStringList tokens = text.split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);

    for (const QString &token : tokens) {
        bool ok = false;
        const uint value = token.toUInt(&ok, 10);
        if (!ok || value < 1 || value > 65535) {
            ++result.invalidCount;
            continue;
        }

        const auto port = static_cast<quint16>(value);
        if (seen.contains(port)) {
            ++result.duplicateCount;
            continue;
        }
        seen.insert(port);
        result.ports.append(port);
    }

    return result;
}

END_NAMESPACE_CIQTEK
