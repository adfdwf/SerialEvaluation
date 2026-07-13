#include "protocolframedecoder.h"

#include <QCoreApplication>
#include <QDebug>

using Ciqtek::ProtocolFrameDecoder;

namespace {

QByteArray makeFrame(quint8 command, const QByteArray &payload = {})
{
    QByteArray frame;
    frame.append(static_cast<char>(0xA0));
    frame.append(static_cast<char>(0x81));
    frame.append(static_cast<char>(command));

    const quint32 length = static_cast<quint32>(payload.size());
    frame.append(static_cast<char>((length >> 24) & 0xFF));
    frame.append(static_cast<char>((length >> 16) & 0xFF));
    frame.append(static_cast<char>((length >> 8) & 0xFF));
    frame.append(static_cast<char>(length & 0xFF));
    frame.append(static_cast<char>(0x00));
    frame.append(payload);

    quint8 checksum = 0;
    for (char byte : frame) {
        checksum = static_cast<quint8>(checksum + static_cast<quint8>(byte));
    }
    frame.append(static_cast<char>(checksum));
    return frame;
}

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
    QCoreApplication app(argc, argv);

    bool passed = true;
    ProtocolFrameDecoder decoder;

    const QByteArray emptyPayloadFrame = makeFrame(0x01);
    const QByteArray payloadFrame = makeFrame(0x02, QByteArray::fromHex("01020304"));

    passed &= expectFrames(decoder.appendData(emptyPayloadFrame.left(4)), {}, "partial header");
    passed &= expectFrames(decoder.appendData(emptyPayloadFrame.mid(4)), {emptyPayloadFrame}, "completed zero-payload frame");

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

    if (!passed) {
        qCritical() << "ProtocolFrameDecoder tests failed";
        return 1;
    }

    qInfo() << "ProtocolFrameDecoder tests passed";
    return 0;
}