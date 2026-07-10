#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "namespace.h"
#include "statisticsmanager.h"

#include <QByteArray>
#include <QMainWindow>
#include <QObject>
#include <QString>
#include <QThread>
#include <QTimer>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

BEGIN_NAMESPACE_CIQTEK

class ICommunicationInterface;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    /**
     * @brief  MainWindow 默认构造函数
     * @param  parent Qt父对象
     * @return void
     */
    explicit MainWindow(QWidget *parent = nullptr);

    /**
     * @brief  ~MainWindow 默认析构函数
     * @return void
     */
    ~MainWindow() override;

private Q_SLOTS:
    /**
     * @brief  slotModeChanged 处理通信模式切换
     * @param  index 当前模式索引
     * @return void
     */
    void slotModeChanged(int index);

    /**
     * @brief  slotRefreshSerialPorts 刷新串口列表
     * @return void
     */
    void slotRefreshSerialPorts();

    /**
     * @brief  slotConnectClicked 处理连接按钮点击
     * @return void
     */
    void slotConnectClicked();

    /**
     * @brief  slotDisconnectClicked 处理断开按钮点击
     * @return void
     */
    void slotDisconnectClicked();

    /**
     * @brief  slotStartClicked 处理开始测试按钮点击
     * @return void
     */
    void slotStartClicked();

    /**
     * @brief  slotStopClicked 处理停止测试按钮点击
     * @return void
     */
    void slotStopClicked();

    /**
     * @brief  slotSendNextPacket 定时发送下一包数据
     * @return void
     */
    void slotSendNextPacket();

    /**
     * @brief  slotCheckTimeouts 定时检查超时数据包
     * @return void
     */
    void slotCheckTimeouts();

    /**
     * @brief  slotWorkerConnected 处理 worker 连接成功信号
     * @return void
     */
    void slotWorkerConnected();

    /**
     * @brief  slotWorkerDisconnected 处理 worker 断开连接信号
     * @return void
     */
    void slotWorkerDisconnected();

    /**
     * @brief  slotWorkerDataReceived 处理 worker 接收数据信号
     * @param  data 接收数据
     * @return void
     */
    void slotWorkerDataReceived(const QByteArray &data);

    /**
     * @brief  slotWorkerError 处理 worker 错误信号
     * @param  message 错误文本
     * @return void
     */
    void slotWorkerError(const QString &message);

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

    /**
     * @brief  setupUiLogic 初始化界面业务逻辑
     * @return void
     */
    void setupUiLogic();

    /**
     * @brief  applyIndustrialTheme 应用工业风界面样式
     * @return void
     */
    void applyIndustrialTheme();

    /**
     * @brief  setConnectionState 设置连接状态和界面显示
     * @param  state 新连接状态
     * @return void
     */
    void setConnectionState(ConnectionState state);

    /**
     * @brief  createWorker 按当前模式创建通信 worker
     * @return void
     */
    void createWorker();

    /**
     * @brief  destroyWorker 销毁通信 worker 和工作线程
     * @return void
     */
    void destroyWorker();

    /**
     * @brief  updateStatsView 刷新统计数据显示
     * @return void
     */
    void updateStatsView();

    /**
     * @brief  appendLog 向日志窗口追加一条记录
     * @param  level 日志级别
     * @param  text 日志文本
     * @param  elapsedMs 可选耗时，单位毫秒
     * @return void
     */
    void appendLog(LogLevel level, const QString &text, qint64 elapsedMs = -1);

    /**
     * @brief  stopTest 停止当前测试流程
     * @param  manualStop true表示用户主动停止
     * @return void
     */
    void stopTest(bool manualStop);

    /**
     * @brief  finalizeTestReport 生成测试报告文件
     * @return void
     */
    void finalizeTestReport();

    /**
     * @brief  checkFinishAfterLimit 达到发送上限后检查是否可结束测试
     * @return void
     */
    void checkFinishAfterLimit();

    /**
     * @brief  currentModeDescription 获取当前通信模式描述
     * @return QString 通信模式文本
     */
    QString currentModeDescription() const;

    /**
     * @brief  currentConfigDescription 获取当前通信参数描述
     * @return QString 通信参数文本
     */
    QString currentConfigDescription() const;

    /**
     * @brief  buildPayload 根据界面输入构建发送负载
     * @return QByteArray 待发送字节数据
     */
    QByteArray buildPayload() const;

    /**
     * @brief  payloadToDisplay 将负载转换为日志显示文本
     * @param  payload 通信负载
     * @return QString 显示文本
     */
    QString payloadToDisplay(const QByteArray &payload) const;

    /**
     * @brief  htmlEscape 转义日志 HTML 特殊字符
     * @param  value 原始文本
     * @return QString 转义后的文本
     */
    QString htmlEscape(const QString &value) const;

private:
    /** UI对象 */
    Ui::MainWindow *ui = nullptr;

    /** 通信工作线程 */
    QThread *m_commThread = nullptr;

    /** worker Qt对象指针 */
    QObject *m_workerObject = nullptr;

    /** 通信接口指针 */
    ICommunicationInterface *m_commInterface = nullptr;

    /** 当前连接状态标志 */
    bool m_connected = false;

    /** 统计管理器 */
    StatisticsManager m_statistics;

    /** 周期发送定时器 */
    QTimer *m_sendTimer = nullptr;

    /** 超时检查定时器 */
    QTimer *m_timeoutTimer = nullptr;

    /** 测试运行状态标志 */
    bool m_testRunning = false;

    /** 达到发送上限后的收尾状态标志 */
    bool m_finishingAfterLimit = false;
};

END_NAMESPACE_CIQTEK

#endif // MAINWINDOW_H