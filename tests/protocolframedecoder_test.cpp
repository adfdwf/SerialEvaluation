#include "protocolframedecoder.h"

#include "statisticsmanager.h"

#include <QCoreApplication>
#include <QDebug>

/** 协议解码器单元测试所使用的命名空间。 */
using Ciqtek::ProtocolFrameDecoder;
using Ciqtek::StatisticsManager;

namespace {

/**
 * @brief 按客户端协议构造一帧请求数据。
 * @param command 命令字节。
 * @param payload 可选 payload 数据。
 * @return 带长度字段和校验和的完整请求帧。
 */
QByteArray makeFrame(quint8 command, const QByteArray &payload = {})
{
    // frame 保存协议头、长度、payload 和校验和。
    QByteArray frame;
    frame.append(static_cast<char>(0xA0));
    frame.append(static_cast<char>(0x81));
    frame.append(static_cast<char>(command));

    const quint32 length = static_cast<quint32>(payload.size()); // payload 字节长度。
    frame.append(static_cast<char>((length >> 24) & 0xFF));
    frame.append(static_cast<char>((length >> 16) & 0xFF));
    frame.append(static_cast<char>((length >> 8) & 0xFF));
    frame.append(static_cast<char>(length & 0xFF));
    frame.append(static_cast<char>(0x00));
    frame.append(payload);

    quint8 checksum = 0; // 前面所有字节的低八位累加和。
    for (char byte : frame) {
        checksum = static_cast<quint8>(checksum + static_cast<quint8>(byte));
    }
    frame.append(static_cast<char>(checksum));
    return frame;
}

/**
 * @brief 比较实际帧列表和预期帧列表。
 * @param result 解码器输出结果。
 * @param expected 预期的完整帧列表。
 * @param message 测试失败时显示的测试名称。
 * @return 内容一致返回 true。
 */
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

int main(int argc, char *argv[])
{
    // 单元测试不需要 GUI，只创建 Qt 核心应用对象。
    QCoreApplication app(argc, argv);

    bool passed = true;             // 汇总所有断言是否通过。
    ProtocolFrameDecoder decoder;   // 测试分片请求帧的解码器。

    const QByteArray emptyPayloadFrame = makeFrame(0x01); // 无 payload 请求帧。
    const QByteArray payloadFrame = makeFrame(0x02, QByteArray::fromHex("01020304")); // 带 payload 请求帧。
    const QByteArray deviceResponse = QByteArray::fromHex("A0 00 64 81 01 00 00 10 96"); // 固定设备响应帧。

    passed &= expectFrames(decoder.appendData(emptyPayloadFrame.left(4)), {}, "partial header");
    passed &= expectFrames(decoder.appendData(emptyPayloadFrame.mid(4)), {emptyPayloadFrame}, "completed zero-payload frame");

    ProtocolFrameDecoder responseDecoder;
    passed &= expectFrames(responseDecoder.appendData(deviceResponse.left(3)), {}, "partial device response header");
    passed &= expectFrames(responseDecoder.appendData(deviceResponse.mid(3)), {deviceResponse}, "device response frame");

    ProtocolFrameDecoder stickyDecoder;
    QByteArray stickyInput("NOISE", 5);
    stickyInput.append(payloadFrame);
    stickyInput.append(emptyPayloadFrame);
    const auto stickyResult = stickyDecoder.appendData(stickyInput);
    passed &= stickyResult.discardedBytes == 5;
    passed &= expectFrames(stickyResult, {payloadFrame, emptyPayloadFrame}, "noise and sticky frames");

    ProtocolFrameDecoder splitDecoder;
    passed &= expectFrames(splitDecoder.appendData(payloadFrame.left(10)), {}, "split variable frame first half");
    passed &= expectFrames(splitDecoder.appendData(payloadFrame.mid(10)), {payloadFrame}, "split variable frame second half");

    ProtocolFrameDecoder rawDecoder;
    const QByteArray rawFrame = QByteArray::fromHex("0102030405060708090A0B0C");
    const auto rawResult = rawDecoder.appendData(rawFrame);
    passed &= rawResult.errors.isEmpty();
    passed &= rawResult.discardedBytes == 0;
    passed &= expectFrames(rawResult, {rawFrame}, "raw non-protocol frame fallback");

    ProtocolFrameDecoder falseHeaderDecoder;
    QByteArray falseHeaderInput;
    falseHeaderInput.append(static_cast<char>(0xA0));
    falseHeaderInput.append(static_cast<char>(0x00));
    falseHeaderInput.append(emptyPayloadFrame);
    const auto falseHeaderResult = falseHeaderDecoder.appendData(falseHeaderInput);
    passed &= falseHeaderResult.errors.isEmpty();
    passed &= falseHeaderResult.discardedBytes == 2;
    passed &= expectFrames(falseHeaderResult, {emptyPayloadFrame}, "false A0 before real header");

    ProtocolFrameDecoder invalidLengthDecoder;
    const QByteArray invalidLengthRaw = QByteArray::fromHex("A0 81 01 81 01 00 00 00 00 22");
    const auto invalidLengthResult = invalidLengthDecoder.appendData(invalidLengthRaw);
    passed &= invalidLengthResult.errors.isEmpty();
    passed &= expectFrames(invalidLengthResult, {invalidLengthRaw}, "invalid protocol length falls back to raw");

    ProtocolFrameDecoder checksumDecoder;
    QByteArray badFrame = emptyPayloadFrame;
    badFrame[badFrame.size() - 1] = static_cast<char>(0x00);
    const auto checksumResult = checksumDecoder.appendData(badFrame + payloadFrame);
    passed &= !checksumResult.errors.isEmpty();
    passed &= expectFrames(checksumResult, {payloadFrame}, "resync after checksum failure");

    const QByteArray largePayload(4 * 1024 * 1024, static_cast<char>(0x5A));
    const QByteArray largeFrame = makeFrame(0x03, largePayload);
    ProtocolFrameDecoder largeDecoder;
    const auto largeResult = largeDecoder.appendData(largeFrame);
    passed &= expectFrames(largeResult, {largeFrame}, "4 MiB protocol frame");

    StatisticsManager statistics;
    for (int i = 0; i < 10000; ++i) {
        const auto packet = statistics.recordSend(QByteArray("payload"), QStringLiteral("ASCII"));
        Q_UNUSED(packet);
        passed &= statistics.pendingPacketCount() == 1;
        passed &= statistics.recordReceive(QByteArray("response"));
    }
    const auto statisticsResult = statistics.snapshot();
    passed &= statisticsResult.totalSent == 10000;
    passed &= statisticsResult.successReceived == 10000;
    passed &= statisticsResult.p50Ms >= 0.0;

    if (!passed) {
        qCritical() << "ProtocolFrameDecoder tests failed";
        return 1;
    }

    qInfo() << "ProtocolFrameDecoder tests passed";
    return 0;
}
