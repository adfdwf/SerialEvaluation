#include "statisticsmanager.h"

#include <limits>
#include <algorithm>

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
}

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

bool StatisticsManager::recordReceive(const QByteArray &payload, PacketInfo *updatedPacket)
{
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

QVector<PacketInfo> StatisticsManager::markTimeouts(qint64 timeoutMs)
{
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

bool StatisticsManager::hasPendingPackets() const
{
    for (const PacketInfo &packet : m_packets) {
        if (packet.status == PacketInfo::Status::Pending) {
            return true;
        }
    }
    return false;
}

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

        QVector<qint64> elapsedList;
        elapsedList.reserve(static_cast<int>(snap.successReceived));
        for (const PacketInfo &packet : m_packets) {
            if (packet.status == PacketInfo::Status::Success) {
                elapsedList.push_back(packet.elapsedMs);
            }
        }

        std::sort(elapsedList.begin(), elapsedList.end());

        const int n = elapsedList.size();
        auto percentile = [&](double p) -> double {
            if (n == 0) return 0.0;
            double rank = p * (n - 1);
            int lo = static_cast<int>(rank);
            int hi = qMin(lo + 1, n - 1);
            double frac = rank - lo;
            return static_cast<double>(elapsedList[lo]) * (1.0 - frac) + static_cast<double>(elapsedList[hi]) * frac;
        };

        snap.p50Ms = percentile(0.50);
        snap.p90Ms = percentile(0.90);
        snap.p95Ms = percentile(0.95);
        snap.p99Ms = percentile(0.99);
    }

    return snap;
}

const QVector<PacketInfo> &StatisticsManager::packets() const
{
    return m_packets;
}

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
