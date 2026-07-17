#include "statisticsmanager.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

BEGIN_NAMESPACE_CIQTEK

StatisticsManager::StatisticsManager(QObject *parent)
    : QObject(parent)
{
    reset();
}

void StatisticsManager::reset()
{
    m_timer.restart();
    m_nextId = 1;
    m_packets.clear();
    m_pendingIds.clear();
    m_indexById.clear();
    m_totalSent = 0;
    m_successReceived = 0;
    m_lostPackets = 0;
    m_totalSentBytes = 0;
    m_totalReceivedBytes = 0;
    m_elapsedSumMs = 0;
    m_minElapsedMs = 0;
    m_maxElapsedMs = 0;
    m_latencyBuckets.fill(0);
}

PacketInfo StatisticsManager::recordSend(const QByteArray &payload, const QString &format)
{
    PacketInfo packet;
    packet.id = m_nextId++;
    packet.sentAt = QDateTime::currentDateTime();
    packet.sentTickMs = m_timer.elapsed();
    packet.txPayload = payload;
    packet.txFormat = format;

    ++m_totalSent;
    m_totalSentBytes += static_cast<quint64>(payload.size());
    m_indexById.insert(packet.id, m_packets.size());
    m_packets.push_back(packet);
    m_pendingIds.enqueue(packet.id);
    return packet;
}

bool StatisticsManager::recordReceive(const QByteArray &payload, PacketInfo *updatedPacket)
{
    while (!m_pendingIds.isEmpty()) {
        const quint64 id = m_pendingIds.dequeue();
        const int index = m_indexById.value(id, -1);
        if (index < 0 || index >= m_packets.size() || m_packets[index].status != PacketInfo::Status::Pending) continue;

        PacketInfo packet = m_packets.at(index);
        packet.receivedAt = QDateTime::currentDateTime();
        packet.elapsedMs = m_timer.elapsed() - packet.sentTickMs;
        packet.status = PacketInfo::Status::Success;
        packet.rxPayload = payload;
        if (updatedPacket) *updatedPacket = packet;

        ++m_successReceived;
        m_totalReceivedBytes += static_cast<quint64>(payload.size());
        recordSuccessfulElapsed(packet.elapsedMs);
        m_packets.removeAt(index);
        rebuildPendingQueue();
        return true;
    }

    return false;
}

bool StatisticsManager::oldestPendingElapsed(qint64 *elapsedMs) const
{
    if (!elapsedMs) return false;
    for (const quint64 id : m_pendingIds) {
        const int index = m_indexById.value(id, -1);
        if (index < 0 || index >= m_packets.size() || m_packets[index].status != PacketInfo::Status::Pending) continue;
        *elapsedMs = m_timer.elapsed() - m_packets[index].sentTickMs;
        return true;
    }
    return false;
}

bool StatisticsManager::markOldestPendingLost(qint64 elapsedMs, PacketInfo *updatedPacket)
{
    while (!m_pendingIds.isEmpty()) {
        const quint64 id = m_pendingIds.dequeue();
        const int index = m_indexById.value(id, -1);
        if (index < 0 || index >= m_packets.size() || m_packets[index].status != PacketInfo::Status::Pending) continue;

        PacketInfo packet = m_packets.at(index);
        packet.status = PacketInfo::Status::Timeout;
        packet.elapsedMs = elapsedMs;
        if (updatedPacket) *updatedPacket = packet;

        ++m_lostPackets;
        m_packets.removeAt(index);
        rebuildPendingQueue();
        return true;
    }

    return false;
}

QVector<PacketInfo> StatisticsManager::markTimeouts(qint64 timeoutMs)
{
    QVector<PacketInfo> timedOut;
    QVector<PacketInfo> remaining;
    remaining.reserve(m_packets.size());
    const qint64 now = m_timer.elapsed();

    for (const PacketInfo &pending : std::as_const(m_packets)) {
        if (pending.status == PacketInfo::Status::Pending && now - pending.sentTickMs >= timeoutMs) {
            PacketInfo packet = pending;
            packet.status = PacketInfo::Status::Timeout;
            packet.elapsedMs = timeoutMs;
            timedOut.push_back(packet);
            ++m_lostPackets;
        } else {
            remaining.push_back(pending);
        }
    }

    if (!timedOut.isEmpty()) {
        m_packets = std::move(remaining);
        rebuildPendingQueue();
    }
    return timedOut;
}

QVector<PacketInfo> StatisticsManager::markAllPendingLost()
{
    QVector<PacketInfo> lost;
    lost.reserve(m_packets.size());
    for (const PacketInfo &pending : std::as_const(m_packets)) {
        PacketInfo packet = pending;
        packet.status = PacketInfo::Status::Timeout;
        packet.elapsedMs = -1;
        lost.push_back(packet);
        ++m_lostPackets;
    }
    m_packets.clear();
    m_pendingIds.clear();
    m_indexById.clear();
    return lost;
}

bool StatisticsManager::hasPendingPackets() const
{
    return !m_packets.isEmpty();
}

int StatisticsManager::pendingPacketCount() const
{
    return m_packets.size();
}

StatisticsSnapshot StatisticsManager::snapshot() const
{
    StatisticsSnapshot snap = countersSnapshot();
    if (snap.successReceived > 0) {
        snap.averageElapsedMs = static_cast<double>(m_elapsedSumMs) / static_cast<double>(snap.successReceived);
        snap.minElapsedMs = m_minElapsedMs;
        snap.maxElapsedMs = m_maxElapsedMs;

        auto percentile = [&](double ratio) {
            const quint64 rank = qMax<quint64>(1, static_cast<quint64>(std::ceil(ratio * static_cast<double>(snap.successReceived))));
            quint64 cumulative = 0;
            for (int bucket = 0; bucket < static_cast<int>(m_latencyBuckets.size()); ++bucket) {
                cumulative += m_latencyBuckets[static_cast<size_t>(bucket)];
                if (cumulative >= rank) return static_cast<double>(latencyValueForBucket(bucket));
            }
            return static_cast<double>(m_maxElapsedMs);
        };

        snap.p50Ms = percentile(0.50);
        snap.p90Ms = percentile(0.90);
        snap.p95Ms = percentile(0.95);
        snap.p99Ms = percentile(0.99);
    }

    return snap;
}

StatisticsSnapshot StatisticsManager::countersSnapshot() const
{
    StatisticsSnapshot snap;
    snap.totalSent = m_totalSent;
    snap.successReceived = m_successReceived;
    snap.lostPackets = m_lostPackets;
    snap.totalSentBytes = m_totalSentBytes;
    snap.totalReceivedBytes = m_totalReceivedBytes;
    if (snap.totalSent > 0) {
        snap.successRate = static_cast<double>(snap.successReceived) * 100.0 / static_cast<double>(snap.totalSent);
    }
    const qint64 elapsedMs = m_timer.elapsed();
    if (elapsedMs > 0) {
        const double seconds = static_cast<double>(elapsedMs) / 1000.0;
        snap.txBytesPerSecond = static_cast<double>(snap.totalSentBytes) / seconds;
        snap.rxBytesPerSecond = static_cast<double>(snap.totalReceivedBytes) / seconds;
    }
    return snap;
}

const QVector<PacketInfo> &StatisticsManager::packets() const
{
    return m_packets;
}

int StatisticsManager::latencyBucket(qint64 elapsedMs)
{
    if (elapsedMs <= 0) return 0;
    quint64 value = static_cast<quint64>(elapsedMs);
    int exponent = 0;
    while (value >= 2 && exponent < kLatencyExponentCount - 1) {
        value >>= 1;
        ++exponent;
    }

    const quint64 base = quint64(1) << exponent;
    const quint64 remainder = static_cast<quint64>(elapsedMs) - base;
    const int subBucket = qBound(0, static_cast<int>((remainder * kLatencySubBucketCount) / base), kLatencySubBucketCount - 1);
    return 1 + exponent * kLatencySubBucketCount + subBucket;
}

qint64 StatisticsManager::latencyValueForBucket(int bucket)
{
    if (bucket <= 0) return 0;
    const int normalized = bucket - 1;
    const int exponent = normalized / kLatencySubBucketCount;
    const int subBucket = normalized % kLatencySubBucketCount;
    const quint64 base = quint64(1) << exponent;
    return static_cast<qint64>(base + (base * static_cast<quint64>(subBucket)) / kLatencySubBucketCount);
}

void StatisticsManager::recordSuccessfulElapsed(qint64 elapsedMs)
{
    ++m_latencyBuckets[static_cast<size_t>(latencyBucket(elapsedMs))];
    m_elapsedSumMs += elapsedMs;
    if (m_successReceived == 1) {
        m_minElapsedMs = elapsedMs;
        m_maxElapsedMs = elapsedMs;
    } else {
        m_minElapsedMs = qMin(m_minElapsedMs, elapsedMs);
        m_maxElapsedMs = qMax(m_maxElapsedMs, elapsedMs);
    }
}

void StatisticsManager::rebuildPendingQueue()
{
    m_pendingIds.clear();
    m_indexById.clear();
    for (int index = 0; index < m_packets.size(); ++index) {
        const quint64 id = m_packets.at(index).id;
        m_pendingIds.enqueue(id);
        m_indexById.insert(id, index);
    }
}

END_NAMESPACE_CIQTEK
