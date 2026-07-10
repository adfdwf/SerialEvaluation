#include "tcpclientworker.h"

#include <utility>

BEGIN_NAMESPACE_CIQTEK

/**
 * @brief  TcpClientWorker 默认构造函数
 * @param  host 目标主机地址
 * @param  port 目标端口号
 * @param  parent Qt父对象
 * @return void
 */
TcpClientWorker::TcpClientWorker(QString host, quint16 port, QObject *parent)
    : QObject(parent), m_host(std::move(host)), m_port(port)
{
}

/**
 * @brief  ~TcpClientWorker 默认析构函数
 * @return void
 */
TcpClientWorker::~TcpClientWorker()
{
    disconnect();
    delete m_socket;
}

/**
 * @brief  connect 建立 TCP 连接
 * @return void
 */
void TcpClientWorker::connect()
{
    if (!m_socket) {
        m_socket = new QTcpSocket();
        m_socket->moveToThread(thread());

        QObject::connect(m_socket, &QTcpSocket::connected, this, &TcpClientWorker::signalConnected);
        QObject::connect(m_socket, &QTcpSocket::disconnected, this, &TcpClientWorker::signalDisconnected);
        QObject::connect(m_socket, &QTcpSocket::readyRead, this, [this]() {
            emit signalDataReceived(m_socket->readAll());
        });
        QObject::connect(m_socket, &QTcpSocket::bytesWritten, this, &TcpClientWorker::signalBytesWritten);
        QObject::connect(m_socket,
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
                         &QTcpSocket::errorOccurred,
#else
                         QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
#endif
                         this,
                         [this](QAbstractSocket::SocketError) {
                             emit signalErrorOccurred(m_socket ? m_socket->errorString() : QStringLiteral("TCP socket error"));
                         });
    }

    if (m_socket->state() == QAbstractSocket::ConnectedState ||
        m_socket->state() == QAbstractSocket::ConnectingState) {
        return;
    }

    m_socket->connectToHost(m_host, m_port);
}

/**
 * @brief  disconnect 断开 TCP 连接
 * @return void
 */
void TcpClientWorker::disconnect()
{
    if (!m_socket) {
        return;
    }

    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }
}

/**
 * @brief  sendData 发送 TCP 数据
 * @param  data 待发送字节数据
 * @return void
 */
void TcpClientWorker::sendData(const QByteArray &data)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        emit signalErrorOccurred(QStringLiteral("TCP 未连接，发送失败"));
        return;
    }

    const qint64 written = m_socket->write(data);
    if (written < 0) {
        emit signalErrorOccurred(QStringLiteral("TCP 写入失败：%1").arg(m_socket->errorString()));
        return;
    }

    m_socket->flush();
}

/**
 * @brief  isConnected 获取 TCP 连接状态
 * @return bool true表示套接字已连接
 */
bool TcpClientWorker::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

END_NAMESPACE_CIQTEK