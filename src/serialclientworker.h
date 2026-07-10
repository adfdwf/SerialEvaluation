#ifndef SERIALCLIENTWORKER_H
#define SERIALCLIENTWORKER_H

#include "icommunicationinterface.h"
#include "namespace.h"

#include <QObject>
#include <QSerialPort>
#include <QString>

BEGIN_NAMESPACE_CIQTEK

struct SerialSettings
{
    /** 串口名称 */
    QString portName;

    /** 波特率 */
    qint32 baudRate = 115200;

    /** 数据位 */
    int dataBits = QSerialPort::Data8;

    /** 停止位 */
    int stopBits = QSerialPort::OneStop;

    /** 校验位 */
    int parity = QSerialPort::NoParity;
};

class SerialClientWorker final : public QObject, public ICommunicationInterface
{
    Q_OBJECT
public:
    /**
     * @brief  SerialClientWorker 默认构造函数
     * @param  settings 串口配置参数
     * @param  parent Qt父对象
     * @return void
     */
    explicit SerialClientWorker(SerialSettings settings, QObject *parent = nullptr);

    /**
     * @brief  ~SerialClientWorker 默认析构函数
     * @return void
     */
    ~SerialClientWorker() override;

public Q_SLOTS:
    /**
     * @brief  connect 打开串口连接
     * @return void
     */
    void connect() override;

    /**
     * @brief  disconnect 关闭串口连接
     * @return void
     */
    void disconnect() override;

    /**
     * @brief  sendData 发送串口数据
     * @param  data 待发送字节数据
     * @return void
     */
    void sendData(const QByteArray &data) override;

    /**
     * @brief  isConnected 获取串口连接状态
     * @return bool true表示串口已打开
     */
    bool isConnected() const override;

Q_SIGNALS:
    /**
     * @brief  signalConnected 串口连接成功信号
     * @return void
     */
    void signalConnected();

    /**
     * @brief  signalDisconnected 串口断开连接信号
     * @return void
     */
    void signalDisconnected();

    /**
     * @brief  signalDataReceived 串口数据接收信号
     * @param  data 接收到的字节数据
     * @return void
     */
    void signalDataReceived(const QByteArray &data);

    /**
     * @brief  signalErrorOccurred 串口错误信号
     * @param  message 错误文本
     * @return void
     */
    void signalErrorOccurred(const QString &message);

    /**
     * @brief  signalBytesWritten 串口写入完成信号
     * @param  bytes 已写入字节数
     * @return void
     */
    void signalBytesWritten(qint64 bytes);

private:
    /** 串口配置 */
    SerialSettings m_settings;

    /** Qt串口对象 */
    QSerialPort *m_serial = nullptr;
};

END_NAMESPACE_CIQTEK

#endif // SERIALCLIENTWORKER_H