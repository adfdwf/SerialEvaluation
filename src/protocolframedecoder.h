#ifndef PROTOCOLFRAMEDECODER_H
#define PROTOCOLFRAMEDECODER_H

#include "namespace.h"

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QtGlobal>

BEGIN_NAMESPACE_CIQTEK

/**
 * @brief Decodes request and device-response TCP frames, with raw fallback.
 *
 * Protocol frame format inferred from the command sample:
 *   A0 81 01 00 00 00 00 00 22
 * - bytes 0..1: frame header 0xA0 0x81
 * - bytes 3..6: big-endian payload length
 * - byte 7: header field before payload
 * - last byte: low 8 bits of the sum of all previous bytes
 *
 * The device response shown by the benchmark protocol is a fixed nine-byte
 * frame beginning with A0 00 64 81 and uses the same checksum rule.
 *
 * If a received byte block does not contain a protocol header, it is emitted
 * as one raw frame so generic TCP echo/reply data does not time out.
 */
class ProtocolFrameDecoder final
{
public:
    struct DecodeResult {
        QVector<QByteArray> frames;
        QVector<QString> errors;
        int discardedBytes = 0;
    };

    DecodeResult appendData(const QByteArray &data);
    void clear();

    int bufferedByteCount() const;

private:
    static constexpr unsigned char kHeaderByte = 0xA0;
    static constexpr unsigned char kFrameTypeByte = 0x81;
    static constexpr int kLengthOffset = 3;
    static constexpr int kLengthSize = 4;
    static constexpr int kHeaderSize = 8;
    static constexpr int kChecksumSize = 1;
    static constexpr int kMinimumFrameSize = kHeaderSize + kChecksumSize;
    static constexpr int kResponseFrameSize = 9;
    static constexpr quint32 kMaximumPayloadLength = 4 * 1024 * 1024;

    quint32 payloadLength() const;
    static int findHeader(const QByteArray &bytes, int from = 0);
    static int findResponseHeader(const QByteArray &bytes);
    static bool hasPartialResponseHeader(const QByteArray &bytes);
    static bool checksumValid(const QByteArray &frame);
    static quint8 checksumFor(const QByteArray &bytes, int length);

    QByteArray m_buffer;
};

inline ProtocolFrameDecoder::DecodeResult ProtocolFrameDecoder::appendData(const QByteArray &data)
{
    m_buffer.append(data);

    DecodeResult result;
    while (!m_buffer.isEmpty()) {
        const int requestIndex = findHeader(m_buffer);
        const int responseIndex = findResponseHeader(m_buffer);
        const bool responseFrame = responseIndex >= 0 && (requestIndex < 0 || responseIndex < requestIndex);
        const int headerIndex = responseFrame ? responseIndex : requestIndex;
        if (headerIndex < 0) {
            if (hasPartialResponseHeader(m_buffer)) {
                break;
            }
            if (m_buffer.size() == 1 && static_cast<quint8>(m_buffer.at(0)) == kHeaderByte) {
                break;
            }

            result.frames.push_back(m_buffer);
            m_buffer.clear();
            break;
        }

        if (headerIndex > 0) {
            result.discardedBytes += headerIndex;
            m_buffer.remove(0, headerIndex);
        }

        if (responseFrame) {
            if (m_buffer.size() < kResponseFrameSize) {
                break;
            }
            const QByteArray frame = m_buffer.left(kResponseFrameSize);
            if (!checksumValid(frame)) {
                result.errors.push_back(QStringLiteral("TCP device response checksum failed"));
            } else {
                result.frames.push_back(frame);
            }
            m_buffer.remove(0, kResponseFrameSize);
            continue;
        }

        if (m_buffer.size() < kMinimumFrameSize) {
            break;
        }

        const quint32 payloadSize = payloadLength();
        if (payloadSize > kMaximumPayloadLength) {
            // This looks like raw data that happened to contain A0 81, not a valid protocol frame.
            result.frames.push_back(m_buffer);
            m_buffer.clear();
            break;
        }

        const int frameSize = kHeaderSize + static_cast<int>(payloadSize) + kChecksumSize;
        if (m_buffer.size() < frameSize) {
            break;
        }

        const QByteArray frame = m_buffer.left(frameSize);
        if (!checksumValid(frame)) {
            result.errors.push_back(QStringLiteral("TCP protocol frame checksum failed; resynchronizing"));
            m_buffer.remove(0, 1);
            continue;
        }

        result.frames.push_back(frame);
        m_buffer.remove(0, frameSize);
    }

    return result;
}

inline void ProtocolFrameDecoder::clear()
{
    m_buffer.clear();
}

inline int ProtocolFrameDecoder::bufferedByteCount() const
{
    return m_buffer.size();
}

inline quint32 ProtocolFrameDecoder::payloadLength() const
{
    quint32 value = 0;
    for (int i = 0; i < kLengthSize; ++i) {
        value = (value << 8) | static_cast<quint8>(m_buffer.at(kLengthOffset + i));
    }
    return value;
}

inline int ProtocolFrameDecoder::findHeader(const QByteArray &bytes, int from)
{
    for (int i = qMax(0, from); i + 1 < bytes.size(); ++i) {
        if (static_cast<quint8>(bytes.at(i)) == kHeaderByte &&
            static_cast<quint8>(bytes.at(i + 1)) == kFrameTypeByte) {
            return i;
        }
    }
    return -1;
}

inline int ProtocolFrameDecoder::findResponseHeader(const QByteArray &bytes)
{
    for (int i = 0; i + 3 < bytes.size(); ++i) {
        if (static_cast<quint8>(bytes.at(i)) == 0xA0 &&
            static_cast<quint8>(bytes.at(i + 1)) == 0x00 &&
            static_cast<quint8>(bytes.at(i + 2)) == 0x64 &&
            static_cast<quint8>(bytes.at(i + 3)) == 0x81) {
            return i;
        }
    }
    return -1;
}

inline bool ProtocolFrameDecoder::hasPartialResponseHeader(const QByteArray &bytes)
{
    static constexpr unsigned char prefix[] = {0xA0, 0x00, 0x64, 0x81};
    const int length = qMin(bytes.size(), 3);
    for (int i = 0; i < length; ++i) {
        if (static_cast<quint8>(bytes.at(i)) != prefix[i]) {
            return false;
        }
    }
    return !bytes.isEmpty() && bytes.size() < 4;
}

inline bool ProtocolFrameDecoder::checksumValid(const QByteArray &frame)
{
    if (frame.size() < kMinimumFrameSize) {
        return false;
    }

    return checksumFor(frame, frame.size() - kChecksumSize) == static_cast<quint8>(frame.at(frame.size() - 1));
}

inline quint8 ProtocolFrameDecoder::checksumFor(const QByteArray &bytes, int length)
{
    quint8 sum = 0;
    for (int i = 0; i < length; ++i) {
        sum = static_cast<quint8>(sum + static_cast<quint8>(bytes.at(i)));
    }
    return sum;
}

END_NAMESPACE_CIQTEK

#endif // PROTOCOLFRAMEDECODER_H
