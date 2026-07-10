#include "statisticsmanager.h"

#include <limits>

BEGIN_NAMESPACE_CIQTEK

/**
 * @brief  StatisticsManager 默认构造函数
 * @param  parent Qt父对象
 * @return void
 */
StatisticsManager::StatisticsManager(QObject *parent)
    : QObject(parent)
{
    reset();
}

/**
 * @brief  reset 重置所有统计数据
 * @return void
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
 * @brief  recordSend 记录一次发送数据
 * @param  payload 发送负载
 * @return PacketInfo 本次发送的数据包信息
 */
PacketInfo StatisticsManager::recordSend(const QByteArray &payload)
{
    PacketInfo packet;
    packet.id = m_nextId++;
    packet.sentAt = QDateTime::currentDateTime();
    packet.sentTickMs = m_timer.elapsed();
    packet.txPayload = payload;

    m_indexById.insert(packet.id, m_packets.size());
    m_packets.push_back(packet);
    m_pendingIds.enqueue(packet.id);
    return packet;
}

/**
 * @brief  recordReceive 记录一次接收数据并匹配最早未完成包
 * @param  payload 接收负载
 * @param  updatedPacket 输出更新后的数据包信息
 * @return bool true表示匹配成功，false表示无待匹配数据包
 */
bool StatisticsManager::recordReceive(const QByteArray &payload, PacketInfo *updatedPacket)
{
    // 串口/TCP 回包通常不带业务 ID，按发送 FIFO 队列匹配最早未完成的包。
    while (!m_pendingIds.isEmpty()) {
        const quint64 id = m_pendingIds.dequeue();
        const int index = m_indexById.value(id, -1);
        if (index < 0 || m_packets[index].status != PacketInfo::Status::Pending) {
            continue;
        }

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
 * @brief  markTimeouts 标记超过超时时间的数据包
 * @param  timeoutMs 超时时间，单位毫秒
 * @return QVector<PacketInfo> 已超时数据包列表
 */
QVector<PacketInfo> StatisticsManager::markTimeouts(qint64 timeoutMs)
{
    // 使用单调时钟判断超时，避免系统时间调整影响性能统计。
    QVector<PacketInfo> timedOut;
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
 * @brief  markAllPendingLost 将所有待回包数据标记为丢包
 * @return QVector<PacketInfo> 被标记的丢包列表
 */
QVector<PacketInfo> StatisticsManager::markAllPendingLost()
{
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
 * @brief  hasPendingPackets 判断是否仍有待回包数据
 * @return bool true表示存在待回包数据
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
 * @brief  snapshot 获取统计快照
 * @return StatisticsSnapshot 当前统计快照
 */
StatisticsSnapshot StatisticsManager::snapshot() const
{
    StatisticsSnapshot snap;
    snap.totalSent = static_cast<quint64>(m_packets.size());

    qint64 sum = 0;
    qint64 minValue = std::numeric_limits<qint64>::max();
    qint64 maxValue = 0;

    for (const PacketInfo &packet : m_packets) {
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
    }

    return snap;
}

/**
 * @brief  packets 获取完整数据包列表
 * @return const QVector<PacketInfo>& 数据包只读引用
 */
const QVector<PacketInfo> &StatisticsManager::packets() const
{
    return m_packets;
}

/**
 * @brief  rebuildPendingQueue 重建待回包队列
 * @return void
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