#pragma once

#include <QtGlobal>

namespace Ciqtek {

inline bool responseReachedTimeout(qint64 elapsedMs, qint64 timeoutMs) noexcept
{
    const qint64 effectiveTimeoutMs = timeoutMs < 1 ? 1 : timeoutMs;
    return elapsedMs >= effectiveTimeoutMs;
}

} // namespace Ciqtek
