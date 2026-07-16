#include "statisticsmanager.h"

#include <algorithm>
#include <cmath>
#include <limits>

BEGIN_NAMESPACE_CIQTEK

StatisticsManager::StatisticsManager(QObject *parent)
    : QObject(parent)
{
    // 构造完成后立即进入可用的空测试状态。
    reset();
}

/**
 * @brief 清理本轮测试的时钟、编号、记录和待响应队列。
 */
void StatisticsManager::reset()
{
    m_timer.restart();
    m_nextId = 1;
    m_packets.clear();
    m_pendingIds.clear();
    m_indexById.clear();
}

/**
 * @brief 保存一条发送记录并加入 FIFO 待响应队列。
 */
PacketInfo StatisticsManager::recordSend(const QByteArray &payload, const QString &format)
{
    // packet 是本次发送的完整元数据副本。
    PacketInfo packet;
    packet.id = m_nextId++;
    packet.sentAt = QDateTime::currentDateTime();
    packet.sentTickMs = m_timer.elapsed();
    packet.txPayload = payload;
    packet.txFormat = format;

    m_indexById.insert(packet.id, m_packets.size());
    m_packets.push_back(packet);
    m_pendingIds.enqueue(packet.id);
    return packet;
}

/**
 * @brief 按发送顺序将响应匹配到最早的 Pending 数据包。
 */
bool StatisticsManager::recordReceive(const QByteArray &payload, PacketInfo *updatedPacket)
{
    while (!m_pendingIds.isEmpty()) {
        // 先取出最早编号；已超时的编号会在下面被跳过。
        const quint64 id = m_pendingIds.dequeue();
        // 通过索引表快速定位数据包记录。
        const int index = m_indexById.value(id, -1);
        if (index < 0 || m_packets[index].status != PacketInfo::Status::Pending) {
            continue;
        }

        // 更新发送记录的接收时间、耗时、状态和响应数据。
        PacketInfo &packet = m_packets[index];
        packet.receivedAt = QDateTime::currentDateTime();
        packet.elapsedMs = m_timer.elapsed() - packet.sentTickMs;
        packet.status = PacketInfo::Status::Success;
        packet.rxPayload = payload;
        if (updatedPacket) {
            *updatedPacket = packet;
        }
        return true;
    }

    return false;
}

/**
 * @brief 扫描所有 Pending 记录并标记超过阈值的数据包。
 */
QVector<PacketInfo> StatisticsManager::markTimeouts(qint64 timeoutMs)
{
    // timedOut 保存本次状态发生变化的记录，供调用方写日志。
    QVector<PacketInfo> timedOut;
    // now 是相对于测试开始时钟的当前毫秒数。
    const qint64 now = m_timer.elapsed();

    for (PacketInfo &packet : m_packets) {
        if (packet.status == PacketInfo::Status::Pending && now - packet.sentTickMs >= timeoutMs) {
            packet.status = PacketInfo::Status::Timeout;
            packet.elapsedMs = timeoutMs;
            timedOut.push_back(packet);
        }
    }

    if (!timedOut.isEmpty()) {
        rebuildPendingQueue();
    }
    return timedOut;
}

/**
 * @brief 将停止测试时仍未响应的数据包全部标记为丢失。
 */
QVector<PacketInfo> StatisticsManager::markAllPendingLost()
{
    // lost 保存被强制结束的数据包，供 UI 输出丢失日志。
    QVector<PacketInfo> lost;
    for (PacketInfo &packet : m_packets) {
        if (packet.status == PacketInfo::Status::Pending) {
            packet.status = PacketInfo::Status::Timeout;
            packet.elapsedMs = -1;
            lost.push_back(packet);
        }
    }
    m_pendingIds.clear();
    return lost;
}

/**
 * @brief 判断是否还有等待响应的记录。
 */
bool StatisticsManager::hasPendingPackets() const
{
    for (const PacketInfo &packet : m_packets) {
        if (packet.status == PacketInfo::Status::Pending) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 统计当前尚未收到响应的数据包数量。
 */
int StatisticsManager::pendingPacketCount() const
{
    int pending = 0;
    for (const PacketInfo &packet : m_packets) {
        if (packet.status == PacketInfo::Status::Pending) {
            ++pending;
        }
    }
    return pending;
}

/**
 * @brief 汇总当前记录并计算线性插值分位数。
 */
StatisticsSnapshot StatisticsManager::snapshot() const
{
    // snap 是返回给 UI 的统计结果。
    StatisticsSnapshot snap;
    snap.totalSent = static_cast<quint64>(m_packets.size());

    qint64 sum = 0;                                       // 成功 RTT 的总和。
    qint64 minValue = std::numeric_limits<qint64>::max(); // 成功响应耗时中的最小值。
    qint64 maxValue = 0;                                  // 成功响应耗时中的最大值。

    for (const PacketInfo &packet : m_packets) {
        snap.totalSentBytes += static_cast<quint64>(packet.txPayload.size());
        snap.totalReceivedBytes += static_cast<quint64>(packet.rxPayload.size());
        if (packet.status == PacketInfo::Status::Success) {
            ++snap.successReceived;
            sum += packet.elapsedMs;
            minValue = qMin(minValue, packet.elapsedMs);
            maxValue = qMax(maxValue, packet.elapsedMs);
        } else if (packet.status == PacketInfo::Status::Timeout) {
            ++snap.lostPackets;
        }
    }

    if (snap.totalSent > 0) {
        snap.successRate = (static_cast<double>(snap.successReceived) * 100.0) / static_cast<double>(snap.totalSent);
    }
    if (snap.successReceived > 0) {
        snap.averageElapsedMs = static_cast<double>(sum) / static_cast<double>(snap.successReceived);
        snap.minElapsedMs = minValue;
        snap.maxElapsedMs = maxValue;

        // elapsedList 用于排序并计算分位数，不改变原始数据包顺序。
        QVector<qint64> elapsedList;
        elapsedList.reserve(static_cast<int>(snap.successReceived));
        for (const PacketInfo &packet : m_packets) {
            if (packet.status == PacketInfo::Status::Success) {
                elapsedList.push_back(packet.elapsedMs);
            }
        }

        std::sort(elapsedList.begin(), elapsedList.end());

        // n 是成功响应样本数量。
        const int n = elapsedList.size();
        // percentile 使用最近秩法：按 1-based 的 ceil(P*N) 位置取整数耗时，
        // 不做线性插值，因此 P50/P90/P95/P99 不会产生小数。
        auto percentile = [&](double p) -> double {
            if (n == 0) return 0.0;
            const int oneBasedRank = qBound(1, static_cast<int>(std::ceil(p * n)), n);
            return static_cast<double>(elapsedList.at(oneBasedRank - 1));
        };

        snap.p50Ms = percentile(0.50);
        snap.p90Ms = percentile(0.90);
        snap.p95Ms = percentile(0.95);
        snap.p99Ms = percentile(0.99);
    }

    const qint64 elapsedMs = m_timer.elapsed();
    if (elapsedMs > 0) {
        const double seconds = static_cast<double>(elapsedMs) / 1000.0;
        snap.txBytesPerSecond = static_cast<double>(snap.totalSentBytes) / seconds;
        snap.rxBytesPerSecond = static_cast<double>(snap.totalReceivedBytes) / seconds;
    }

    return snap;
}

/**
 * @brief 返回当前测试的完整数据包记录。
 */
const QVector<PacketInfo> &StatisticsManager::packets() const
{
    return m_packets;
}

/**
 * @brief 从所有 Pending 记录重建 FIFO 队列。
 */
void StatisticsManager::rebuildPendingQueue()
{
    m_pendingIds.clear();
    for (const PacketInfo &packet : m_packets) {
        if (packet.status == PacketInfo::Status::Pending) {
            m_pendingIds.enqueue(packet.id);
        }
    }
}

END_NAMESPACE_CIQTEK
