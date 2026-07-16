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

/**
 * @brief 一条发送请求及其响应的完整生命周期记录。
 */
struct PacketInfo
{
    /** 当前数据包的处理状态。 */
    enum class Status {
        Pending,    ///< 已发送，等待响应。
        Success,    ///< 已收到响应并完成耗时统计。
        Timeout     ///< 超过超时时间仍未收到响应。
    };

    quint64 id = 0;                ///< 当前测试中的递增数据包编号。
    QDateTime sentAt;              ///< 发送时的系统时间，用于日志记录。
    QDateTime receivedAt;          ///< 收到响应时的系统时间。
    qint64 sentTickMs = 0;         ///< 发送时的单调时钟毫秒值，用于精确计算 RTT。
    qint64 elapsedMs = -1;         ///< 往返耗时；未完成时为 -1。
    Status status = Status::Pending; ///< 当前生命周期状态。
    QByteArray txPayload;          ///< 实际发送的原始字节。
    QString txFormat;              ///< 发送格式，取值通常为 ASCII 或 HEX。
    QByteArray rxPayload;          ///< 收到的原始响应字节。
};

/**
 * @brief 一次测试结束后的统计快照。
 */
struct StatisticsSnapshot
{
    quint64 totalSent = 0;         ///< 已发送数据包总数。
    quint64 successReceived = 0;   ///< 成功收到响应的数据包数。
    quint64 lostPackets = 0;       ///< 超时或断开导致丢失的数据包数。
    double successRate = 0.0;      ///< 成功率，范围为 0 到 100。
    double averageElapsedMs = 0.0; ///< 成功数据包的平均往返耗时。
    qint64 minElapsedMs = 0;        ///< 所有成功响应记录中的最小耗时。
    qint64 maxElapsedMs = 0;        ///< 所有成功响应记录中的最大耗时。
    double p50Ms = 0.0;             ///< 50 分位往返耗时。
    double p90Ms = 0.0;             ///< 90 分位往返耗时。
    double p95Ms = 0.0;             ///< 95 分位往返耗时。
    double p99Ms = 0.0;             ///< 99 分位往返耗时。
    quint64 totalSentBytes = 0;     ///< 已提交到底层 I/O 的发送字节数。
    quint64 totalReceivedBytes = 0; ///< 已从底层 I/O 读取的接收字节数。
    double txBytesPerSecond = 0.0;  ///< 从测试开始计算的发送吞吐量（字节/秒）。
    double rxBytesPerSecond = 0.0;  ///< 从测试开始计算的接收吞吐量（字节/秒）。
};

/**
 * @brief 管理发送记录、响应匹配、超时状态和分位数统计。
 *
 * 调用方应保证同一实例由一个统计线程访问；响应没有内置序列号时，使用发送顺序
 * 与响应顺序进行 FIFO 匹配；RTT 使用单调时钟，避免系统时间调整造成误差。
 */
class StatisticsManager final : public QObject
{
    Q_OBJECT

public:
    /** @brief 创建统计管理器并初始化空测试状态。 */
    explicit StatisticsManager(QObject *parent = nullptr);

    /** @brief 清空所有记录、队列和计数器，开始新的测试周期。 */
    void reset();

    /**
     * @brief 记录一条发送数据并分配递增编号。
     * @param payload 实际发送的原始字节。
     * @param format 用户选择的发送格式。
     * @return 已创建的数据包记录副本。
     */
    PacketInfo recordSend(const QByteArray &payload, const QString &format = QString());

    /**
     * @brief 将接收数据匹配到最早的 Pending 数据包。
     * @param payload 收到的原始响应字节。
     * @param updatedPacket 可选输出参数，返回更新后的数据包记录。
     * @return 成功匹配返回 true，否则返回 false。
     */
    bool recordReceive(const QByteArray &payload, PacketInfo *updatedPacket = nullptr);

    /**
     * @brief 将超过超时阈值的 Pending 数据包标记为 Timeout。
     * @param timeoutMs 超时阈值，单位为毫秒。
     * @return 本次新标记为超时的数据包列表。
     */
    QVector<PacketInfo> markTimeouts(qint64 timeoutMs);

    /**
     * @brief 将所有剩余 Pending 数据包标记为丢失。
     * @return 本次被标记的数据包列表。
     */
    QVector<PacketInfo> markAllPendingLost();

    /**
     * @brief 判断是否仍有等待响应的数据包。
     * @return 存在 Pending 数据包时返回 true。
     */
    bool hasPendingPackets() const;

    /**
     * @brief 返回当前仍在等待响应的数据包数量。
     * @return Pending 数据包数。
     */
    int pendingPacketCount() const;

    /**
     * @brief 根据当前记录计算总数、成功率、平均值和 P50/P90/P95/P99。
     * @return 当前测试统计快照。
     */
    StatisticsSnapshot snapshot() const;

    /**
     * @brief 返回全部数据包记录的只读引用。
     * @return 当前测试的完整数据包列表。
     */
    const QVector<PacketInfo> &packets() const;

private:
    /** @brief 根据数据包状态重新构建待响应编号队列。 */
    void rebuildPendingQueue();

private:
    QElapsedTimer m_timer;              ///< 测试周期使用的单调时钟。
    quint64 m_nextId = 1;               ///< 下一条发送记录要使用的编号。
    QVector<PacketInfo> m_packets;      ///< 当前测试周期的全部数据包记录。
    QQueue<quint64> m_pendingIds;       ///< 按发送顺序排列的 Pending 编号队列。
    QHash<quint64, int> m_indexById;    ///< 数据包编号到 m_packets 下标的快速索引。
};

END_NAMESPACE_CIQTEK

#endif // STATISTICSMANAGER_H
