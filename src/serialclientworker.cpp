#include "serialclientworker.h"

#include <QElapsedTimer>
#include <algorithm>
#include <chrono>
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
    enqueue({TaskType::Send, data});
}

bool SerialClientWorker::isConnected() const
{
    return m_connected.load(std::memory_order_acquire);
}

void SerialClientWorker::enqueue(Task task)
{
    if (m_stopping.load(std::memory_order_acquire)) return;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_tasks.emplace_back(std::move(task));
    }
    m_queueCondition.notify_one();
}

void SerialClientWorker::ioLoop()
{
    QSerialPort serial;
    bool connected = false;
    bool waitingForResponse = false;
    QElapsedTimer responseTimer;

    const auto closeSerial = [&]() {
        const bool wasConnected = connected;
        if (serial.isOpen()) serial.close();
        connected = false;
        m_connected.store(false, std::memory_order_release);
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
                        emit signalErrorOccurred(QStringLiteral("Failed to open serial port: %1").arg(serial.errorString()));
                    }
                }
            } else if (task.type == TaskType::Disconnect) {
                closeSerial();
            } else if (task.type == TaskType::Send) {
                if (!connected) {
                    emit signalErrorOccurred(QStringLiteral("Serial port is not connected; send failed"));
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
                        emit signalErrorOccurred(QStringLiteral("Serial port write failed: %1").arg(serial.errorString()));
                    } else {
                        emit signalBytesWritten(totalWritten);
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
                if (!data.isEmpty()) {
                    waitingForResponse = false;
                    emit signalDataReceived(data);
                }
            }
            if (waitingForResponse && responseTimer.elapsed() >= m_receiveTimeoutMs.load(std::memory_order_acquire)) {
                waitingForResponse = false;
            }
            if (!serial.isOpen()) closeSerial();
        }
    }

    closeSerial();
}

END_NAMESPACE_CIQTEK
