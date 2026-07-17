#include "serialclientworker.h"

#include <QElapsedTimer>
#include <algorithm>
#include <chrono>
#include <exception>
#include <new>
#include <utility>

BEGIN_NAMESPACE_CIQTEK

SerialClientWorker::SerialClientWorker(SerialSettings settings, QObject *parent)
    : QObject(parent), m_settings(std::move(settings)), m_ioThread(&SerialClientWorker::ioLoop, this)
{
}

SerialClientWorker::~SerialClientWorker()
{
    m_stopping.store(true, std::memory_order_release);
    m_queueCondition.notify_all();
    if (m_ioThread.joinable()) {
        m_ioThread.join();
    }
}

void SerialClientWorker::setReceiveTimeout(int timeoutMs)
{
    m_receiveTimeoutMs.store(std::max(1, timeoutMs), std::memory_order_release);
}

void SerialClientWorker::setSendInterval(int intervalMs)
{
    m_sendIntervalMs.store(std::max(0, intervalMs), std::memory_order_release);
}

void SerialClientWorker::resetStatistics()
{
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_statistics.reset();
    m_statisticsValid = true;
}

bool SerialClientWorker::finalStatisticsSnapshot(StatisticsSnapshot *snapshot)
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

void SerialClientWorker::connect()
{
    enqueue({TaskType::Connect, {}});
}

void SerialClientWorker::disconnect()
{
    enqueue({TaskType::Disconnect, {}});
}

void SerialClientWorker::sendData(const QByteArray &data)
{
    sendDataWithFormat(data, QString());
}

void SerialClientWorker::sendDataWithFormat(const QByteArray &data, const QString &format)
{
    enqueue({TaskType::Send, data, format});
}

bool SerialClientWorker::isConnected() const
{
    return m_connected.load(std::memory_order_acquire);
}

void SerialClientWorker::enqueue(Task task)
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
            reportError(QStringLiteral("Serial I/O task queue limit reached; send discarded"));
            return;
        }
        m_queueCondition.notify_one();
    } catch (const std::bad_alloc &) {
        reportError(QStringLiteral("Insufficient memory while queuing serial send"));
    }
}

void SerialClientWorker::markStatisticsInvalid()
{
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_statisticsValid = false;
}

void SerialClientWorker::reportError(const QString &message)
{
    markStatisticsInvalid();
    emit signalErrorOccurred(message);
}

void SerialClientWorker::ioLoop()
{
    try {
    QSerialPort serial;
    bool connected = false;
    bool waitingForResponse = false;
    int expectedResponseBytes = 0;
    QByteArray responseBuffer;
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

    const auto closeSerial = [&]() {
        const bool wasConnected = connected;
        if (serial.isOpen()) serial.close();
        connected = false;
        m_connected.store(false, std::memory_order_release);
        expectedResponseBytes = 0;
        responseBuffer.clear();
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
                    serial.setPortName(m_settings.portName);
                    serial.setBaudRate(m_settings.baudRate);
                    serial.setDataBits(static_cast<QSerialPort::DataBits>(m_settings.dataBits));
                    serial.setStopBits(static_cast<QSerialPort::StopBits>(m_settings.stopBits));
                    serial.setParity(static_cast<QSerialPort::Parity>(m_settings.parity));
                    serial.setFlowControl(QSerialPort::NoFlowControl);
                    if (serial.open(QIODevice::ReadWrite)) {
                        connected = true;
                        m_connected.store(true, std::memory_order_release);
                        emit signalConnected();
                    } else if (!m_stopping.load(std::memory_order_acquire)) {
                        reportError(QStringLiteral("Failed to open serial port: %1").arg(serial.errorString()));
                    }
                }
            } else if (task.type == TaskType::Disconnect) {
                closeSerial();
            } else if (task.type == TaskType::Send) {
                if (!connected) {
                    reportError(QStringLiteral("Serial port is not connected; send failed"));
                } else {
                    qint64 totalWritten = 0;
                    bool writeOk = true;
                    while (totalWritten < task.data.size()) {
                        const qint64 written = serial.write(task.data.constData() + totalWritten, task.data.size() - totalWritten);
                        if (written <= 0 || !serial.waitForBytesWritten(1000)) {
                            writeOk = false;
                            break;
                        }
                        totalWritten += written;
                    }
                    if (!writeOk) {
                        const QString errorMessage = QStringLiteral("Serial port write failed: %1").arg(serial.errorString());
                        {
                            std::lock_guard<std::mutex> lock(m_statsMutex);
                            m_statistics.markAllPendingLost();
                        }
                        reportError(errorMessage);
                    } else {
                        {
                            std::lock_guard<std::mutex> lock(m_statsMutex);
                            m_statistics.recordSend(task.data, task.format);
                        }
                        emit signalBytesWritten(totalWritten);
                        emit signalDataSent(task.data, task.format);
                        expectedResponseBytes = task.data.size();
                        responseBuffer.clear();
                        waitingForResponse = true;
                        responseTimer.restart();
                    }
                }
            }
        }

        if (connected) {
            const int pollMs = waitingForResponse
                ? std::max(1, std::min(2, m_receiveTimeoutMs.load(std::memory_order_acquire) - static_cast<int>(responseTimer.elapsed())))
                : 0;
            if (serial.waitForReadyRead(pollMs)) {
                const QByteArray data = serial.read(64 * 1024);
                if (waitingForResponse && !data.isEmpty()) {
                    if (data.size() > kMaxResponseBufferBytes || responseBuffer.size() > kMaxResponseBufferBytes - data.size()) {
                        responseBuffer.clear();
                        waitingForResponse = false;
                        reportError(QStringLiteral("Serial response buffer limit reached; response discarded"));
                    } else {
                        responseBuffer.append(data);
                    }
                }
                if (waitingForResponse && expectedResponseBytes > 0 && responseBuffer.size() >= expectedResponseBytes) {
                    const QByteArray completeResponse = responseBuffer.left(expectedResponseBytes);
                    const auto responseCompletedAt = std::chrono::steady_clock::now();
                    const int extraBytes = responseBuffer.size() - expectedResponseBytes;
                    if (extraBytes > 0) {
                        reportError(QStringLiteral("Serial sticky packet detected; %1 extra bytes discarded").arg(extraBytes));
                    }
                    responseBuffer.clear();
                    {
                        std::lock_guard<std::mutex> lock(m_statsMutex);
                        if (!m_statistics.recordReceive(completeResponse)) m_statisticsValid = false;
                    }
                    waitingForResponse = false;
                    armSendInterval(responseCompletedAt);
                    emit signalDataReceived(completeResponse);
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
            if (!serial.isOpen()) closeSerial();
        }
    }

    closeSerial();
    } catch (const std::bad_alloc &) {
        m_connected.store(false, std::memory_order_release);
        reportError(QStringLiteral("Insufficient memory in serial worker; statistics discarded"));
    } catch (const std::exception &error) {
        m_connected.store(false, std::memory_order_release);
        reportError(QStringLiteral("Serial worker failed: %1; statistics discarded").arg(QString::fromLocal8Bit(error.what())));
    }
}

END_NAMESPACE_CIQTEK
