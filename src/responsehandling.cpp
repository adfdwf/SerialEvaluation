#include "responsehandling.h"

#include <QChar>

BEGIN_NAMESPACE_CIQTEK

ResponseBatch collectLengthResponse(const QByteArray &received, int expectedBytes)
{
    ResponseBatch batch;
    if (expectedBytes <= 0 || received.size() < expectedBytes) {
        return batch;
    }

    batch.events.push_back({received, received.size() == expectedBytes});
    return batch;
}

ResponseBatch collectProtocolResponse(const ProtocolFrameDecoder::DecodeResult &decoded)
{
    ResponseBatch batch;
    batch.errors = decoded.errors;
    if (decoded.frames.isEmpty()) {
        return batch;
    }

    QByteArray responseData;
    for (const QByteArray &frame : decoded.frames) {
        responseData.append(frame);
    }
    batch.events.push_back({responseData,
                            decoded.errors.isEmpty() && decoded.frames.size() == 1});
    return batch;
}

QString formatResponsePayload(const QByteArray &payload, const QString &format, bool truncate)
{
    if (payload.isEmpty()) {
        return QStringLiteral("(empty)");
    }

    constexpr int kPreviewBytes = 256;
    if (format.trimmed().compare(QStringLiteral("ASCII"), Qt::CaseInsensitive) == 0) {
        const auto decodeAscii = [](const QByteArray &bytes) {
            QString text;
            text.reserve(bytes.size());
            for (const unsigned char byte : bytes) {
                if (byte == '\r') {
                    text += QStringLiteral("\\r");
                } else if (byte == '\n') {
                    text += QStringLiteral("\\n");
                } else if (byte == '\t') {
                    text += QStringLiteral("\\t");
                } else if (byte >= 0x20 && byte <= 0x7E) {
                    text += QChar(byte);
                } else {
                    text += QStringLiteral("\\x%1").arg(byte, 2, 16, QLatin1Char('0'));
                }
            }
            return text;
        };

        if (!truncate || payload.size() <= kPreviewBytes * 2) {
            return decodeAscii(payload);
        }

        return decodeAscii(payload.left(kPreviewBytes)) +
               QStringLiteral(" ... [truncated %1 bytes] ... ").arg(payload.size() - kPreviewBytes * 2) +
               decodeAscii(payload.right(kPreviewBytes));
    }

    if (!truncate || payload.size() <= kPreviewBytes * 2) {
        return QString::fromLatin1(payload.toHex(' ').toUpper());
    }

    const QByteArray prefix = payload.left(kPreviewBytes).toHex(' ').toUpper();
    const QByteArray suffix = payload.right(kPreviewBytes).toHex(' ').toUpper();
    return QString::fromLatin1(prefix) +
           QStringLiteral(" ... [truncated %1 bytes] ... ").arg(payload.size() - kPreviewBytes * 2) +
           QString::fromLatin1(suffix);
}

ResponseLogEntry formatPendingResponseError(const QString &portPrefix,
                                            quint64 packetId,
                                            const QString &reason,
                                            const QByteArray &data,
                                            const QString &txFormat)
{
    ResponseLogEntry entry;
    if (reason == QStringLiteral("sticky response")) {
        entry.message = QStringLiteral("%1 #%2 sticky response: %3")
                            .arg(portPrefix)
                            .arg(packetId)
                            .arg(formatResponsePayload(data, txFormat, false));
        return entry;
    }

    entry.message = QStringLiteral("%1 #%2 %3%4")
                        .arg(portPrefix)
                        .arg(packetId)
                        .arg(reason)
                        .arg(data.isEmpty()
                                 ? QString()
                                 : QStringLiteral(" (%1)").arg(formatResponsePayload(data, txFormat, false)));
    return entry;
}

END_NAMESPACE_CIQTEK
