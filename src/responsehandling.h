#ifndef RESPONSEHANDLING_H
#define RESPONSEHANDLING_H

#include "namespace.h"
#include "protocolframedecoder.h"

#include <QByteArray>
#include <QString>
#include <QVector>

BEGIN_NAMESPACE_CIQTEK

/** A single response notification emitted by a communication worker. */
struct ResponseEvent {
    QByteArray data;
    bool valid = false;
};

/** Response notifications and independent protocol errors produced by one read. */
struct ResponseBatch {
    QVector<ResponseEvent> events;
    QVector<QString> errors;
};

/** Severity carried by a response log entry. */
enum class ResponseLogLevel {
    Error
};

/** Fully formatted response log body and its severity. */
struct ResponseLogEntry {
    ResponseLogLevel level = ResponseLogLevel::Error;
    QString message;
};

/**
 * Convert a length-based receive buffer into at most one response event.
 * An overrun preserves the complete buffer and marks the event invalid.
 */
ResponseBatch collectLengthResponse(const QByteArray &received, int expectedBytes);

/**
 * Convert one protocol-decoder result into at most one response event.
 * Multiple decoded frames are concatenated in receive order and marked invalid.
 */
ResponseBatch collectProtocolResponse(const ProtocolFrameDecoder::DecodeResult &decoded);

/** Convert raw response bytes to escaped ASCII or upper-case spaced HEX. */
QString formatResponsePayload(const QByteArray &payload,
                              const QString &format = QString(),
                              bool truncate = true);

/** Format the one ERROR entry associated with a pending abnormal response. */
ResponseLogEntry formatPendingResponseError(const QString &portPrefix,
                                            quint64 packetId,
                                            const QString &reason,
                                            const QByteArray &data,
                                            const QString &txFormat);

END_NAMESPACE_CIQTEK

#endif // RESPONSEHANDLING_H
