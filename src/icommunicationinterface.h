#ifndef ICOMMUNICATIONINTERFACE_H
#define ICOMMUNICATIONINTERFACE_H

#include "namespace.h"

#include <QByteArray>

BEGIN_NAMESPACE_CIQTEK

/**
 * @brief 通信 worker 的统一抽象接口。
 *
 * TCP 和串口 worker 都通过该接口提供连接、断开、发送和状态查询能力，
 * 主窗口只依赖通信行为，不依赖具体传输介质。
 */
class ICommunicationInterface
{
public:
    /**
     * @brief 销毁通信接口对象。
     *
     * 使用虚析构函数，确保通过接口指针释放派生 worker 时资源能够正确回收。
     */
    virtual ~ICommunicationInterface() = default;

    /**
     * @brief 建立底层通信连接。
     */
    virtual void connect() = 0;

    /**
     * @brief 关闭底层通信连接。
     */
    virtual void disconnect() = 0;

    /**
     * @brief 发送一段原始字节数据。
     * @param data 要发送的字节数组；接口不改变数据内容。
     */
    virtual void sendData(const QByteArray &data) = 0;

    /**
     * @brief 查询当前是否已经建立连接。
     * @return true 表示已连接，false 表示未连接。
     */
    virtual bool isConnected() const = 0;
};

END_NAMESPACE_CIQTEK

#endif // ICOMMUNICATIONINTERFACE_H
