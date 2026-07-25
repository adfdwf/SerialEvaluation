#include "protocolframedecoder.h"
#include "responsehandling.h"

#include <QtTest>

using Ciqtek::ProtocolFrameDecoder;
using Ciqtek::ResponseInvalidReason;
using Ciqtek::ResponseLogLevel;
using Ciqtek::collectLengthResponse;
using Ciqtek::collectProtocolResponse;
using Ciqtek::formatPendingResponseError;

class ResponseHandlingTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void preservesFullTcpLengthOverrunAsOneAbnormalEvent()
    {
        const QByteArray received = QByteArray::fromHex("010203040506");

        const auto batch = collectLengthResponse(received, 4);

        QCOMPARE(batch.events.size(), 1);
        QCOMPARE(batch.events.constFirst().data, received);
        QVERIFY(!batch.events.constFirst().valid);
        QVERIFY(batch.events.constFirst().invalidReason.has_value());
        QCOMPARE(*batch.events.constFirst().invalidReason, ResponseInvalidReason::LengthMismatch);
        QVERIFY(batch.errors.isEmpty());
    }

    void preservesFullSerialLengthOverrunAsOneAbnormalEvent()
    {
        const QByteArray received("reply-extra", 11);

        const auto batch = collectLengthResponse(received, 5);

        QCOMPARE(batch.events.size(), 1);
        QCOMPARE(batch.events.constFirst().data, received);
        QVERIFY(!batch.events.constFirst().valid);
        QVERIFY(batch.events.constFirst().invalidReason.has_value());
        QCOMPARE(*batch.events.constFirst().invalidReason, ResponseInvalidReason::LengthMismatch);
        QVERIFY(batch.errors.isEmpty());
    }

    void preservesOrdinaryLengthResponseBehavior()
    {
        const QByteArray received = QByteArray::fromHex("01020304");

        const auto batch = collectLengthResponse(received, received.size());

        QCOMPARE(batch.events.size(), 1);
        QCOMPARE(batch.events.constFirst().data, received);
        QVERIFY(batch.events.constFirst().valid);
        QVERIFY(!batch.events.constFirst().invalidReason.has_value());
        QVERIFY(batch.errors.isEmpty());
    }

    void keepsPartialLengthResponsePendingForTimeoutHandling()
    {
        const auto batch = collectLengthResponse(QByteArray::fromHex("0102"), 4);

        QVERIFY(batch.events.isEmpty());
        QVERIFY(batch.errors.isEmpty());
    }

    void emitsTcpProtocolFramesAsIndependentEvents()
    {
        ProtocolFrameDecoder decoder;
        const QByteArray first = QByteArray::fromHex("A00064810000000085");
        const QByteArray second = QByteArray::fromHex("A00064810000000186");

        const auto batch = collectProtocolResponse(decoder.appendData(first + second));

        QCOMPARE(batch.events.size(), 2);
        QCOMPARE(batch.events.at(0).data, first);
        QCOMPARE(batch.events.at(1).data, second);
        QVERIFY(batch.events.at(0).valid);
        QVERIFY(batch.events.at(1).valid);
        QVERIFY(!batch.events.at(0).invalidReason.has_value());
        QVERIFY(!batch.events.at(1).invalidReason.has_value());
        QVERIFY(batch.errors.isEmpty());
    }

    void reportsProtocolErrorWhenFramesAndDecoderErrorsCoexist()
    {
        ProtocolFrameDecoder::DecodeResult decoded;
        decoded.frames.push_back(QByteArray::fromHex("A00064810000000085"));
        decoded.errors.push_back(QStringLiteral("checksum failed"));

        const auto batch = collectProtocolResponse(decoded);

        QCOMPARE(batch.events.size(), 1);
        QVERIFY(!batch.events.constFirst().valid);
        QVERIFY(batch.events.constFirst().invalidReason.has_value());
        QCOMPARE(*batch.events.constFirst().invalidReason, ResponseInvalidReason::ProtocolError);
    }

    void preservesOrdinaryProtocolResponseBehavior()
    {
        ProtocolFrameDecoder decoder;
        const QByteArray response = QByteArray::fromHex("A00064810000000085");

        const auto batch = collectProtocolResponse(decoder.appendData(response));

        QCOMPARE(batch.events.size(), 1);
        QCOMPARE(batch.events.constFirst().data, response);
        QVERIFY(batch.events.constFirst().valid);
        QVERIFY(!batch.events.constFirst().invalidReason.has_value());
        QVERIFY(batch.errors.isEmpty());
    }

    void preservesProtocolChecksumErrorsWithoutInventingStickyErrors()
    {
        ProtocolFrameDecoder decoder;
        const QByteArray invalidChecksum = QByteArray::fromHex("A00064810000000000");

        const auto batch = collectProtocolResponse(decoder.appendData(invalidChecksum));

        QVERIFY(batch.events.isEmpty());
        QCOMPARE(batch.errors, QVector<QString>({QStringLiteral("TCP device response checksum failed")}));
        for (const QString &error : batch.errors) {
            QVERIFY(!error.contains(QStringLiteral("sticky packet"), Qt::CaseInsensitive));
        }
    }

    void formatsOneHexStickyErrorForPortAndPacket()
    {
        const auto entry = formatPendingResponseError(QStringLiteral("[Port 10160]"),
                                                      23,
                                                      QStringLiteral("sticky response"),
                                                      QByteArray::fromHex("0102A0FF"),
                                                      QStringLiteral("HEX"));

        QCOMPARE(entry.level, ResponseLogLevel::Error);
        QCOMPARE(entry.message,
                 QStringLiteral("[Port 10160] #23 sticky response: 01 02 A0 FF"));
    }

    void formatsAsciiStickyErrorUsingEscapedAscii()
    {
        const QByteArray received("OK\r\n\t", 5);
        QByteArray withControlByte = received;
        withControlByte.append(char(0x01));

        const auto entry = formatPendingResponseError(QStringLiteral("[COM3]"),
                                                      7,
                                                      QStringLiteral("sticky response"),
                                                      withControlByte,
                                                      QStringLiteral("ASCII"));

        QCOMPARE(entry.level, ResponseLogLevel::Error);
        QCOMPARE(entry.message,
                 QStringLiteral("[COM3] #7 sticky response: OK\\r\\n\\t\\x01"));
    }

    void preservesTimeoutErrorFormatting()
    {
        const auto entry = formatPendingResponseError(QStringLiteral("[Port 10160]"),
                                                      23,
                                                      QStringLiteral("response exceeded Timeout 300 ms"),
                                                      QByteArray::fromHex("0102"),
                                                      QStringLiteral("HEX"));

        QCOMPARE(entry.level, ResponseLogLevel::Error);
        QCOMPARE(entry.message,
                 QStringLiteral("[Port 10160] #23 response exceeded Timeout 300 ms (01 02)"));
    }
};

QTEST_APPLESS_MAIN(ResponseHandlingTest)

#include "responsehandling_test.moc"
