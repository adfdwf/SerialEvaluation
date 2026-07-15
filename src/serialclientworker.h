#ifndef SERIALCLIENTWORKER_H
#define SERIALCLIENTWORKER_H

#include "icommunicationinterface.h"
#include "namespace.h"

#include <QObject>
#include <QSerialPort>
#include <QString>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

BEGIN_NAMESPACE_CIQTEK

struct SerialSettings
{
    QString portName;
    qint32 baudRate = 115200;
    int dataBits = QSerialPort::Data8;
    int stopBits = QSerialPort::OneStop;
    int parity = QSerialPort::NoParity;
};

/**
 * @brief Queue-driven serial I/O worker.
 *
 * The QObject stays in the GUI thread. A private I/O thread owns QSerialPort;
 * callers only enqueue tasks, so no QObject is moved between threads.
 */
class SerialClientWorker final : public QObject, public ICommunicationInterface
{
    Q_OBJECT

public:
    explicit SerialClientWorker(SerialSettings settings, QObject *parent = nullptr);
    ~SerialClientWorker() override;

    /** @brief Set the receive wait budget used by the I/O polling loop. */
    void setReceiveTimeout(int timeoutMs);

public Q_SLOTS:
    void connect() override;
    void disconnect() override;
    void sendData(const QByteArray &data) override;
    bool isConnected() const override;

Q_SIGNALS:
    void signalConnected();
    void signalDisconnected();
    void signalDataReceived(const QByteArray &data);
    void signalErrorOccurred(const QString &message);
    void signalBytesWritten(qint64 bytes);

private:
    enum class TaskType { Connect, Disconnect, Send };
    struct Task { TaskType type; QByteArray data; };

    void enqueue(Task task);
    void ioLoop();

    SerialSettings m_settings;
    std::atomic_bool m_stopping{false};
    std::atomic_bool m_connected{false};
    std::atomic_int m_receiveTimeoutMs{300};
    mutable std::mutex m_queueMutex;
    std::condition_variable m_queueCondition;
    std::deque<Task> m_tasks;
    std::thread m_ioThread;
};

END_NAMESPACE_CIQTEK

#endif // SERIALCLIENTWORKER_H
