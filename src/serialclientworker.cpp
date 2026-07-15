#include "serialclientworker.h"

#include <utility>

BEGIN_NAMESPACE_CIQTEK

/**
 * @brief 保存串口配置并创建 worker。
 * @param settings 串口配置。
 * @param parent Qt 父对象。
 */
SerialClientWorker::SerialClientWorker(SerialSettings settings, QObject *parent)
    : QObject(parent), m_settings(std::move(settings))
{
}

/**
 * @brief 关闭串口并释放串口对象。
 */
SerialClientWorker::~SerialClientWorker()
{
    disconnect();
    delete m_serial;
}

/**
 * @brief 创建 QSerialPort，连接事件并按配置打开设备。
 */
void SerialClientWorker::connect()
{
    if (!m_serial) {
        // QSerialPort 必须在 worker 线程中创建，才能在该线程接收 readyRead 事件。
        m_serial = new QSerialPort();
        m_serial->moveToThread(thread());

        // readyRead 可能一次返回多个字节块，当前 worker 将可读数据全部交给上层。
        QObject::connect(m_serial, &QSerialPort::readyRead, this, [this]() {
            emit signalDataReceived(m_serial->readAll());
        });
        // 转发底层写入完成事件。
        QObject::connect(m_serial, &QSerialPort::bytesWritten, this, &SerialClientWorker::signalBytesWritten);
        QObject::connect(m_serial,
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
                         &QSerialPort::errorOccurred,
#else
                         QOverload<QSerialPort::SerialPortError>::of(&QSerialPort::error),
#endif
                         this,
                         [this](QSerialPort::SerialPortError error) {
                             if (error == QSerialPort::NoError) {
                                 return;
                             }

                             const QString message = m_serial ? m_serial->errorString() : QStringLiteral("Serial port error");
                             const bool wasOpen = m_serial && m_serial->isOpen();
                             emit signalErrorOccurred(message);
                             // 资源错误通常表示热插拔或系统级断开，主动关闭并通知 UI。
                             if (wasOpen && (error == QSerialPort::ResourceError ||
                                             error == QSerialPort::DeviceNotFoundError ||
                                             error == QSerialPort::PermissionError)) {
                                 m_serial->close();
                                 emit signalDisconnected();
                             }
                         });
    }

    // 已打开时不重复配置或打开串口。
    if (m_serial->isOpen()) {
        return;
    }

    // 将 UI 中的配置转换为 QSerialPort 的运行参数。
    m_serial->setPortName(m_settings.portName);
    m_serial->setBaudRate(m_settings.baudRate);
    m_serial->setDataBits(static_cast<QSerialPort::DataBits>(m_settings.dataBits));
    m_serial->setStopBits(static_cast<QSerialPort::StopBits>(m_settings.stopBits));
    m_serial->setParity(static_cast<QSerialPort::Parity>(m_settings.parity));
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serial->open(QIODevice::ReadWrite)) {
        emit signalErrorOccurred(QStringLiteral("Failed to open serial port: %1").arg(m_serial->errorString()));
        return;
    }

    emit signalConnected();
}

/**
 * @brief 关闭串口并通知主窗口。
 */
void SerialClientWorker::disconnect()
{
    if (!m_serial || !m_serial->isOpen()) {
        return;
    }

    m_serial->close();
    emit signalDisconnected();
}

/**
 * @brief 向已打开的串口写入数据。
 * @param data 待发送的字节数组。
 */
void SerialClientWorker::sendData(const QByteArray &data)
{
    if (!m_serial || !m_serial->isOpen()) {
        emit signalErrorOccurred(QStringLiteral("Serial port is not connected; send failed"));
        return;
    }

    const qint64 written = m_serial->write(data);
    if (written < 0) {
        emit signalErrorOccurred(QStringLiteral("Serial port write failed: %1").arg(m_serial->errorString()));
        return;
    }

    m_serial->flush();
}

/**
 * @brief 查询串口当前是否已打开。
 * @return true 表示 QSerialPort 已打开。
 */
bool SerialClientWorker::isConnected() const
{
    return m_serial && m_serial->isOpen();
}

END_NAMESPACE_CIQTEK
