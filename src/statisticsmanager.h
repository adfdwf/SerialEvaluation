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
#include <array>

BEGIN_NAMESPACE_CIQTEK

/**
 * @brief 一条发送请求及其响应的生命周期记录。
 *
 * StatisticsManager 只保留仍在等待响应的记录；已完成记录会立即释放 payload。
 */
struct PacketInfo
{
    enum class Status {
        Pending,
        Success,
        Timeout
    };

    quint64 id = 0;
    QDateTime sentAt;
    QDateTime receivedAt;
    qint64 sentTickMs = 0;
    qint64 elapsedMs = -1;
    Status status = Status::Pending;
    QByteArray txPayload;
    QString txFormat;
    QByteArray rxPayload;
};

/** @brief 一次测试结束后的统计快照。 */
struct StatisticsSnapshot
{
    quint64 totalSent = 0;
    quint64 successReceived = 0;
    quint64 lostPackets = 0;
    double successRate = 0.0;
    double averageElapsedMs = 0.0;
    qint64 minElapsedMs = 0;
    qint64 maxElapsedMs = 0;
    double p50Ms = 0.0;
    double p90Ms = 0.0;
    double p95Ms = 0.0;
    double p99Ms = 0.0;
    quint64 totalSentBytes = 0;
    quint64 totalReceivedBytes = 0;
    double txBytesPerSecond = 0.0;
    double rxBytesPerSecond = 0.0;
};

/**
 * @brief 管理发送记录、响应匹配和有界统计。
 *
 * 已完成数据包不再保存到历史数组；平均值、最大/最小值和字节计数采用流式累计，
 * P50/P90/P95/P99 使用固定大小的延迟直方图估算，因此内存占用与运行时长无关。
 */
class StatisticsManager final : public QObject
{
    Q_OBJECT

public:
    explicit StatisticsManager(QObject *parent = nullptr);

    void reset();
    PacketInfo recordSend(const QByteArray &payload, const QString &format = QString());
    bool recordReceive(const QByteArray &payload, PacketInfo *updatedPacket = nullptr);
    QVector<PacketInfo> markTimeouts(qint64 timeoutMs);
    QVector<PacketInfo> markAllPendingLost();
    bool hasPendingPackets() const;
    int pendingPacketCount() const;
    /** @brief 返回实时计数，不计算耗时分位数。 */
    StatisticsSnapshot countersSnapshot() const;
    StatisticsSnapshot snapshot() const;

    /** @brief 返回当前 Pending 记录，仅用于响应匹配和诊断，不是历史记录。 */
    const QVector<PacketInfo> &packets() const;

private:
    static constexpr int kLatencySubBucketCount = 64;
    static constexpr int kLatencyExponentCount = 63;
    static constexpr int kLatencyBucketCount = kLatencySubBucketCount * kLatencyExponentCount + 1;

    static int latencyBucket(qint64 elapsedMs);
    static qint64 latencyValueForBucket(int bucket);
    void recordSuccessfulElapsed(qint64 elapsedMs);
    void rebuildPendingQueue();

private:
    QElapsedTimer m_timer;
    quint64 m_nextId = 1;

    // 仅保留尚未完成的请求，通常每个端口最多只有一个。
    QVector<PacketInfo> m_packets;
    QQueue<quint64> m_pendingIds;
    QHash<quint64, int> m_indexById;

    quint64 m_totalSent = 0;
    quint64 m_successReceived = 0;
    quint64 m_lostPackets = 0;
    quint64 m_totalSentBytes = 0;
    quint64 m_totalReceivedBytes = 0;
    qint64 m_elapsedSumMs = 0;
    qint64 m_minElapsedMs = 0;
    qint64 m_maxElapsedMs = 0;
    std::array<quint64, kLatencyBucketCount> m_latencyBuckets{};
};

END_NAMESPACE_CIQTEK

#endif // STATISTICSMANAGER_H
