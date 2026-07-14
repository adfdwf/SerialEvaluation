#include "serialclientworker.h"

#include <utility>

BEGIN_NAMESPACE_CIQTEK

/**
 * @brief  SerialClientWorker 默认构造函数
 * @param  settings 串口配置参数
 * @param  parent Qt父对象
 * @return void
 */
SerialClientWorker::SerialClientWorker(SerialSettings settings, QObject *parent)
    : QObject(parent), m_settings(std::move(settings))
{
}

/**
 * @brief  ~SerialClientWorker 默认析构函数
 * @return void
 */
SerialClientWorker::~SerialClientWorker()
{
    disconnect();
    delete m_serial;
}

/**
 * @brief  connect 打开串口连接
 * @return void
 */
void SerialClientWorker::connect()
{
    if (!m_serial) {
        m_serial = new QSerialPort();
        m_serial->moveToThread(thread());

        QObject::connect(m_serial, &QSerialPort::readyRead, this, [this]() {
            emit signalDataReceived(m_serial->readAll());
        });
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
                             // Resource/device errors indicate an unexpected
                             // hot-unplug or OS-level disconnect. Close the
                             // handle and notify the UI so this session is
                             // marked disconnected without affecting peers.
                             if (wasOpen && (error == QSerialPort::ResourceError ||
                                             error == QSerialPort::DeviceNotFoundError ||
                                             error == QSerialPort::PermissionError)) {
                                 m_serial->close();
                                 emit signalDisconnected();
                             }
                         });
    }

    if (m_serial->isOpen()) {
        return;
    }

    m_serial->setPortName(m_settings.portName);
    m_serial->setBaudRate(m_settings.baudRate);
    m_serial->setDataBits(static_cast<QSerialPort::DataBits>(m_settings.dataBits));
    m_serial->setStopBits(static_cast<QSerialPort::StopBits>(m_settings.stopBits));
    m_serial->setParity(static_cast<QSerialPort::Parity>(m_settings.parity));
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serial->open(QIODevice::ReadWrite)) {
        emit signalErrorOccurred(QStringLiteral("串口打开失败：%1").arg(m_serial->errorString()));
        return;
    }

    emit signalConnected();
}

/**
 * @brief  disconnect 关闭串口连接
 * @return void
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
 * @brief  sendData 发送串口数据
 * @param  data 待发送字节数据
 * @return void
 */
void SerialClientWorker::sendData(const QByteArray &data)
{
    if (!m_serial || !m_serial->isOpen()) {
        emit signalErrorOccurred(QStringLiteral("串口未连接，发送失败"));
        return;
    }

    const qint64 written = m_serial->write(data);
    if (written < 0) {
        emit signalErrorOccurred(QStringLiteral("串口写入失败：%1").arg(m_serial->errorString()));
        return;
    }

    m_serial->flush();
}

/**
 * @brief  isConnected 获取串口连接状态
 * @return bool true表示串口已打开
 */
bool SerialClientWorker::isConnected() const
{
    return m_serial && m_serial->isOpen();
}

END_NAMESPACE_CIQTEK
