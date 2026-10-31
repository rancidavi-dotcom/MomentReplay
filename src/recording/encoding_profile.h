#pragma once

#include <QSize>
#include <QtGlobal>

namespace MomentEncoding {

inline QSize replayFrameSize(const QSize &source)
{
    constexpr QSize maximum(1920, 1080);
    if (source.width() <= maximum.width() && source.height() <= maximum.height())
        return source;
    return source.scaled(maximum, Qt::KeepAspectRatio);
}

inline int videoBitRate(int quality, const QSize &size)
{
    constexpr qint64 fullHdPixels = 1920LL * 1080LL;
    constexpr int rates[] = {2000000, 3500000, 5500000, 8000000, 12000000};
    const int boundedQuality = qBound(0, quality, 4);
    const qint64 pixels = qMax<qint64>(1, qint64(size.width()) * size.height());
    const qint64 scaled = qint64(rates[boundedQuality]) * pixels / fullHdPixels;
    return int(qBound<qint64>(450000, scaled, qint64(rates[boundedQuality])));
}

} // namespace MomentEncoding
