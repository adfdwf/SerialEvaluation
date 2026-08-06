#include <QtTest>
#include <QTableWidget>
#include <QComboBox>

#include "mainwindow.h"
#include "serialclientworker.h"

using namespace Ciqtek;

class LogFileTest final : public QObject
{
    Q_OBJECT

private slots:
    void reusesOpenFileForRepeatedPortLogs();
    void removingRunningTcpPortResetsAggregateState();
    void removingRunningSerialPortResetsAggregateState();
    void serialConnectionFailureReleasesWorker();
    void serialDisconnectClearsOneShotState();
};

void LogFileTest::reusesOpenFileForRepeatedPortLogs()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    QDir::setCurrent(temporaryDir.path());

    MainWindow window;
    window.appendLogFile(QStringLiteral("COM_TEST"), QStringLiteral("first"));
    window.appendLogFile(QStringLiteral("COM_TEST"), QStringLiteral("second"));

    QCOMPARE(window.m_logFiles.size(), 1);
    QVERIFY(window.m_logFiles.value(QStringLiteral("COM_TEST"))->isOpen());
}

void LogFileTest::removingRunningTcpPortResetsAggregateState()
{
    MainWindow window;
    auto *session = window.m_tcpSessions.value(10160, nullptr);
    QVERIFY(session);

    session->testRunning = true;
    session->statistics.recordSend(QByteArrayLiteral("request"), QStringLiteral("ASCII"));
    window.m_testRunning = true;

    window.removeTcpPort(10160);

    QVERIFY(!window.m_tcpSessions.contains(10160));
    QVERIFY(!window.m_testRunning);
}

void LogFileTest::removingRunningSerialPortResetsAggregateState()
{
    MainWindow window;
    window.addSerialPort(QStringLiteral("COM_TEST"));
    auto *session = window.m_serialSessions.value(QStringLiteral("COM_TEST"), nullptr);
    QVERIFY(session);

    session->oneShotRunning = true;
    session->statistics.recordSend(QByteArrayLiteral("request"), QStringLiteral("ASCII"));
    window.m_testRunning = true;

    window.removeSerialPort(QStringLiteral("COM_TEST"));

    QVERIFY(!window.m_serialSessions.contains(QStringLiteral("COM_TEST")));
    QVERIFY(!window.m_testRunning);
}

void LogFileTest::serialConnectionFailureReleasesWorker()
{
    MainWindow window;
    window.addSerialPort(QStringLiteral("COM_TEST"));
    auto *createdSession = window.m_serialSessions.value(QStringLiteral("COM_TEST"), nullptr);
    QVERIFY(createdSession);
    createdSession->portCombo->addItem(QStringLiteral("COM_TEST"));
    createdSession->portCombo->setCurrentText(QStringLiteral("COM_TEST"));
    window.m_serialPortTable->selectRow(0);
    window.connectSerialPorts();

    auto *session = window.m_serialSessions.value(QStringLiteral("COM_TEST"), nullptr);
    QVERIFY(session);
    QVERIFY(session->worker);
    auto *worker = session->worker;

    emit worker->signalErrorOccurred(QStringLiteral("open failed"));

    QVERIFY(session->worker == nullptr);
    QVERIFY(!session->connecting);
}

void LogFileTest::serialDisconnectClearsOneShotState()
{
    MainWindow window;
    window.addSerialPort(QStringLiteral("COM_TEST"));
    auto *createdSession = window.m_serialSessions.value(QStringLiteral("COM_TEST"), nullptr);
    QVERIFY(createdSession);
    createdSession->portCombo->addItem(QStringLiteral("COM_TEST"));
    createdSession->portCombo->setCurrentText(QStringLiteral("COM_TEST"));
    window.m_serialPortTable->selectRow(0);
    window.connectSerialPorts();

    auto *session = window.m_serialSessions.value(QStringLiteral("COM_TEST"), nullptr);
    QVERIFY(session);
    QVERIFY(session->worker);
    session->oneShotRunning = true;
    session->awaitingResponse = true;
    session->statistics.recordSend(QByteArrayLiteral("request"), QStringLiteral("ASCII"));

    emit session->worker->signalDisconnected();

    QVERIFY(!session->oneShotRunning);
    QVERIFY(!session->awaitingResponse);
    QCOMPARE(session->statistics.pendingPacketCount(), 0);
}

QTEST_MAIN(LogFileTest)
#include "logfile_test.moc"
