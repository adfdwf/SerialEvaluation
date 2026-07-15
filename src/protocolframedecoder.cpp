#include "protocolframedecoder.h"

#include <QtGlobal>

BEGIN_NAMESPACE_CIQTEK

/**
 * @brief 将新数据加入缓存，并循环解析半包、粘包、原始块和无效帧。
 */
ProtocolFrameDecoder::DecodeResult ProtocolFrameDecoder::appendData(const QByteArray &data)
{
    m_buffer.append(data);

    DecodeResult result;
    while (!m_buffer.isEmpty()) {
        const int headerIndex = findHeader(m_buffer);
        if (headerIndex < 0) {
            // 尾部单独的 A0 可能是下一次 TCP 数据的帧头，不能提前输出。
            const bool partialHeader = static_cast<quint8>(m_buffer.back()) == kHeaderByte;
            const int rawSize = m_buffer.size() - (partialHeader ? 1 : 0);
            if (rawSize > 0) {
                result.frames.push_back(m_buffer.left(rawSize));
                m_buffer.remove(0, rawSize);
            }
            break;
        }

        if (headerIndex > 0) {
            // 帧头前的数据不属于协议帧，按原始数据块交给上层。
            result.frames.push_back(m_buffer.left(headerIndex));
            result.discardedBytes += headerIndex;
            m_buffer.remove(0, headerIndex);
            continue;
        }

        // 只有收到帧头和长度字段后，才可以读取 N。
        if (m_buffer.size() < kLengthFieldSize + 1) {
            break;
        }
        const int payloadSize = payloadLength(m_buffer);
        const int frameSize = payloadSize + kOverheadSize;
        if (m_buffer.size() < frameSize) {
            // 半包：保留缓存，等待下一次 readyRead。
            break;
        }

        const QByteArray frame = m_buffer.left(frameSize);
        m_buffer.remove(0, frameSize);
        if (!checksumValid(frame)) {
            // 校验失败时丢弃当前 A0，重新搜索后续 A0，避免整个连接失步。
            result.errors.push_back(QStringLiteral("TCP frame checksum failed; resynchronizing"));
            ++result.discardedBytes;
            continue;
        }

        // 上层只需要业务 payload，不需要帧头、长度和校验字节。
        result.frames.push_back(frame.mid(2, payloadSize));
    }

    return result;
}

/** 清除所有尚未完成的半帧缓存。 */
void ProtocolFrameDecoder::clear()
{
    m_buffer.clear();
}

/** 返回半包缓存的当前长度。 */
int ProtocolFrameDecoder::bufferedByteCount() const
{
    return m_buffer.size();
}

/** 查找从 from 开始出现的下一个 A0 帧头。 */
int ProtocolFrameDecoder::findHeader(const QByteArray &bytes, int from)
{
    for (int index = qMax(0, from); index < bytes.size(); ++index) {
        if (static_cast<quint8>(bytes.at(index)) == kHeaderByte) {
            return index;
        }
    }
    return -1;
}

/** 读取帧头后紧邻的一个字节作为 payload 长度。 */
int ProtocolFrameDecoder::payloadLength(const QByteArray &bytes)
{
    return static_cast<quint8>(bytes.at(1));
}

/** 验证帧头、长度、payload 的低八位累加和是否等于最后字节。 */
bool ProtocolFrameDecoder::checksumValid(const QByteArray &frame)
{
    if (frame.size() < kMinimumFrameSize) {
        return false;
    }
    return checksumFor(frame, frame.size() - 1) == static_cast<quint8>(frame.back());
}

/** 计算 bytes 前 length 个字节的低八位累加和。 */
quint8 ProtocolFrameDecoder::checksumFor(const QByteArray &bytes, int length)
{
    quint8 checksum = 0;
    for (int index = 0; index < length; ++index) {
        checksum = static_cast<quint8>(checksum + static_cast<quint8>(bytes.at(index)));
    }
    return checksum;
}

END_NAMESPACE_CIQTEK
