#ifndef PROTOCOLFRAMEDECODER_H
#define PROTOCOLFRAMEDECODER_H

#include "namespace.h"

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QtGlobal>

BEGIN_NAMESPACE_CIQTEK

/**
 * @brief TCP 协议帧解码器，支持请求帧、设备响应帧和原始数据回退。
 *
 * 请求帧以 A0 81 开头，字节 3 到 6 为大端序 payload 长度，最后一个字节
 * 是前面全部字节累加和的低 8 位。设备响应以 A0 00 64 81 开头，当前为
 * 固定 9 字节。没有协议头的数据会作为原始帧输出，避免普通 Echo 数据超时。
 */
class ProtocolFrameDecoder final
{
public:
    /**
     * @brief 一次 appendData 调用产生的解码结果。
     */
    struct DecodeResult {
        QVector<QByteArray> frames; ///< 本次识别出的完整帧或原始数据块。
        QVector<QString> errors;    ///< 本次发现的校验或协议错误。
        int discardedBytes = 0;     ///< 为重新同步而丢弃的前导字节数。
    };

    /** @brief 追加 TCP 新数据并尽可能解码完整帧。 */
    DecodeResult appendData(const QByteArray &data);
    /** @brief 清空尚未完成的半帧缓存。 */
    void clear();

    /** @brief 返回当前缓存中尚未组成完整帧的字节数。 */
    int bufferedByteCount() const;

private:
    static constexpr unsigned char kHeaderByte = 0xA0; ///< 请求/响应共同的首字节。
    static constexpr unsigned char kFrameTypeByte = 0x81; ///< 请求帧类型字节。
    static constexpr int kLengthOffset = 3; ///< payload 长度字段起始偏移。
    static constexpr int kLengthSize = 4; ///< payload 长度字段的字节数。
    static constexpr int kHeaderSize = 8; ///< 请求帧中 payload 之前的字节数。
    static constexpr int kChecksumSize = 1; ///< 校验和占用字节数。
    static constexpr int kMinimumFrameSize = kHeaderSize + kChecksumSize; ///< 空 payload 请求帧长度。
    static constexpr int kResponseFrameSize = 9; ///< 当前设备响应的固定长度。
    static constexpr quint32 kMaximumPayloadLength = 4 * 1024 * 1024; ///< 单帧 payload 上限。
    static constexpr int kMaximumBufferedBytes = 8 * 1024 * 1024; ///< 解码器缓存硬上限。

    /** @brief 从缓存的长度字段读取 payload 长度。 */
    quint32 payloadLength() const;
    /** @brief 查找下一个 A0 81 请求头。 */
    static int findHeader(const QByteArray &bytes, int from = 0);
    /** @brief 查找下一个 A0 00 64 81 响应头。 */
    static int findResponseHeader(const QByteArray &bytes);
    /** @brief 判断缓存是否可能是一个被拆开的响应头。 */
    static bool hasPartialResponseHeader(const QByteArray &bytes);
    /** @brief 验证一帧最后一个字节的累加校验和。 */
    static bool checksumValid(const QByteArray &frame);
    /** @brief 计算指定长度字节的低 8 位累加和。 */
    static quint8 checksumFor(const QByteArray &bytes, int length);

    QByteArray m_buffer; ///< 尚未解码完成的 TCP 字节缓存。
};

inline ProtocolFrameDecoder::DecodeResult ProtocolFrameDecoder::appendData(const QByteArray &data)
{
    DecodeResult result;
    if (data.size() > kMaximumBufferedBytes || m_buffer.size() > kMaximumBufferedBytes - data.size()) {
        m_buffer.clear();
        result.errors.push_back(QStringLiteral("TCP decoder buffer limit reached; buffered data discarded"));
        return result;
    }

    // 将新到达的 TCP 字节追加到半帧缓存中。
    m_buffer.append(data);

    // result 记录本次调用产生的帧、错误以及重新同步信息。
    while (!m_buffer.isEmpty()) {
        // 分别寻找请求头和响应头，较早出现的头决定当前帧类型。
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
            // 响应长度固定，因此只截取固定长度进行校验。
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

        // 读取请求帧 payload 长度并计算完整帧长度。
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
    // 丢弃所有未完成数据，通常在连接断开或重新连接时调用。
    m_buffer.clear();
}

inline int ProtocolFrameDecoder::bufferedByteCount() const
{
    // 返回半帧缓存长度，供断开日志说明丢弃了多少数据。
    return m_buffer.size();
}

inline quint32 ProtocolFrameDecoder::payloadLength() const
{
    // 按协议规定的大端序读取四字节 payload 长度。
    quint32 value = 0;
    for (int i = 0; i < kLengthSize; ++i) {
        // i 是长度字段内的相对字节下标。
        value = (value << 8) | static_cast<quint8>(m_buffer.at(kLengthOffset + i));
    }
    return value;
}

inline int ProtocolFrameDecoder::findHeader(const QByteArray &bytes, int from)
{
    // 从 from 开始扫描请求帧头，找不到时返回 -1。
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
    // 扫描设备响应固定四字节帧头。
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
    // prefix 表示设备响应头，用于判断当前缓存是否只是半个帧头。
    static constexpr unsigned char prefix[] = {0xA0, 0x00, 0x64, 0x81};
    // 只比较当前已有的最多前三个字节。
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
    // 长度不足最小帧时不可能包含合法校验和。
    if (frame.size() < kMinimumFrameSize) {
        return false;
    }

    return checksumFor(frame, frame.size() - kChecksumSize) == static_cast<quint8>(frame.at(frame.size() - 1));
}

inline quint8 ProtocolFrameDecoder::checksumFor(const QByteArray &bytes, int length)
{
    // sum 使用 quint8，累加过程自然保留低八位。
    quint8 sum = 0;
    for (int i = 0; i < length; ++i) {
        sum = static_cast<quint8>(sum + static_cast<quint8>(bytes.at(i)));
    }
    return sum;
}

END_NAMESPACE_CIQTEK

#endif // PROTOCOLFRAMEDECODER_H
