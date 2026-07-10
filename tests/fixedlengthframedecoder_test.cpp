#include "fixedlengthframedecoder.h"

#include <QCoreApplication>
#include <QDebug>

using namespace Ciqtek;

namespace {

bool expectFrames(const QVector<QByteArray> &actual,
                  const QVector<QByteArray> &expected,
                  const char *scenario)
{
    if (actual == expected) {
        return true;
    }

    qCritical() << scenario << "failed; expected" << expected << "but got" << actual;
    return false;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    bool passed = true;

    FixedLengthFrameDecoder decoder(4);

    passed &= expectFrames(decoder.appendData("AB"), {}, "partial frame");
    passed &= decoder.bufferedByteCount() == 2;
    passed &= expectFrames(decoder.appendData("CD"), {QByteArray("ABCD")}, "completed partial frame");
    passed &= decoder.bufferedByteCount() == 0;

    passed &= expectFrames(decoder.appendData("EFGHIJKL"),
                           {QByteArray("EFGH"), QByteArray("IJKL")},
                           "two sticky frames");

    passed &= expectFrames(decoder.appendData("12"), {}, "mixed first chunk");
    passed &= expectFrames(decoder.appendData("3456789A"),
                           {QByteArray("1234"), QByteArray("5678")},
                           "mixed partial and sticky frames");
    passed &= decoder.bufferedByteCount() == 2;

    decoder.clear();
    passed &= decoder.bufferedByteCount() == 0;

    if (!passed) {
        qCritical() << "FixedLengthFrameDecoder tests failed";
        return 1;
    }

    qInfo() << "FixedLengthFrameDecoder tests passed";
    return 0;
}
