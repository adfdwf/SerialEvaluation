#ifndef ICOMMUNICATIONINTERFACE_H
#define ICOMMUNICATIONINTERFACE_H

#include "namespace.h"

#include <QByteArray>

BEGIN_NAMESPACE_CIQTEK

class ICommunicationInterface
{
public:
    /**
     * @brief  ~ICommunicationInterface 默认虚析构函数
     * @return void
     */
    virtual ~ICommunicationInterface() = default;

    /**
     * @brief  connect 建立通信连接
     * @return void
     */
    virtual void connect() = 0;

    /**
     * @brief  disconnect 断开通信连接
     * @return void
     */
    virtual void disconnect() = 0;

    /**
     * @brief  sendData 发送通信数据
     * @param  data 待发送字节数据
     * @return void
     */
    virtual void sendData(const QByteArray &data) = 0;

    /**
     * @brief  isConnected 获取当前连接状态
     * @return bool true表示已连接，false表示未连接
     */
    virtual bool isConnected() const = 0;
};

END_NAMESPACE_CIQTEK

#endif // ICOMMUNICATIONINTERFACE_H