#ifndef SERIALCLIENTWORKER_H
#define SERIALCLIENTWORKER_H

#include "icommunicationinterface.h"
#include "namespace.h"

#include <QObject>
#include <QSerialPort>
#include <QString>

BEGIN_NAMESPACE_CIQTEK

/**
 * @brief 一个串口连接所需的基础配置。
 */
struct SerialSettings
{
    QString portName;                         ///< 串口名称，例如 COM3 或 /dev/ttyUSB0。
    qint32 baudRate = 115200;                 ///< 波特率。
    int dataBits = QSerialPort::Data8;        ///< 数据位数量，对应 QSerialPort::DataBits。
    int stopBits = QSerialPort::OneStop;      ///< 停止位数量，对应 QSerialPort::StopBits。
    int parity = QSerialPort::NoParity;       ///< 校验方式，对应 QSerialPort::Parity。
};

/**
 * @brief 在独立线程中执行一个串口的打开、收发和关闭操作。
 *
 * 每个 SerialPortSession 拥有一个 worker 实例，因此多个串口的 I/O 事件
 * 互不阻塞。worker 不负责发送节奏和统计，只负责可靠传输原始字节。
 */
class SerialClientWorker final : public QObject, public ICommunicationInterface
{
    Q_OBJECT

public:
    /**
     * @brief 创建串口 worker。
     * @param settings 串口名称、波特率和帧格式配置。
     * @param parent Qt 父对象。
     */
    explicit SerialClientWorker(SerialSettings settings, QObject *parent = nullptr);

    /** @brief 关闭串口并释放 QSerialPort。 */
    ~SerialClientWorker() override;

public Q_SLOTS:
    /** @brief 按配置打开串口。 */
    void connect() override;

    /** @brief 关闭当前串口连接。 */
    void disconnect() override;

    /**
     * @brief 向已打开的串口写入原始字节数据。
     * @param data 待发送的字节数组。
     */
    void sendData(const QByteArray &data) override;

    /**
     * @brief 查询串口是否已打开。
     * @return true 表示串口处于打开状态。
     */
    bool isConnected() const override;

Q_SIGNALS:
    /** @brief 串口成功打开时发出。 */
    void signalConnected();

    /** @brief 串口关闭或异常断开时发出。 */
    void signalDisconnected();

    /**
     * @brief 收到串口字节时发出。
     * @param data 当前 readyRead 事件读取到的字节块。
     */
    void signalDataReceived(const QByteArray &data);

    /**
     * @brief 发生打开、读取或写入错误时发出。
     * @param message 可展示给用户的错误信息。
     */
    void signalErrorOccurred(const QString &message);

    /**
     * @brief 串口底层完成写入时发出。
     * @param bytes 本次完成写入的字节数。
     */
    void signalBytesWritten(qint64 bytes);

private:
    SerialSettings m_settings;        ///< 当前 worker 使用的串口配置。
    QSerialPort *m_serial = nullptr;  ///< Qt 串口对象，归 worker 所在线程管理。
};

END_NAMESPACE_CIQTEK

#endif // SERIALCLIENTWORKER_H
