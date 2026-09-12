#include <QtCore>
#include <QAudioInput>
#include <QAudioDevice>
#include <QAudioSource>

// Exercise actual input-buffer code without opening any recording device.
#define private public
#include "UBMicrophoneInput.h"
#undef private

// Keep the implementation and moc code in this test translation unit because
// MSVC includes access level in member-function symbols.
#include "UBMicrophoneInput.cpp"
#include "moc_UBMicrophoneInput.cpp"

static void check(bool condition, const char* message)
{
    if (!condition)
        qFatal("FAIL: %s", message);
}

static void prepare(UBMicrophoneInput& microphone, QBuffer& device)
{
    microphone.mAudioFormat.setSampleRate(1000);
    microphone.mAudioFormat.setChannelCount(2);
    microphone.mAudioFormat.setSampleFormat(QAudioFormat::Int16);
    microphone.mIODevice = &device;
    microphone.mCaptureActive = true;
    check(device.open(QIODevice::ReadOnly), "fake input opens");
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    UBMicrophoneInput microphone;
    int errors = 0;
    QObject::connect(&microphone, &UBMicrophoneInput::error,
                     [&errors](const QString&) { ++errors; });
    microphone.start();
    microphone.stop();
    check(errors == 1 && !microphone.mCaptureActive,
          "uninitialized start signals an error without crashing");
    check(microphone.audioLevel(QByteArray()) == 0, "empty meter input is safe");

    QByteArray source(403, '\0');
    for (int i = 0; i < source.size(); ++i)
        source[i] = char(i % 127);
    QBuffer device(&source);
    prepare(microphone, device);
    QList<QByteArray> chunks;
    QObject::connect(&microphone, &UBMicrophoneInput::dataAvailable,
                     [&chunks](QByteArray data) { chunks.append(data); });
    microphone.onDataReady();
    check(chunks.size() == 1 && chunks.first().size() == 400,
          "100 ms chunk emitted once");
    check(microphone.mPendingAudio.size() == 3, "partial PCM frame retained");
    microphone.onDataReady();
    check(chunks.size() == 1, "ready notification does not recount consumed bytes");
    source.append(QByteArray(13, '\x55'));
    microphone.onDataReady();
    check(chunks.size() == 1 && microphone.mPendingAudio.size() == 16,
          "sub-100 ms complete tail remains buffered");
    microphone.emitBufferedAudio(true);
    check(chunks.size() == 2 && chunks.last().size() == 16,
          "final flush retains the complete tail");
    check(chunks.first() + chunks.last() == source, "all captured PCM bytes preserved in order");
    microphone.emitBufferedAudio(true);
    check(chunks.size() == 2, "second flush does not duplicate tail");

    microphone.mCaptureActive = false;
    source.append(QByteArray(400, '\x22'));
    const qint64 pausedPosition = device.pos();
    microphone.onDataReady();
    check(device.pos() == pausedPosition && chunks.size() == 2,
          "inactive capture neither reads nor emits paused audio");

    UBMicrophoneInput second;
    QByteArray secondSource(20123, '\0');
    QBuffer secondDevice(&secondSource);
    prepare(second, secondDevice);
    QList<QByteArray> secondChunks;
    QObject::connect(&second, &UBMicrophoneInput::dataAvailable,
                     [&secondChunks](QByteArray data) { secondChunks.append(data); });
    second.onDataReady();
    check(secondChunks.size() == 50 && second.mPendingAudio.size() == 123,
          "large backlog uses bounded 100 ms chunks independently of other captures");
    second.emitBufferedAudio(true);
    check(secondChunks.size() == 51 && secondChunks.last().size() == 120
          && second.mPendingAudio.size() == 3,
          "final flush emits only frame-aligned samples");
    for (const QByteArray& chunk : secondChunks)
        check(chunk.size() <= 400 && chunk.size() % 4 == 0,
              "every emitted chunk is bounded and frame aligned");

    UBMicrophoneInput lifetime;
    QBuffer* temporaryDevice = new QBuffer();
    lifetime.mIODevice = temporaryDevice;
    lifetime.mCaptureActive = true;
    delete temporaryDevice;
    lifetime.onDataReady();
    check(lifetime.mIODevice.isNull(), "destroyed input device cannot leave a dangling pointer");

    qInfo() << "PASS: microphone synthetic buffering, tail flush, pause guard, isolation, lifecycle";
    return 0;
}
