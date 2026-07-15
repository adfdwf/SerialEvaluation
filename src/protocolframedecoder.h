#ifndef PROTOCOLFRAMEDECODER_H
#define PROTOCOLFRAMEDECODER_H

#include "namespace.h"

#include <QByteArray>
#include <QString>
#include <QVector>

BEGIN_NAMESPACE_CIQTEK

/**
 * @brief 按 A0 + 长度 + payload + 校验和格式解析 TCP 字节流。
 *
 * 完整协议帧长度为 N + 3：首字节 A0，第二字节为 payload 长度 N，随后是
 * N 字节 payload，最后一个字节是前面所有字节低 8 位累加和。没有协议头的
 * 数据会以原始数据块输出；校验失败的帧会丢弃首字节并继续寻找下一个 A0。
 */
class ProtocolFrameDecoder final
{
public:
    /** @brief 一次追加数据产生的帧、错误和重同步统计。 */
    struct DecodeResult {
        QVector<QByteArray> frames; ///< 合法帧的 payload 或无协议头的原始数据块。
        QVector<QString> errors;    ///< 校验失败等解析错误。
        int discardedBytes = 0;     ///< 为重新搜索帧头而丢弃的字节数。
    };

    /**
     * @brief 追加一段 TCP 字节并解析当前可用的完整数据。
     * @param data 新收到的 TCP 字节。
     * @return 本次解析得到的 payload、原始块和错误列表。
     */
    DecodeResult appendData(const QByteArray &data);

    /** @brief 清空半包缓存，通常在断开或重连时调用。 */
    void clear();

    /** @brief 返回当前缓存中尚未组成完整帧的字节数。 */
    int bufferedByteCount() const;

private:
    static constexpr quint8 kHeaderByte = 0xA0; ///< 固定协议帧头。
    static constexpr int kLengthFieldSize = 1;  ///< 长度字段固定为一个字节。
    static constexpr int kOverheadSize = 3;     ///< 帧头、长度、校验和的总字节数。
    static constexpr int kMinimumFrameSize = kOverheadSize; ///< N=0 时的最小帧长度。

    /** @brief 从缓存中寻找下一个 A0 字节。 */
    static int findHeader(const QByteArray &bytes, int from = 0);

    /** @brief 根据缓存第二字节读取 payload 长度 N。 */
    static int payloadLength(const QByteArray &bytes);

    /** @brief 验证完整帧的低八位累加和。 */
    static bool checksumValid(const QByteArray &frame);

    /** @brief 计算指定前缀长度的低八位累加和。 */
    static quint8 checksumFor(const QByteArray &bytes, int length);

    QByteArray m_buffer; ///< 尚未解析完成的 TCP 字节缓存。
};

END_NAMESPACE_CIQTEK

#endif // PROTOCOLFRAMEDECODER_H
