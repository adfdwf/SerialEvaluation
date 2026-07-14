#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "namespace.h"
#include "serialclientworker.h"
#include "statisticsmanager.h"

#include <QByteArray>
#include <QHash>
#include <QLineEdit>
#include <QMainWindow>
#include <QQueue>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QVector>
#include <QRadioButton>
#include <QWidget>

class QTableWidget;
class QTabWidget;
class QPushButton;
class QComboBox;
class QGroupBox;

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

struct TcpPortSession {
    quint16 port = 0;
    QThread *thread = nullptr;
    QObject *worker = nullptr;
    bool connected = false;
    bool connecting = false;
    QTimer *sendTimer = nullptr;
    QTableWidget *commandTable = nullptr;
    QWidget *commandPage = nullptr;
    StatisticsManager statistics;
    QElapsedTimer sendClock;
    qint64 nextSendDeadlineMs = 0;
    bool testRunning = false;
    bool finishingAfterLimit = false;
    int currentCommandIndex = 0;
    QVector<int> perCommandSendCount;
    QQueue<CommandItem> oneShotCommands;
    bool oneShotRunning = false;
    qint64 nextOneShotDeadlineMs = 0;
};

struct SerialPortSession {
    QString portName;
    QThread *thread = nullptr;
    SerialClientWorker *worker = nullptr;
    bool connected = false;
    bool connecting = false;
    QTimer *sendTimer = nullptr;
    QTableWidget *commandTable = nullptr;
    QWidget *commandPage = nullptr;
    QComboBox *portCombo = nullptr;
    QComboBox *baudCombo = nullptr;
    QComboBox *dataBitsCombo = nullptr;
    QComboBox *stopBitsCombo = nullptr;
    QComboBox *parityCombo = nullptr;
    StatisticsManager statistics;
    QElapsedTimer sendClock;
    qint64 nextSendDeadlineMs = 0;
    bool testRunning = false;
    bool finishingAfterLimit = false;
    int currentCommandIndex = 0;
    QVector<int> perCommandSendCount;
    QQueue<CommandItem> oneShotCommands;
    bool oneShotRunning = false;
    qint64 nextOneShotDeadlineMs = 0;
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
    void slotAddTcpPort();
    void slotRemoveTcpPort();
    void slotSendSelectedTcpPort();
    void slotSendAllTcpPorts();
    void slotAddSerialPort();
    void slotRemoveSerialPort();
    void slotSendAllSerialPorts();

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
    QString payloadToDisplay(const QByteArray &payload,
                             const QString &format = QString());
    QString htmlEscape(const QString &value);

    void setupCommandTable();
    void populateCommandTableFromList();
    QList<CommandItem> collectCommands();
    static bool isValidHexChar(QChar c);
    static bool validateHexSyntax(const QString &text);
    static bool validateHexPayload(const QString &text);
    void setupStatsLabels();
    void scheduleStatsRefresh();
    void scheduleNextSend(int intervalMs);

    bool isTcpMode() const;
    void setupTcpPortUi();
    void addTcpPort(quint16 port);
    void removeTcpPort(quint16 port);
    void addTcpCommand(TcpPortSession *session);
    void removeTcpCommand(TcpPortSession *session);
    QList<CommandItem> collectCommands(QTableWidget *table) const;
    bool validateCommands(const QList<CommandItem> &commands);
    void connectTcpPorts();
    void disconnectTcpPorts();
    void destroyTcpWorkers();
    void startTcpTest();
    void stopTcpTest(bool manualStop);
    void sendAllTcpPort(TcpPortSession *session);
    void sendNextOneShotTcpPacket(TcpPortSession *session);
    void scheduleNextOneShotTcpPacket(TcpPortSession *session, int intervalMs);
    void sendNextTcpPacket(TcpPortSession *session);
    void scheduleNextTcpPacket(TcpPortSession *session, int intervalMs);
    void checkTcpTimeouts();
    void updateTcpPortRow(TcpPortSession *session, const QString &state);
    void updateTcpConnectionState();
    void finalizeTcpReport();
    void setupSerialPortUi();
    void addSerialPort(const QString &portName);
    void removeSerialPort(const QString &portName);
    void addSerialCommand(SerialPortSession *session);
    void removeSerialCommand(SerialPortSession *session);
    QList<CommandItem> collectSerialCommands(const SerialPortSession *session) const;
    void connectSerialPorts();
    void disconnectSerialPorts();
    void destroySerialWorker(SerialPortSession *session);
    void destroySerialWorkers();
    SerialSettings serialSettings(const SerialPortSession *session) const;
    void updateSerialPortRow(SerialPortSession *session, const QString &state);
    void updateSerialConnectionState();
    void sendAllSerialPort(SerialPortSession *session);
    void sendNextOneShotSerialPacket(SerialPortSession *session);
    void scheduleNextOneShotSerialPacket(SerialPortSession *session, int intervalMs);
    void startSerialTest();
    void stopSerialTest(bool manualStop);
    void sendNextSerialPacket(SerialPortSession *session);
    void scheduleNextSerialPacket(SerialPortSession *session, int intervalMs);
    void checkSerialTimeouts();
    void updateSerialSessionStats(SerialPortSession *session);
    void finalizeSerialReport();
    void refreshSerialPortChoices();

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
    QHash<quint16, TcpPortSession *> m_tcpSessions;
    QTableWidget *m_tcpPortTable = nullptr;
    QTabWidget *m_tcpCommandTabs = nullptr;
    QPushButton *m_tcpAddPortButton = nullptr;
    QPushButton *m_tcpRemovePortButton = nullptr;
    QPushButton *m_tcpSendAllButton = nullptr;
    QHash<QString, SerialPortSession *> m_serialSessions;
    QGroupBox *m_serialPortBox = nullptr;
    QTableWidget *m_serialPortTable = nullptr;
    QTabWidget *m_serialCommandTabs = nullptr;
    QPushButton *m_serialAddPortButton = nullptr;
    QPushButton *m_serialRemovePortButton = nullptr;
    QPushButton *m_serialRefreshButton = nullptr;
    QPushButton *m_serialSendAllButton = nullptr;
    QTimer *m_serialHotplugTimer = nullptr;
};

END_NAMESPACE_CIQTEK

#endif // MAINWINDOW_H
