#include "tcpclientworker.h"

#include "protocolframedecoder.h"

#include <QElapsedTimer>
#include <QTcpSocket>
#include <algorithm>
#include <chrono>
#include <exception>
#include <new>
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

void TcpClientWorker::resetStatistics()
{
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_statistics.reset();
    m_statisticsValid = true;
}

bool TcpClientWorker::finalStatisticsSnapshot(StatisticsSnapshot *snapshot)
{
    try {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        if (!m_statistics.markAllPendingLost().isEmpty()) m_statisticsValid = false;
        if (!m_statisticsValid || !snapshot) return false;
        *snapshot = m_statistics.snapshot();
        return true;
    } catch (const std::bad_alloc &) {
        markStatisticsInvalid();
        return false;
    }
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
    sendDataWithFormat(data, QString());
}

void TcpClientWorker::sendDataWithFormat(const QByteArray &data, const QString &format)
{
    enqueue({TaskType::Send, data, format});
}

bool TcpClientWorker::isConnected() const
{
    return m_connected.load(std::memory_order_acquire);
}

void TcpClientWorker::enqueue(Task task)
{
    if (m_stopping.load(std::memory_order_acquire)) return;
    try {
        bool queueFull = false;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_tasks.size() >= kMaxQueuedTasks) queueFull = true;
            else m_tasks.emplace_back(std::move(task));
        }
        if (queueFull) {
            reportError(QStringLiteral("TCP I/O task queue limit reached; send discarded"));
            return;
        }
        m_queueCondition.notify_one();
    } catch (const std::bad_alloc &) {
        reportError(QStringLiteral("Insufficient memory while queuing TCP send"));
    }
}

void TcpClientWorker::markStatisticsInvalid()
{
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_statisticsValid = false;
}

void TcpClientWorker::reportError(const QString &message)
{
    markStatisticsInvalid();
    emit signalErrorOccurred(message);
}

void TcpClientWorker::ioLoop()
{
    try {
    QTcpSocket socket;
    ProtocolFrameDecoder decoder;
    bool connected = false;
    bool waitingForResponse = false;
    bool expectProtocolFrame = false;
    int expectedResponseBytes = 0;
    QByteArray rawResponseBuffer;
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
        rawResponseBuffer.clear();
        expectedResponseBytes = 0;
        expectProtocolFrame = false;
        waitingForResponse = false;
        if (wasConnected) emit signalDisconnected();
    };

    while (!m_stopping.load(std::memory_order_acquire)) {
        Task task{TaskType::Disconnect, {}, {}};
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
                        reportError(QStringLiteral("TCP connect failed: %1").arg(socket.errorString()));
                    }
                }
            } else if (task.type == TaskType::Disconnect) {
                closeSocket();
            } else if (task.type == TaskType::Send) {
                if (!connected) {
                    reportError(QStringLiteral("TCP is not connected; send failed"));
                } else {
                    {
                        std::lock_guard<std::mutex> lock(m_statsMutex);
                        m_statistics.recordSend(task.data, task.format);
                    }
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
                        std::lock_guard<std::mutex> lock(m_statsMutex);
                        m_statistics.markAllPendingLost();
                        reportError(QStringLiteral("TCP write failed: %1").arg(socket.errorString()));
                    } else {
                        socket.flush();
                        emit signalBytesWritten(totalWritten);
                        expectProtocolFrame = task.format.compare(QStringLiteral("HEX"), Qt::CaseInsensitive) == 0 &&
                                              task.data.size() >= 2 &&
                                              static_cast<quint8>(task.data.at(0)) == 0xA0 &&
                                              static_cast<quint8>(task.data.at(1)) == 0x81;
                        expectedResponseBytes = task.data.size();
                        rawResponseBuffer.clear();
                        decoder.clear();
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
                const QByteArray receivedChunk = socket.read(64 * 1024);
                if (waitingForResponse && !expectProtocolFrame) {
                    if (receivedChunk.size() > kMaxResponseBufferBytes || rawResponseBuffer.size() > kMaxResponseBufferBytes - receivedChunk.size()) {
                        rawResponseBuffer.clear();
                        waitingForResponse = false;
                        reportError(QStringLiteral("TCP response buffer limit reached; response discarded"));
                    } else {
                        rawResponseBuffer.append(receivedChunk);
                    }
                    if (rawResponseBuffer.size() >= expectedResponseBytes) {
                        const QByteArray completeResponse = rawResponseBuffer.left(expectedResponseBytes);
                        const int extraBytes = rawResponseBuffer.size() - expectedResponseBytes;
                        if (extraBytes > 0) {
                            reportError(QStringLiteral("TCP sticky packet detected; %1 extra bytes discarded").arg(extraBytes));
                        }
                        rawResponseBuffer.clear();
                        {
                            std::lock_guard<std::mutex> lock(m_statsMutex);
                            if (!m_statistics.recordReceive(completeResponse)) m_statisticsValid = false;
                        }
                        waitingForResponse = false;
                        emit signalDataReceived(completeResponse);
                    }
                } else {
                    const ProtocolFrameDecoder::DecodeResult result = decoder.appendData(receivedChunk);
                    for (const QString &error : result.errors) reportError(error);
                    if (result.frames.size() > 1) {
                        reportError(QStringLiteral("TCP sticky packet detected; split into %1 frames").arg(result.frames.size()));
                    }
                    for (const QByteArray &frame : result.frames) {
                        {
                            std::lock_guard<std::mutex> lock(m_statsMutex);
                            if (!m_statistics.recordReceive(frame)) m_statisticsValid = false;
                        }
                        waitingForResponse = false;
                        emit signalDataReceived(frame);
                    }
                }
            }
            if (waitingForResponse && responseTimer.elapsed() >= m_receiveTimeoutMs.load(std::memory_order_acquire)) {
                waitingForResponse = false;
                {
                    std::lock_guard<std::mutex> lock(m_statsMutex);
                    m_statistics.markTimeouts(m_receiveTimeoutMs.load(std::memory_order_acquire));
                    m_statisticsValid = false;
                }
                emit signalReceiveTimeout();
            }
            if (socket.state() == QAbstractSocket::UnconnectedState) closeSocket();
        }
    }

    closeSocket();
    } catch (const std::bad_alloc &) {
        m_connected.store(false, std::memory_order_release);
        reportError(QStringLiteral("Insufficient memory in TCP worker; statistics discarded"));
    } catch (const std::exception &error) {
        m_connected.store(false, std::memory_order_release);
        reportError(QStringLiteral("TCP worker failed: %1; statistics discarded").arg(QString::fromLocal8Bit(error.what())));
    }
}

END_NAMESPACE_CIQTEK
