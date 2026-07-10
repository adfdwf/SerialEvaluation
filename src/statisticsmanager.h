#ifndef STATISTICSMANAGER_H
#define STATISTICSMANAGER_H

#include "namespace.h"

#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QQueue>
#include <QVector>

BEGIN_NAMESPACE_CIQTEK

struct PacketInfo
{
    enum class Status {
        Pending,
        Success,
        Timeout
    };

    /** 数据包序号 */
    quint64 id = 0;

    /** 发送时间 */
    QDateTime sentAt;

    /** 接收时间 */
    QDateTime receivedAt;

    /** 单调时钟发送刻度 */
    qint64 sentTickMs = 0;

    /** 往返耗时，-1表示未统计 */
    qint64 elapsedMs = -1;

    /** 数据包状态 */
    Status status = Status::Pending;

    /** 发送负载 */
    QByteArray txPayload;

    /** 接收负载 */
    QByteArray rxPayload;
};

struct StatisticsSnapshot
{
    /** 发送总数 */
    quint64 totalSent = 0;

    /** 成功回包数 */
    quint64 successReceived = 0;

    /** 丢包数量 */
    quint64 lostPackets = 0;

    /** 成功率百分比 */
    double successRate = 0.0;

    /** 平均耗时 */
    double averageElapsedMs = 0.0;

    /** 最小耗时 */
    qint64 minElapsedMs = 0;

    /** 最大耗时 */
    qint64 maxElapsedMs = 0;
};

class StatisticsManager final : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief  StatisticsManager 默认构造函数
     * @param  parent Qt父对象
     * @return void
     */
    explicit StatisticsManager(QObject *parent = nullptr);

    /**
     * @brief  reset 重置所有统计数据
     * @return void
     */
    void reset();

    /**
     * @brief  recordSend 记录一次发送数据
     * @param  payload 发送负载
     * @return PacketInfo 本次发送的数据包信息
     */
    PacketInfo recordSend(const QByteArray &payload);

    /**
     * @brief  recordReceive 记录一次接收数据并匹配最早未完成包
     * @param  payload 接收负载
     * @param  updatedPacket 输出更新后的数据包信息
     * @return bool true表示匹配成功，false表示无待匹配数据包
     */
    bool recordReceive(const QByteArray &payload, PacketInfo *updatedPacket = nullptr);

    /**
     * @brief  markTimeouts 标记超过超时时间的数据包
     * @param  timeoutMs 超时时间，单位毫秒
     * @return QVector<PacketInfo> 已超时数据包列表
     */
    QVector<PacketInfo> markTimeouts(qint64 timeoutMs);

    /**
     * @brief  markAllPendingLost 将所有待回包数据标记为丢包
     * @return QVector<PacketInfo> 被标记的丢包列表
     */
    QVector<PacketInfo> markAllPendingLost();

    /**
     * @brief  hasPendingPackets 判断是否仍有待回包数据
     * @return bool true表示存在待回包数据
     */
    bool hasPendingPackets() const;

    /**
     * @brief  snapshot 获取统计快照
     * @return StatisticsSnapshot 当前统计快照
     */
    StatisticsSnapshot snapshot() const;

    /**
     * @brief  packets 获取完整数据包列表
     * @return const QVector<PacketInfo>& 数据包只读引用
     */
    const QVector<PacketInfo> &packets() const;

private:
    /**
     * @brief  rebuildPendingQueue 重建待回包队列
     * @return void
     */
    void rebuildPendingQueue();

private:
    /** 统计用单调计时器 */
    QElapsedTimer m_timer;

    /** 下一个数据包序号 */
    quint64 m_nextId = 1;

    /** 全量数据包记录 */
    QVector<PacketInfo> m_packets;

    /** 等待回包的数据包序号队列 */
    QQueue<quint64> m_pendingIds;

    /** 数据包序号到数组下标的索引 */
    QHash<quint64, int> m_indexById;
};

END_NAMESPACE_CIQTEK

#endif // STATISTICSMANAGER_H