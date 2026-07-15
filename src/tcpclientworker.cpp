#include "tcpclientworker.h"

#include "protocolframedecoder.h"

#include <QElapsedTimer>
#include <QTcpSocket>
#include <algorithm>
#include <chrono>
#include <utility>

BEGIN_NAMESPACE_CIQTEK

TcpClientWorker::TcpClientWorker(QString host, quint16 port, QObject *parent)
    : QObject(parent), m_host(std::move(host)), m_port(port), m_ioThread(&TcpClientWorker::ioLoop, this)
{
}

TcpClientWorker::~TcpClientWorker()
{
    m_stopping.store(true, std::memory_order_release);
    m_queueCondition.notify_all();
    if (m_ioThread.joinable()) {
        m_ioThread.join();
    }
}

void TcpClientWorker::setReceiveTimeout(int timeoutMs)
{
    m_receiveTimeoutMs.store(std::max(1, timeoutMs), std::memory_order_release);
}

void TcpClientWorker::connect()
{
    enqueue({TaskType::Connect, {}});
}

void TcpClientWorker::disconnect()
{
    enqueue({TaskType::Disconnect, {}});
}

void TcpClientWorker::sendData(const QByteArray &data)
{
    enqueue({TaskType::Send, data});
}

bool TcpClientWorker::isConnected() const
{
    return m_connected.load(std::memory_order_acquire);
}

void TcpClientWorker::enqueue(Task task)
{
    if (m_stopping.load(std::memory_order_acquire)) return;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_tasks.emplace_back(std::move(task));
    }
    m_queueCondition.notify_one();
}

void TcpClientWorker::ioLoop()
{
    QTcpSocket socket;
    ProtocolFrameDecoder decoder;
    bool connected = false;
    bool waitingForResponse = false;
    QElapsedTimer responseTimer;

    const auto closeSocket = [&]() {
        const bool wasConnected = connected;
        if (socket.state() != QAbstractSocket::UnconnectedState) {
            socket.disconnectFromHost();
            socket.waitForDisconnected(100);
        }
        socket.abort();
        connected = false;
        m_connected.store(false, std::memory_order_release);
        decoder.clear();
        waitingForResponse = false;
        if (wasConnected) emit signalDisconnected();
    };

    while (!m_stopping.load(std::memory_order_acquire)) {
        Task task{TaskType::Disconnect, {}};
        bool hasTask = false;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCondition.wait_for(lock, std::chrono::milliseconds(5), [&]() {
                return m_stopping.load(std::memory_order_acquire) || !m_tasks.empty();
            });
            if (!m_tasks.empty()) {
                task = std::move(m_tasks.front());
                m_tasks.pop_front();
                hasTask = true;
            }
        }

        if (hasTask) {
            if (task.type == TaskType::Connect) {
                if (!connected) {
                    decoder.clear();
                    socket.abort();
                    socket.connectToHost(m_host, m_port);
                    QElapsedTimer connectTimer;
                    connectTimer.start();
                    while (!m_stopping.load(std::memory_order_acquire) &&
                           socket.state() == QAbstractSocket::ConnectingState &&
                           connectTimer.elapsed() < 3000) {
                        socket.waitForConnected(20);
                    }
                    if (socket.state() == QAbstractSocket::ConnectedState) {
                        connected = true;
                        m_connected.store(true, std::memory_order_release);
                        emit signalConnected();
                    } else if (!m_stopping.load(std::memory_order_acquire)) {
                        emit signalErrorOccurred(QStringLiteral("TCP connect failed: %1").arg(socket.errorString()));
                    }
                }
            } else if (task.type == TaskType::Disconnect) {
                closeSocket();
            } else if (task.type == TaskType::Send) {
                if (!connected) {
                    emit signalErrorOccurred(QStringLiteral("TCP is not connected; send failed"));
                } else {
                    qint64 totalWritten = 0;
                    bool writeOk = true;
                    while (totalWritten < task.data.size()) {
                        const qint64 written = socket.write(task.data.constData() + totalWritten, task.data.size() - totalWritten);
                        if (written <= 0 || !socket.waitForBytesWritten(1000)) {
                            writeOk = false;
                            break;
                        }
                        totalWritten += written;
                    }
                    if (!writeOk) {
                        emit signalErrorOccurred(QStringLiteral("TCP write failed: %1").arg(socket.errorString()));
                    } else {
                        socket.flush();
                        emit signalBytesWritten(totalWritten);
                        waitingForResponse = true;
                        responseTimer.restart();
                    }
                }
            }
        }

        // 每轮都轮询接收，不依赖 Qt 事件循环；Timeout 仅限制本次等待窗口。
        if (connected) {
            const int pollMs = waitingForResponse
                ? std::max(1, std::min(10, m_receiveTimeoutMs.load(std::memory_order_acquire) - static_cast<int>(responseTimer.elapsed())))
                : 5;
            if (socket.waitForReadyRead(pollMs)) {
                const ProtocolFrameDecoder::DecodeResult result = decoder.appendData(socket.readAll());
                for (const QString &error : result.errors) emit signalErrorOccurred(error);
                for (const QByteArray &frame : result.frames) {
                    waitingForResponse = false;
                    emit signalDataReceived(frame);
                }
            }
            if (waitingForResponse && responseTimer.elapsed() >= m_receiveTimeoutMs.load(std::memory_order_acquire)) {
                waitingForResponse = false;
            }
            if (socket.state() == QAbstractSocket::UnconnectedState) closeSocket();
        }
    }

    closeSocket();
}

END_NAMESPACE_CIQTEK
