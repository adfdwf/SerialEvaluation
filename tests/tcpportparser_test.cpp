#include "tcpportparser.h"

#include <QtTest/QtTest>

using Ciqtek::TcpPortParseResult;
using Ciqtek::parseTcpPortList;

class TcpPortParserTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesSupportedSeparators();
    void countsDuplicates();
    void countsInvalidTokens();
    void acceptsBoundaryPorts();
};

void TcpPortParserTest::parsesSupportedSeparators()
{
    const auto result = parseTcpPortList(QStringLiteral("10160,10165 10200;10300\n10400"));

    QCOMPARE(result.ports, QVector<quint16>({10160, 10165, 10200, 10300, 10400}));
    QCOMPARE(result.invalidCount, 0);
    QCOMPARE(result.duplicateCount, 0);
}

void TcpPortParserTest::countsDuplicates()
{
    const auto result = parseTcpPortList(QStringLiteral("10160,10160;10165 10160"));

    QCOMPARE(result.ports, QVector<quint16>({10160, 10165}));
    QCOMPARE(result.duplicateCount, 2);
}

void TcpPortParserTest::countsInvalidTokens()
{
    const auto result = parseTcpPortList(QStringLiteral("0,-1,65536,abc,10160"));

    QCOMPARE(result.ports, QVector<quint16>({10160}));
    QCOMPARE(result.invalidCount, 4);
}

void TcpPortParserTest::acceptsBoundaryPorts()
{
    const auto result = parseTcpPortList(QStringLiteral("1 65535"));

    QCOMPARE(result.ports, QVector<quint16>({1, 65535}));
}

QTEST_APPLESS_MAIN(TcpPortParserTest)

#include "tcpportparser_test.moc"
