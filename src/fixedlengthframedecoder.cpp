#include "fixedlengthframedecoder.h"

#include <QtGlobal>

BEGIN_NAMESPACE_CIQTEK

FixedLengthFrameDecoder::FixedLengthFrameDecoder(int frameLength)
    : m_frameLength(qMax(1, frameLength))
{
}

QVector<QByteArray> FixedLengthFrameDecoder::appendData(const QByteArray &data)
{
    m_buffer.append(data);

    QVector<QByteArray> frames;
    frames.reserve(m_buffer.size() / m_frameLength);
    while (m_buffer.size() >= m_frameLength) {
        frames.push_back(m_buffer.left(m_frameLength));
        m_buffer.remove(0, m_frameLength);
    }
    return frames;
}

void FixedLengthFrameDecoder::clear()
{
    m_buffer.clear();
}

int FixedLengthFrameDecoder::frameLength() const
{
    return m_frameLength;
}

int FixedLengthFrameDecoder::bufferedByteCount() const
{
    return m_buffer.size();
}

END_NAMESPACE_CIQTEK
