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
 * @brief PacketInfo stores the metadata and payload for one sent/received packet.
 *
 * Each packet has a unique sequential ID, timestamps for send and receive,
 * a monotonic clock tick for precise RTT measurement, and a status field
 * that tracks whether it is Pending, Success, or Timeout.
 */
struct PacketInfo
{
    /** Packet lifecycle status */
    enum class Status {
        Pending,    ///< Sent, waiting for response
        Success,    ///< Response received within timeout
        Timeout     ///< No response received before timeout
    };

    /** Auto-incremented unique sequence ID */
    quint64 id = 0;

    /** Wall-clock timestamp when the packet was sent */
    QDateTime sentAt;

    /** Wall-clock timestamp when the response was received */
    QDateTime receivedAt;

    /** Monotonic clock tick (ms) when the packet was sent, used for RTT */
    qint64 sentTickMs = 0;

    /** Round-trip time in milliseconds, -1 = not measured yet */
    qint64 elapsedMs = -1;

    /** Current lifecycle status */
    Status status = Status::Pending;

    /** Transmitted payload bytes */
    QByteArray txPayload;

    /** Received response payload bytes */
    QByteArray rxPayload;
};

/**
 * @brief StatisticsSnapshot is a read-only summary of the current test run.
 *
 * It contains aggregate counters (total sent, success, lost), performance
 * metrics (average, min, max, P50/P90/P95/P99 latency), and success rate.
 */
struct StatisticsSnapshot
{
    /** Total number of packets sent */
    quint64 totalSent = 0;

    /** Number of successful (response received) packets */
    quint64 successReceived = 0;

    /** Number of lost (timed out) packets */
    quint64 lostPackets = 0;

    /** Success rate as a percentage (0.0 ~ 100.0) */
    double successRate = 0.0;

    /** Average round-trip time in milliseconds */
    double averageElapsedMs = 0.0;

    /** Minimum round-trip time in milliseconds */
    qint64 minElapsedMs = 0;

    /** Maximum round-trip time in milliseconds */
    qint64 maxElapsedMs = 0;

    /** 50th percentile (median) latency in milliseconds */
    double p50Ms = 0.0;

    /** 90th percentile latency in milliseconds */
    double p90Ms = 0.0;

    /** 95th percentile latency in milliseconds */
    double p95Ms = 0.0;

    /** 99th percentile latency in milliseconds */
    double p99Ms = 0.0;
};

/**
 * @brief StatisticsManager tracks all sent/received packets and computes
 *        real-time statistics for the communication benchmark.
 *
 * It uses a monotonic clock (QElapsedTimer) for precise RTT measurement
 * regardless of system wall-clock adjustments. Received packets are matched
 * to pending sends in FIFO order (serial port / raw TCP typically have no
 * embedded sequence ID).
 *
 * Thread-safety note: all methods are designed to be called from a single
 * thread (the main / UI thread). Cross-thread access is not guarded.
 */
class StatisticsManager final : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Constructs a StatisticsManager and resets all state.
     * @param parent Qt parent object (optional)
     */
    explicit StatisticsManager(QObject *parent = nullptr);

    /**
     * @brief Resets all counters, clears all packet records and pending queue.
     *
     * Call this at the start of each new test run.
     */
    void reset();

    /**
     * @brief Records a newly sent packet and assigns it a unique sequence ID.
     * @param payload The byte array that was sent
     * @return PacketInfo containing the assigned ID and send timestamp
     */
    PacketInfo recordSend(const QByteArray &payload);

    /**
     * @brief Records a received response and matches it to the earliest
     *        pending packet (FIFO matching).
     * @param payload The received byte array
     * @param updatedPacket Optional output parameter receiving a copy of
     *        the matched PacketInfo after update
     * @return true if a pending packet was matched, false otherwise
     */
    bool recordReceive(const QByteArray &payload, PacketInfo *updatedPacket = nullptr);

    /**
     * @brief Marks all pending packets whose elapsed time exceeds timeoutMs
     *        as Timeout.
     * @param timeoutMs Timeout threshold in milliseconds
     * @return Vector of PacketInfo entries that were newly marked as Timeout
     */
    QVector<PacketInfo> markTimeouts(qint64 timeoutMs);

    /**
     * @brief Marks every remaining pending packet as Timeout (lost).
     *
     * Typically called when the test is stopped manually.
     * @return Vector of all packets that were marked as Timeout
     */
    QVector<PacketInfo> markAllPendingLost();

    /**
     * @brief Checks whether any packets are still in Pending status.
     * @return true if at least one packet is still waiting for a response
     */
    bool hasPendingPackets() const;

    /**
     * @brief Computes and returns a StatisticsSnapshot with all aggregate
     *        counters, percentiles, and success rate.
     *
     * This is a const method; it does not modify internal state.
     * P50/P90/P95/P99 are calculated by sorting the successful RTT values
     * and applying linear interpolation.
     * @return StatisticsSnapshot with current test-run statistics
     */
    StatisticsSnapshot snapshot() const;

    /**
     * @brief Returns a const reference to the full list of all packet records.
     * @return const QVector<PacketInfo>& packet list (read-only)
     */
    const QVector<PacketInfo> &packets() const;

private:
    /**
     * @brief Rebuilds the pending-ID queue from scratch by scanning m_packets.
     *
     * Called internally after timeouts are marked.
     */
    void rebuildPendingQueue();

private:
    QElapsedTimer m_timer;                   ///< Monotonic clock for RTT measurement
    quint64 m_nextId = 1;                    ///< Next auto-increment packet ID
    QVector<PacketInfo> m_packets;            ///< All packet records for the current run
    QQueue<quint64> m_pendingIds;             ///< FIFO queue of packet IDs awaiting response
    QHash<quint64, int> m_indexById;          ///< Packet-ID-to-index lookup for O(1) access
};

END_NAMESPACE_CIQTEK

#endif // STATISTICSMANAGER_H
