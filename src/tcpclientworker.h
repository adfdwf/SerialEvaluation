#ifndef TCPCLIENTWORKER_H
#define TCPCLIENTWORKER_H

#include "icommunicationinterface.h"
#include "namespace.h"
#include "protocolframedecoder.h"

#include <QObject>
#include <QTcpSocket>
#include <QString>

BEGIN_NAMESPACE_CIQTEK

/**
 * @brief 在独立线程中执行 TCP 连接、收发和协议拆帧的 worker。
 *
 * worker 只负责字节流传输和协议帧边界识别，发送节奏、统计和 UI 更新由
 * MainWindow 在主线程中负责。
 */
class TcpClientWorker final : public QObject, public ICommunicationInterface
{
    Q_OBJECT

public:
    /**
     * @brief 创建 TCP worker。
     * @param host 目标主机地址。
     * @param port 目标 TCP 端口。
     * @param parent Qt 父对象。
     */
    explicit TcpClientWorker(QString host, quint16 port, QObject *parent = nullptr);

    /** @brief 关闭连接并销毁 socket。 */
    ~TcpClientWorker() override;

public Q_SLOTS:
    /** @brief 异步建立 TCP 连接。 */
    void connect() override;

    /** @brief 关闭当前 TCP 连接。 */
    void disconnect() override;

    /**
     * @brief 向 TCP socket 写入原始数据。
     * @param data 待发送的字节数组。
     */
    void sendData(const QByteArray &data) override;

    /**
     * @brief 查询 socket 是否处于已连接状态。
     * @return true 表示已连接。
     */
    bool isConnected() const override;

Q_SIGNALS:
    /** @brief TCP 连接成功时发出。 */
    void signalConnected();

    /** @brief TCP 连接断开时发出。 */
    void signalDisconnected();

    /**
     * @brief 收到一个完整协议帧或原始数据块时发出。
     * @param data 已完成拆帧的数据。
     */
    void signalDataReceived(const QByteArray &data);

    /**
     * @brief 发生 socket 或协议错误时发出。
     * @param message 可直接展示给用户的错误信息。
     */
    void signalErrorOccurred(const QString &message);

    /**
     * @brief socket 完成写入后发出。
     * @param bytes 本次写入的字节数。
     */
    void signalBytesWritten(qint64 bytes);

private:
    QString m_host;                         ///< 目标主机地址。
    quint16 m_port = 0;                     ///< 目标 TCP 端口号。
    ProtocolFrameDecoder m_frameDecoder;    ///< TCP 字节流协议拆帧器。
    QTcpSocket *m_socket = nullptr;         ///< Qt TCP socket，由 worker 线程拥有。
};

END_NAMESPACE_CIQTEK

#endif // TCPCLIENTWORKER_H
