#ifndef TCPCLIENTWORKER_H
#define TCPCLIENTWORKER_H

#include "icommunicationinterface.h"
#include "namespace.h"
#include "statisticsmanager.h"

#include <QObject>
#include <QString>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>

BEGIN_NAMESPACE_CIQTEK

/**
 * @brief 基于任务队列的 TCP I/O worker。
 *
 * worker 对象本身保留在创建它的线程中，不迁移 QObject。所有连接、
 * 发送和断开请求先进入线程安全队列，内部 I/O 线程轮询队列并用阻塞式
 * QTcpSocket 完成 send/recv。这样每个端口的 socket 始终只由一个线程访问。
 */
class TcpClientWorker final : public QObject, public ICommunicationInterface
{
    Q_OBJECT

public:
    explicit TcpClientWorker(QString host, quint16 port, QObject *parent = nullptr);
    ~TcpClientWorker() override;

    /** @brief 设置 I/O 线程等待响应的超时时间（毫秒）。 */
    void setReceiveTimeout(int timeoutMs);
    /** @brief 清空本端口线程的统计数据。 */
    void resetStatistics();
    /** @brief 结束本轮统计并返回最终快照。 */
    bool finalStatisticsSnapshot(StatisticsSnapshot *snapshot);
    /** @brief 将带格式信息的发送任务放入队列。 */
    void sendDataWithFormat(const QByteArray &data, const QString &format);

public Q_SLOTS:
    /** @brief 将连接请求放入线程安全任务队列。 */
    void connect() override;
    /** @brief 将断开请求放入线程安全任务队列。 */
    void disconnect() override;
    /** @brief 将发送请求放入线程安全任务队列。 */
    void sendData(const QByteArray &data) override;
    /** @brief 返回 I/O 线程维护的连接状态。 */
    bool isConnected() const override;

Q_SIGNALS:
    void signalConnected();
    void signalDisconnected();
    void signalDataReceived(const QByteArray &data);
    void signalErrorOccurred(const QString &message);
    void signalBytesWritten(qint64 bytes);
    void signalReceiveTimeout();

private:
    enum class TaskType { Connect, Disconnect, Send };
    struct Task { TaskType type; QByteArray data; QString format; };

    void enqueue(Task task);
    void markStatisticsInvalid();
    void reportError(const QString &message);
    void ioLoop();

    QString m_host;
    quint16 m_port = 0;
    std::atomic_bool m_stopping{false};
    std::atomic_bool m_connected{false};
    std::atomic_int m_receiveTimeoutMs{300};
    mutable std::mutex m_queueMutex;
    std::condition_variable m_queueCondition;
    std::deque<Task> m_tasks;
    std::thread m_ioThread;
    mutable std::mutex m_statsMutex;
    StatisticsManager m_statistics;
    bool m_statisticsValid = true;
};

END_NAMESPACE_CIQTEK

#endif // TCPCLIENTWORKER_H
