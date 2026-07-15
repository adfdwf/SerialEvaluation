#include "tcpclientworker.h"

#include <utility>

BEGIN_NAMESPACE_CIQTEK

/**
 * @brief 创建 TCP worker 并保存目标地址。
 * @param host 目标主机地址。
 * @param port 目标 TCP 端口。
 * @param parent Qt 父对象。
 */
TcpClientWorker::TcpClientWorker(QString host, quint16 port, QObject *parent)
    : QObject(parent), m_host(std::move(host)), m_port(port)
{
}

/**
 * @brief 关闭连接并释放 socket。
 */
TcpClientWorker::~TcpClientWorker()
{
    disconnect();
    delete m_socket;
}

/**
 * @brief 创建 socket 并向目标主机发起异步连接。
 */
void TcpClientWorker::connect()
{
    if (!m_socket) {
        // socket 必须在 worker 所在线程创建，保证 readyRead 等事件在该线程处理。
        m_socket = new QTcpSocket();
        m_socket->moveToThread(thread());

        // 连接成功信号直接转发给主窗口。
        QObject::connect(m_socket, &QTcpSocket::connected, this, &TcpClientWorker::signalConnected);
        // 断开时清理未完成帧并通知主窗口。
        QObject::connect(m_socket, &QTcpSocket::disconnected, this, [this]() {
            const int incompleteBytes = m_frameDecoder.bufferedByteCount();
            m_frameDecoder.clear();
            if (incompleteBytes > 0) {
                emit signalErrorOccurred(QStringLiteral("TCP connection closed; %1 bytes of incomplete response discarded").arg(incompleteBytes));
            }
            emit signalDisconnected();
        });
        // readyRead 每次读取所有可用字节，再交给协议解码器处理粘包和拆包。
        QObject::connect(m_socket, &QTcpSocket::readyRead, this, [this]() {
            const ProtocolFrameDecoder::DecodeResult result = m_frameDecoder.appendData(m_socket->readAll());
            for (const QString &error : result.errors) {
                emit signalErrorOccurred(error);
            }
            for (const QByteArray &frame : result.frames) {
                emit signalDataReceived(frame);
            }
        });
        // 将 socket 写入完成事件转发给上层。
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

    // 已连接或正在连接时不重复发起连接请求。
    if (m_socket->state() == QAbstractSocket::ConnectedState ||
        m_socket->state() == QAbstractSocket::ConnectingState) {
        return;
    }

    m_frameDecoder.clear();
    m_socket->connectToHost(m_host, m_port);
}

/**
 * @brief 请求 socket 关闭 TCP 连接。
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
 * @brief 向已连接 socket 写入一段原始 TCP 数据。
 * @param data 待写入的数据。
 */
void TcpClientWorker::sendData(const QByteArray &data)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        emit signalErrorOccurred(QStringLiteral("TCP is not connected; send failed"));
        return;
    }

    const qint64 written = m_socket->write(data);
    if (written < 0) {
        emit signalErrorOccurred(QStringLiteral("TCP write failed: %1").arg(m_socket->errorString()));
        return;
    }

    m_socket->flush();
}

/**
 * @brief 查询 TCP socket 当前是否已经连接。
 * @return true 表示 socket 状态为 ConnectedState。
 */
bool TcpClientWorker::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

END_NAMESPACE_CIQTEK
