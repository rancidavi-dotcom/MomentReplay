#pragma once

#include <QSize>
#include <QtGlobal>

namespace MomentEncoding {

inline QSize replayFrameSize(const QSize &source)
{
    constexpr QSize maximum(1920, 1080);
    QSize size(source);
    if (size.width() > maximum.width() || size.height() > maximum.height())
        size = source.scaled(maximum, Qt::KeepAspectRatio);
    // H.264 with the yuv420p pixel format only accepts frames whose width and
    // height are divisible by 2; odd dimensions make libx264 fail the encode.
    return QSize(((size.width() + 1) / 2) * 2, ((size.height() + 1) / 2) * 2);
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
