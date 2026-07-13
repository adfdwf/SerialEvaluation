#ifndef TCPCLIENTWORKER_H
#define TCPCLIENTWORKER_H

#include "protocolframedecoder.h"
#include "icommunicationinterface.h"
#include "namespace.h"

#include <QObject>
#include <QTcpSocket>
#include <QString>

BEGIN_NAMESPACE_CIQTEK

class TcpClientWorker final : public QObject, public ICommunicationInterface
{
    Q_OBJECT
public:
    /**
     * @brief  TcpClientWorker 默认构造函数
     * @param  host 目标主机地址
     * @param  port 目标端口号
     * @param  parent Qt父对象
     * @return void
     */
    explicit TcpClientWorker(QString host, quint16 port, QObject *parent = nullptr);

    /**
     * @brief  ~TcpClientWorker 默认析构函数
     * @return void
     */
    ~TcpClientWorker() override;

public Q_SLOTS:
    /**
     * @brief  connect 建立 TCP 连接
     * @return void
     */
    void connect() override;

    /**
     * @brief  disconnect 断开 TCP 连接
     * @return void
     */
    void disconnect() override;

    /**
     * @brief  sendData 发送 TCP 数据
     * @param  data 待发送字节数据
     * @return void
     */
    void sendData(const QByteArray &data) override;

    /**
     * @brief  isConnected 获取 TCP 连接状态
     * @return bool true表示套接字已连接
     */
    bool isConnected() const override;

Q_SIGNALS:
    /**
     * @brief  signalConnected TCP 连接成功信号
     * @return void
     */
    void signalConnected();

    /**
     * @brief  signalDisconnected TCP 断开连接信号
     * @return void
     */
    void signalDisconnected();

    /**
     * @brief  signalDataReceived TCP 数据接收信号
     * @param  data 接收到的字节数据
     * @return void
     */
    void signalDataReceived(const QByteArray &data);

    /**
     * @brief  signalErrorOccurred TCP 错误信号
     * @param  message 错误文本
     * @return void
     */
    void signalErrorOccurred(const QString &message);

    /**
     * @brief  signalBytesWritten TCP 写入完成信号
     * @param  bytes 已写入字节数
     * @return void
     */
    void signalBytesWritten(qint64 bytes);

private:
    /** 目标主机地址 */
    QString m_host;

    /** 目标端口号 */
    quint16 m_port;

    /** TCP byte stream protocol frame decoder */
    ProtocolFrameDecoder m_frameDecoder;

    /** Qt TCP 套接字对象 */
    QTcpSocket *m_socket = nullptr;
};

END_NAMESPACE_CIQTEK

#endif // TCPCLIENTWORKER_H