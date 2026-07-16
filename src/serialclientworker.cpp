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
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_tasks.emplace_back(std::move(task));
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
                    {
                        std::lock_guard<std::mutex> lock(m_statsMutex);
                        m_statistics.recordSend(task.data, task.format);
                    }
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
                        std::lock_guard<std::mutex> lock(m_statsMutex);
                        m_statistics.markAllPendingLost();
                        reportError(QStringLiteral("Serial port write failed: %1").arg(serial.errorString()));
                    } else {
                        emit signalBytesWritten(totalWritten);
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
                ? std::max(1, std::min(10, m_receiveTimeoutMs.load(std::memory_order_acquire) - static_cast<int>(responseTimer.elapsed())))
                : 5;
            if (serial.waitForReadyRead(pollMs)) {
                const QByteArray data = serial.readAll();
                if (waitingForResponse && !data.isEmpty()) {
                    responseBuffer.append(data);
                }
                if (waitingForResponse && expectedResponseBytes > 0 && responseBuffer.size() >= expectedResponseBytes) {
                    const QByteArray completeResponse = responseBuffer.left(expectedResponseBytes);
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
                    emit signalDataReceived(completeResponse);
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
