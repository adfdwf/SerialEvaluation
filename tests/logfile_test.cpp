#include <QtTest>

#include "mainwindow.h"

using namespace Ciqtek;

class LogFileTest final : public QObject
{
    Q_OBJECT

private slots:
    void reusesOpenFileForRepeatedPortLogs();
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

QTEST_MAIN(LogFileTest)
#include "logfile_test.moc"
