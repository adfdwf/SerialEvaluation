#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "namespace.h"
#include "statisticsmanager.h"

#include <QByteArray>
#include <QLineEdit>
#include <QMainWindow>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QVector>
#include <QRadioButton>
#include <QWidget>

class QTableWidget;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

BEGIN_NAMESPACE_CIQTEK

class ICommunicationInterface;

struct CommandItem {
    QString text;
    bool hexMode = false;
    bool valid = true;
};

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private Q_SLOTS:
    void slotModeChanged(int index);
    void slotRefreshSerialPorts();
    void slotConnectClicked();
    void slotDisconnectClicked();
    void slotStartClicked();
    void slotStopClicked();
    void slotSendNextPacket();
    void slotCheckTimeouts();
    void slotWorkerConnected();
    void slotWorkerDisconnected();
    void slotWorkerDataReceived(const QByteArray &data);
    void slotWorkerError(const QString &message);


    void slotAddCommandRow();
    void slotRemoveCommandRow();
    void slotCommandTextChanged();
    void slotHexModeToggled();

private:
    enum class ConnectionState {
        Disconnected,
        Connecting,
        Connected
    };

    enum class LogLevel {
        Tx,
        Rx,
        Error,
        Info
    };

    void setupUiLogic();
    void applyIndustrialTheme();
    void setConnectionState(ConnectionState state);
    void createWorker();
    void destroyWorker();
    void updateStatsView();
    void appendLog(LogLevel level, const QString &text, qint64 elapsedMs = -1);
    void stopTest(bool manualStop);
    void finalizeTestReport();
    void checkFinishAfterLimit();
    QString currentModeDescription() const;
    QString currentConfigDescription() const;
    QByteArray buildPayload();
    QString payloadToDisplay(const QByteArray &payload);
    QString htmlEscape(const QString &value);

    void setupCommandTable();
    void populateCommandTableFromList();
    QList<CommandItem> collectCommands();
    static bool isValidHexChar(QChar c);
    bool validateHexSyntax(const QString &text);
    void setupStatsLabels();
    void scheduleStatsRefresh();
    void scheduleNextSend(int intervalMs);

private:
    Ui::MainWindow *ui = nullptr;
    QThread *m_commThread = nullptr;
    QObject *m_workerObject = nullptr;
    ICommunicationInterface *m_commInterface = nullptr;
    bool m_connected = false;
    StatisticsManager m_statistics;
    QTimer *m_sendTimer = nullptr;
    QTimer *m_timeoutTimer = nullptr;
    QTimer *m_statsTimer = nullptr;
    QElapsedTimer m_sendClock;
    qint64 m_nextSendDeadlineMs = 0;
    bool m_testRunning = false;
    bool m_finishingAfterLimit = false;
    int m_currentCommandIndex = 0;
    int m_totalCommands = 0;
    QVector<int> m_perCommandSendCount;
};

END_NAMESPACE_CIQTEK

#endif // MAINWINDOW_H