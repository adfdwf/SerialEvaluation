#ifndef FIXEDLENGTHFRAMEDECODER_H
#define FIXEDLENGTHFRAMEDECODER_H

#include "namespace.h"

#include <QByteArray>
#include <QVector>

BEGIN_NAMESPACE_CIQTEK

/**
 * @brief Accumulates stream bytes and extracts complete fixed-length frames.
 *
 * TCP exposes a byte stream, so one socket read can contain a partial frame,
 * one frame, or multiple frames. This decoder preserves incomplete trailing
 * bytes until more data arrives.
 */
class FixedLengthFrameDecoder final
{
public:
    explicit FixedLengthFrameDecoder(int frameLength);

    QVector<QByteArray> appendData(const QByteArray &data);
    void clear();

    int frameLength() const;
    int bufferedByteCount() const;

private:
    int m_frameLength;
    QByteArray m_buffer;
};

END_NAMESPACE_CIQTEK

#endif // FIXEDLENGTHFRAMEDECODER_H
