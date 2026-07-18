#include "tcpclientworker.h"

#include "protocolframedecoder.h"
#include "responsehandling.h"

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

void TcpClientWorker::setSendInterval(int intervalMs)
{
    m_sendIntervalMs.store(std::max(0, intervalMs), std::memory_order_release);
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
        if (!snapshot) return false;
        m_statistics.markAllPendingLost();
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
    const bool isSendTask = task.type == TaskType::Send;
    try {
        bool queueFull = false;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_tasks.size() >= kMaxQueuedTasks) queueFull = true;
            else m_tasks.emplace_back(std::move(task));
        }
        if (queueFull) {
            const QString message = QStringLiteral("TCP I/O task queue limit reached; send discarded");
            reportError(message);
            if (isSendTask) emit signalSendFailed(message);
            return;
        }
        m_queueCondition.notify_one();
    } catch (const std::bad_alloc &) {
        const QString message = QStringLiteral("Insufficient memory while queuing TCP send");
        reportError(message);
        if (isSendTask) emit signalSendFailed(message);
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
    auto intervalAnchor = std::chrono::steady_clock::time_point::min();

    const auto armSendInterval = [&](const std::chrono::steady_clock::time_point completedAt) {
        // Interval 的起点必须是 I/O 线程确认响应完整到达的时刻，不能使用
        // GUI 收到 signalDataReceived 的时刻，否则 GUI 忙时会改变间隔基准。
        intervalAnchor = completedAt;
    };

    const auto waitForSendInterval = [&]() {
        while (!m_stopping.load(std::memory_order_acquire)) {
            const auto now = std::chrono::steady_clock::now();
            if (intervalAnchor == std::chrono::steady_clock::time_point::min()) return;
            const int intervalMs = std::max(0, m_sendIntervalMs.load(std::memory_order_acquire));
            const auto sendNotBefore = intervalAnchor + std::chrono::milliseconds(intervalMs);
            if (now >= sendNotBefore) {
                intervalAnchor = std::chrono::steady_clock::time_point::min();
                return;
            }
            const auto remaining = sendNotBefore - now;
            if (remaining > std::chrono::milliseconds(2)) {
                const auto sleepDuration = std::min<std::chrono::steady_clock::duration>(
                    remaining - std::chrono::milliseconds(1), std::chrono::milliseconds(2));
                std::this_thread::sleep_for(sleepDuration);
            } else {
                std::this_thread::yield();
            }
        }
    };

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
            if (task.type == TaskType::Send && connected) {
                waitForSendInterval();
                if (m_stopping.load(std::memory_order_acquire)) break;
            }
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
                        const QString errorMessage = QStringLiteral("TCP write failed: %1").arg(socket.errorString());
                        {
                            std::lock_guard<std::mutex> lock(m_statsMutex);
                            m_statistics.markAllPendingLost();
                        }
                        reportError(errorMessage);
                        emit signalSendFailed(errorMessage);
                    } else {
                        socket.flush();
                        {
                            std::lock_guard<std::mutex> lock(m_statsMutex);
                            m_statistics.recordSend(task.data, task.format);
                        }
                        emit signalBytesWritten(totalWritten);
                        emit signalDataSent(task.data, task.format);
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
                ? std::max(1, std::min(2, m_receiveTimeoutMs.load(std::memory_order_acquire) - static_cast<int>(responseTimer.elapsed())))
                : 0;
            if (socket.waitForReadyRead(pollMs)) {
                const QByteArray receivedChunk = socket.read(64 * 1024);
                if (waitingForResponse && !expectProtocolFrame) {
                    if (receivedChunk.size() > kMaxResponseBufferBytes || rawResponseBuffer.size() > kMaxResponseBufferBytes - receivedChunk.size()) {
                        rawResponseBuffer.clear();
                        waitingForResponse = false;
                        const QString message = QStringLiteral("TCP response buffer limit reached; response discarded");
                        reportError(message);
                        emit signalResponseAborted(message);
                    } else {
                        rawResponseBuffer.append(receivedChunk);
                    }
                    const ResponseBatch batch = collectLengthResponse(rawResponseBuffer, expectedResponseBytes);
                    if (!batch.events.isEmpty()) {
                        const ResponseEvent response = batch.events.constFirst();
                        const auto responseCompletedAt = std::chrono::steady_clock::now();
                        rawResponseBuffer.clear();
                        {
                            std::lock_guard<std::mutex> lock(m_statsMutex);
                            if (response.valid) {
                                if (!m_statistics.recordReceive(response.data)) m_statisticsValid = false;
                            } else {
                                m_statistics.markOldestPendingLost(responseTimer.elapsed());
                            }
                        }
                        waitingForResponse = false;
                        armSendInterval(responseCompletedAt);
                        emit signalDataReceived(response.data, response.valid);
                    }
                } else {
                    const ResponseBatch batch = collectProtocolResponse(decoder.appendData(receivedChunk));
                    for (const QString &error : batch.errors) reportError(error);
                    if (!batch.events.isEmpty()) {
                        const ResponseEvent response = batch.events.constFirst();
                        const auto responseCompletedAt = std::chrono::steady_clock::now();
                        {
                            std::lock_guard<std::mutex> lock(m_statsMutex);
                            if (response.valid) {
                                if (!m_statistics.recordReceive(response.data)) m_statisticsValid = false;
                            } else {
                                m_statistics.markOldestPendingLost(responseTimer.elapsed());
                            }
                        }
                        waitingForResponse = false;
                        armSendInterval(responseCompletedAt);
                        emit signalDataReceived(response.data, response.valid);
                    }
                }
            }
            if (waitingForResponse && responseTimer.elapsed() >= m_receiveTimeoutMs.load(std::memory_order_acquire)) {
                const auto responseCompletedAt = std::chrono::steady_clock::now();
                waitingForResponse = false;
                {
                    std::lock_guard<std::mutex> lock(m_statsMutex);
                    m_statistics.markTimeouts(m_receiveTimeoutMs.load(std::memory_order_acquire));
                    m_statisticsValid = false;
                }
                armSendInterval(responseCompletedAt);
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
