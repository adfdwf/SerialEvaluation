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

    quint64 id = 0;
    QDateTime sentAt;
    QDateTime receivedAt;
    qint64 sentTickMs = 0;
    qint64 elapsedMs = -1;
    Status status = Status::Pending;
    QByteArray txPayload;
    QByteArray rxPayload;
};

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
};

class StatisticsManager final : public QObject
{
    Q_OBJECT
public:
    explicit StatisticsManager(QObject *parent = nullptr);

    void reset();
    PacketInfo recordSend(const QByteArray &payload);
    bool recordReceive(const QByteArray &payload, PacketInfo *updatedPacket = nullptr);
    QVector<PacketInfo> markTimeouts(qint64 timeoutMs);
    QVector<PacketInfo> markAllPendingLost();
    bool hasPendingPackets() const;
    StatisticsSnapshot snapshot() const;
    const QVector<PacketInfo> &packets() const;

private:
    void rebuildPendingQueue();

private:
    QElapsedTimer m_timer;
    quint64 m_nextId = 1;
    QVector<PacketInfo> m_packets;
    QQueue<quint64> m_pendingIds;
    QHash<quint64, int> m_indexById;
};

END_NAMESPACE_CIQTEK

#endif // STATISTICSMANAGER_H
