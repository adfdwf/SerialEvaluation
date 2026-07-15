#include "protocolframedecoder.h"

#include <QCoreApplication>
#include <QDebug>

using Ciqtek::ProtocolFrameDecoder;

namespace {

/** 按 A0 + 长度 + payload + 校验和格式构造测试帧。 */
QByteArray makeFrame(const QByteArray &payload)
{
    QByteArray frame;
    frame.append(static_cast<char>(0xA0));
    frame.append(static_cast<char>(payload.size()));
    frame.append(payload);

    quint8 checksum = 0;
    for (char byte : frame) {
        checksum = static_cast<quint8>(checksum + static_cast<quint8>(byte));
    }
    frame.append(static_cast<char>(checksum));
    return frame;
}

/** 检查解码结果的 payload/原始块列表是否符合预期。 */
bool expectFrames(const ProtocolFrameDecoder::DecodeResult &result,
                  const QVector<QByteArray> &expected,
                  const char *message)
{
    if (result.frames == expected) {
        return true;
    }
    qCritical() << message << "expected" << expected << "got" << result.frames;
    return false;
}

} // namespace

/** 执行新协议的半包、粘包、原始数据和校验失败测试。 */
int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    bool passed = true;

    const QByteArray firstPayload = QByteArray::fromHex("010203");
    const QByteArray secondPayload = QByteArray("ASCII payload");
    const QByteArray firstFrame = makeFrame(firstPayload);
    const QByteArray secondFrame = makeFrame(secondPayload);

    ProtocolFrameDecoder splitDecoder;
    passed &= expectFrames(splitDecoder.appendData(firstFrame.left(2)), {}, "partial header/length");
    passed &= expectFrames(splitDecoder.appendData(firstFrame.mid(2)), {firstPayload}, "completed payload");

    ProtocolFrameDecoder stickyDecoder;
    passed &= expectFrames(stickyDecoder.appendData(firstFrame + secondFrame), {firstPayload, secondPayload}, "sticky frames");

    ProtocolFrameDecoder rawDecoder;
    const QByteArray raw("ASCII command\r\n");
    passed &= expectFrames(rawDecoder.appendData(raw), {raw}, "raw block");

    ProtocolFrameDecoder mixedDecoder;
    const QByteArray prefix("noise");
    passed &= expectFrames(mixedDecoder.appendData(prefix + firstFrame), {prefix, firstPayload}, "raw prefix and frame");

    ProtocolFrameDecoder invalidDecoder;
    QByteArray invalidFrame = firstFrame;
    invalidFrame[invalidFrame.size() - 1] ^= 0x01;
    const auto invalidResult = invalidDecoder.appendData(invalidFrame + secondFrame);
    passed &= invalidResult.frames == QVector<QByteArray>({secondPayload});
    passed &= invalidResult.errors.size() == 1;

    ProtocolFrameDecoder zeroLengthDecoder;
    passed &= expectFrames(zeroLengthDecoder.appendData(makeFrame({})), {QByteArray()}, "zero-length payload");

    if (!passed) {
        qCritical() << "ProtocolFrameDecoder tests failed";
        return 1;
    }
    qInfo() << "ProtocolFrameDecoder tests passed";
    return 0;
}
