#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "namespace.h"
#include "serialclientworker.h"
#include "tcpclientworker.h"
#include "statisticsmanager.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QLineEdit>
#include <QMainWindow>
#include <QQueue>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QRadioButton>
#include <QWidget>

class QTableWidget;
class QTabWidget;
class QPushButton;
class QComboBox;
class QGroupBox;
class QTextStream;
class QTextEdit;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

BEGIN_NAMESPACE_CIQTEK

/** 一条命令的输入文本、编码方式和校验结果。 */
struct CommandItem {
    QString text;          ///< UI 中输入的 ASCII 文本或 HEX 字符串。
    bool hexMode = false;  ///< true 表示 text 需要按 HEX 解析。
    bool valid = true;     ///< 当前命令是否通过语法和协议校验。
};

/** 一个 TCP 端口的 UI、线程、发送调度和统计状态。 */
struct TcpPortSession {
    quint16 port = 0;                         ///< TCP 端口号。
    TcpClientWorker *worker = nullptr;         ///< TCP worker 对象。
    bool connected = false;                   ///< 是否已连接。
    bool connecting = false;                  ///< 是否正在连接。
    QTimer *sendTimer = nullptr;              ///< 连续发送和单次发送共用的精准定时器。
    QTableWidget *commandTable = nullptr;     ///< 当前端口的命令表格。
    QTextEdit *logEdit = nullptr;              ///< 当前 TCP 端口独立日志区域。
    QWidget *commandPage = nullptr;           ///< 当前端口的标签页。
    StatisticsManager statistics;             ///< 当前端口独立统计管理器。
    QElapsedTimer sendClock;                  ///< 当前端口的发送调度单调时钟。
    qint64 nextSendDeadlineMs = 0;            ///< 下一次连续发送目标时间。
    bool testRunning = false;                 ///< 是否参与连续性能测试。
    bool finishingAfterLimit = false;         ///< 是否达到发送上限并等待响应。
    int currentCommandIndex = 0;              ///< 下一条连续发送命令的下标。
    QVector<int> perCommandSendCount;         ///< 每条命令已经发送的次数。
    QQueue<CommandItem> oneShotCommands;      ///< 单端口 Send All 尚未发送的命令队列。
    bool oneShotRunning = false;              ///< 是否正在执行单次批量发送。
    bool awaitingResponse = false;             ///< 当前任务是否正在等待 recv 或 Timeout。
    bool statisticsValid = true;               ///< 本轮测试是否可以生成耗时统计。
    qint64 nextOneShotDeadlineMs = 0;          ///< 下一条单次命令的目标发送时间。
};

/** 一个串口的配置控件、worker、发送调度和独立统计状态。 */
struct SerialPortSession {
    QString portName;                         ///< 串口名称，例如 COM3 或 /dev/ttyUSB0。
    SerialClientWorker *worker = nullptr;     ///< 串口 worker 对象。
    bool connected = false;                   ///< 是否已打开串口。
    bool connecting = false;                  ///< 是否正在打开串口。
    QTimer *sendTimer = nullptr;              ///< 该串口的精准发送定时器。
    QTableWidget *commandTable = nullptr;     ///< 该串口独立命令表格。
    QTextEdit *logEdit = nullptr;              ///< 当前串口独立日志区域。
    QWidget *commandPage = nullptr;           ///< 该串口独立标签页。
    QComboBox *portCombo = nullptr;            ///< 串口名称展示控件。
    QComboBox *baudCombo = nullptr;            ///< 波特率选择控件。
    QComboBox *dataBitsCombo = nullptr;        ///< 数据位选择控件。
    QComboBox *stopBitsCombo = nullptr;        ///< 停止位选择控件。
    QComboBox *parityCombo = nullptr;          ///< 校验位选择控件。
    StatisticsManager statistics;              ///< 该串口独立的响应统计。
    QElapsedTimer sendClock;                  ///< 该串口的发送调度单调时钟。
    qint64 nextSendDeadlineMs = 0;             ///< 下一次连续发送目标时间。
    bool testRunning = false;                 ///< 是否参与连续性能测试。
    bool finishingAfterLimit = false;         ///< 是否达到发送上限并等待响应。
    int currentCommandIndex = 0;              ///< 下一条连续发送命令的下标。
    QVector<int> perCommandSendCount;         ///< 每条命令已经发送的次数。
    QQueue<CommandItem> oneShotCommands;      ///< 单串口 Send All 尚未发送的命令队列。
    bool oneShotRunning = false;              ///< 是否正在执行单次批量发送。
    bool awaitingResponse = false;             ///< 当前任务是否正在等待 recv 或 Timeout。
    bool statisticsValid = true;               ///< 本轮测试是否可以生成耗时统计。
    qint64 nextOneShotDeadlineMs = 0;          ///< 下一条单次命令的目标发送时间。
};

/**
 * @brief CommBench Pro 主窗口。
 *
 * 负责模式切换、动态端口 UI、连接线程、发送调度、实时统计和日志报告。
 */
class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    /** @brief 创建并初始化主窗口。 */
    explicit MainWindow(QWidget *parent = nullptr);
    /** @brief 停止测试、关闭 worker 并释放动态控件。 */
    ~MainWindow() override;

private Q_SLOTS:
    /** @brief 响应 TCP/串口模式切换。 */
    void slotModeChanged(int index);
    /** @brief 刷新系统当前可用的串口列表。 */
    void slotRefreshSerialPorts();
    /** @brief 连接当前模式下的全部端口。 */
    void slotConnectClicked();
    /** @brief 断开当前模式下的全部端口。 */
    void slotDisconnectClicked();
    /** @brief 启动当前模式下的连续发送测试。 */
    void slotStartClicked();
    /** @brief 停止连续发送测试。 */
    void slotStopClicked();
    /** @brief 周期检查各端口的响应超时和测试完成状态。 */
    void slotCheckTimeouts();
    /** @brief 动态添加 TCP 端口。 */
    void slotAddTcpPort();
    /** @brief 批量添加 TCP 端口。 */
    void slotBatchAddTcpPorts();
    /** @brief 删除选中的 TCP 端口。 */
    void slotRemoveTcpPort();
    /** @brief 对选中的 TCP 端口执行 Send All。 */
    void slotSendSelectedTcpPort();
    /** @brief 对全部 TCP 端口执行 Send All。 */
    void slotSendAllTcpPorts();
    /** @brief 动态添加串口。 */
    void slotAddSerialPort();
    /** @brief 删除选中的串口。 */
    void slotRemoveSerialPort();
    /** @brief 对全部串口执行 Send All。 */
    void slotSendAllSerialPorts();

private:
    /** 全局连接状态，用于顶部 LED 和按钮状态。 */
    enum class ConnectionState {
        Disconnected, ///< 没有活动连接。
        Connecting,   ///< 至少一个端口正在连接。
        Connected     ///< 当前模式下全部端口已连接。
    };

    /** 屏幕日志的分类和颜色映射。 */
    enum class LogLevel {
        Tx,    ///< 发送数据。
        Rx,    ///< 接收数据。
        Error, ///< 错误信息。
        Info   ///< 普通状态信息。
    };

    /** @brief 初始化公共定时器、控件范围和信号连接。 */
    void setupUiLogic();
    /** @brief 应用窗口标题和基础工业风格。 */
    void applyIndustrialTheme();
    /** @brief 刷新各端口统计卡片。 */
    void updateStatsView();
    /** @brief 将一条日志写入 UI，并限制屏幕预览长度。 */
    void appendLog(LogLevel level, const QString &text, qint64 elapsedMs = -1);
    /** @brief 根据 ASCII/HEX 格式将响应转换为日志显示文本。 */
    QString payloadToDisplay(const QByteArray &payload,
                             const QString &format = QString(),
                             bool truncate = true);

    /** @brief 判断一个字符是否为十六进制字符。 */
    static bool isValidHexChar(QChar c);
    /** @brief 校验 HEX 文本的偶数位和字符语法。 */
    static bool validateHexSyntax(const QString &text);
    /** @brief 校验 A0 81 帧长度和校验和。 */
    static bool validateHexPayload(const QString &text);
    /** @brief 请求统计面板在下一次定时刷新。 */
    void scheduleStatsRefresh();

    /** @brief 判断当前是否为 TCP Network 模式。 */
    bool isTcpMode() const;
    /** @brief 创建 TCP 多端口动态 UI。 */
    void setupTcpPortUi();
    /** @brief 创建一个 TCP 端口会话和对应标签页。 */
    void addTcpPort(quint16 port);
    /** @brief 安全删除一个 TCP 端口会话。 */
    void removeTcpPort(quint16 port);
    /** @brief 为 TCP 会话添加一条命令。 */
    void addTcpCommand(TcpPortSession *session);
    /** @brief 删除 TCP 会话当前选中的命令。 */
    void removeTcpCommand(TcpPortSession *session);
    /** @brief 从任意命令表收集命令并判断其格式。 */
    QList<CommandItem> collectCommands(QTableWidget *table) const;
    /** @brief 检查命令列表非空且所有命令合法。 */
    bool validateCommands(const QList<CommandItem> &commands);
    /** @brief 并发连接全部 TCP 端口。 */
    void connectTcpPorts();
    /** @brief 断开全部 TCP 端口。 */
    void disconnectTcpPorts();
    /** @brief 停止并释放全部 TCP worker。 */
    void destroyTcpWorkers();
    /** @brief 启动全部 TCP 会话的连续测试。 */
    void startTcpTest();
    /** @brief 停止 TCP 连续测试并生成报告。 */
    void stopTcpTest(bool manualStop);
    /** @brief 将一个 TCP 会话的全部命令排入单次发送队列。 */
    void sendAllTcpPort(TcpPortSession *session);
    /** @brief 发送 TCP 单次队列中的下一条命令。 */
    void sendNextOneShotTcpPacket(TcpPortSession *session);
    /** @brief 安排 TCP 单次队列的下一条命令。 */
    void scheduleNextOneShotTcpPacket(TcpPortSession *session, int intervalMs);
    /** @brief 发送 TCP 连续测试的下一条命令。 */
    void sendNextTcpPacket(TcpPortSession *session);
    /** @brief 安排 TCP 连续测试的下一次发送。 */
    void scheduleNextTcpPacket(TcpPortSession *session, int intervalMs);
    /** @brief 检查全部 TCP 会话的响应超时。 */
    void checkTcpTimeouts();
    /** @brief 更新 TCP 端口表格中的状态文本。 */
    void updateTcpPortRow(TcpPortSession *session, const QString &state);
    /** @brief 汇总 TCP 会话连接状态并更新顶部控件。 */
    void updateTcpConnectionState();
    /** @brief 创建串口多端口动态 UI。 */
    void setupSerialPortUi();
    /** @brief 创建一个串口会话和对应标签页。 */
    void addSerialPort(const QString &portName);
    /** @brief 关闭并删除一个串口会话。 */
    void removeSerialPort(const QString &portName);
    /** @brief 为串口会话添加一条命令。 */
    void addSerialCommand(SerialPortSession *session);
    /** @brief 删除串口会话当前选中的命令。 */
    void removeSerialCommand(SerialPortSession *session);
    /** @brief 从串口会话命令表收集命令。 */
    QList<CommandItem> collectSerialCommands(const SerialPortSession *session) const;
    /** @brief 并发打开全部串口。 */
    void connectSerialPorts();
    /** @brief 安全关闭全部串口。 */
    void disconnectSerialPorts();
    /** @brief 关闭一个串口 worker 的线程和设备。 */
    void destroySerialWorker(SerialPortSession *session);
    /** @brief 关闭全部串口 worker。 */
    void destroySerialWorkers();
    /** @brief 从串口标签页控件读取 SerialSettings。 */
    SerialSettings serialSettings(const SerialPortSession *session) const;
    /** @brief 更新串口表格中的连接状态。 */
    void updateSerialPortRow(SerialPortSession *session, const QString &state);
    /** @brief 汇总串口连接状态并更新顶部控件。 */
    void updateSerialConnectionState();
    /** @brief 将单串口全部命令排入一次发送队列。 */
    void sendAllSerialPort(SerialPortSession *session);
    /** @brief 发送串口单次队列的下一条命令。 */
    void sendNextOneShotSerialPacket(SerialPortSession *session);
    /** @brief 安排串口单次队列的下一条命令。 */
    void scheduleNextOneShotSerialPacket(SerialPortSession *session, int intervalMs);
    /** @brief 启动全部已连接串口的连续测试。 */
    void startSerialTest();
    /** @brief 停止串口连续测试并生成报告。 */
    void stopSerialTest(bool manualStop);
    /** @brief 发送一个串口连续测试周期的下一条命令。 */
    void sendNextSerialPacket(SerialPortSession *session);
    /** @brief 安排串口连续测试的下一次发送。 */
    void scheduleNextSerialPacket(SerialPortSession *session, int intervalMs);
    /** @brief 检查全部串口会话的响应超时。 */
    void checkSerialTimeouts();
    /** @brief 更新单个串口标签页的统计卡片。 */
    void updateSerialSessionStats(SerialPortSession *session);
    /** @brief 更新单个 TCP 端口标签页的实时统计卡片。 */
    void updateTcpSessionStats(TcpPortSession *session);
    /** @brief 刷新每个串口标签页中的可用端口候选项。 */
    void refreshSerialPortChoices();
    /** @brief 将一条事件直接追加到对应端口的本地日志文件。 */
    void appendLogFile(const QString &portTag, const QString &line);
    /** @brief 将端口日志事件显示到对应的独立日志区域。 */
    void appendPortLog(const QString &portTag, const QString &line, const QString &color);
    /** @brief 将当前 TCP 端口的全部界面参数写入该端口日志。 */
    void logTcpSessionParameters(const TcpPortSession *session, const QString &testType);
    /** @brief 将当前串口的全部界面参数写入该串口日志。 */
    void logSerialSessionParameters(const SerialPortSession *session, const QString &testType);

private:
    Ui::MainWindow *ui = nullptr;                         ///< Qt Designer 生成的控件集合。
    bool m_connected = false;                              ///< 当前模式的全局连接标志。
    QTimer *m_timeoutTimer = nullptr;                      ///< 全局超时检查定时器。
    QTimer *m_statsTimer = nullptr;                        ///< 实时统计刷新定时器。
    bool m_testRunning = false;                             ///< 当前是否有连续测试运行。
    QHash<quint16, TcpPortSession *> m_tcpSessions;        ///< TCP 端口号到会话对象的映射。
    QTableWidget *m_tcpPortTable = nullptr;                 ///< TCP 端口状态表格。
    QTabWidget *m_tcpCommandTabs = nullptr;                 ///< TCP 端口命令标签页。
    QPushButton *m_tcpAddPortButton = nullptr;              ///< TCP 添加端口按钮。
    QPushButton *m_tcpBatchAddPortButton = nullptr;         ///< TCP 批量添加端口按钮。
    QPushButton *m_tcpRemovePortButton = nullptr;           ///< TCP 删除端口按钮。
    QPushButton *m_tcpSendAllButton = nullptr;              ///< TCP 全局 Send All 按钮。
    QHash<QString, SerialPortSession *> m_serialSessions;  ///< 串口名到会话对象的映射。
    QGroupBox *m_serialPortBox = nullptr;                   ///< 串口动态区域容器。
    QTableWidget *m_serialPortTable = nullptr;              ///< 串口状态表格。
    QTabWidget *m_serialCommandTabs = nullptr;              ///< 串口命令标签页。
    QPushButton *m_serialAddPortButton = nullptr;           ///< 串口添加按钮。
    QPushButton *m_serialRemovePortButton = nullptr;        ///< 串口删除按钮。
    QPushButton *m_serialRefreshButton = nullptr;           ///< 串口刷新按钮。
    QPushButton *m_serialSendAllButton = nullptr;           ///< 串口全局 Send All 按钮。
    QTimer *m_serialHotplugTimer = nullptr;                 ///< 串口热插拔轮询定时器。
    QHash<QString, QString> m_logFilePaths;                 ///< 端口到当前本地日志文件路径的映射。
    QHash<quint16, StatisticsSnapshot> m_tcpFinalStats;     ///< TCP worker 停止时返回的最终统计。
    QHash<QString, StatisticsSnapshot> m_serialFinalStats;  ///< 串口 worker 停止时返回的最终统计。
};

END_NAMESPACE_CIQTEK

#endif // MAINWINDOW_H
