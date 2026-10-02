#include "podcast/ffmpeg/UBAudioTimeline.h"
#include "podcast/ffmpeg/UBWindowsAudioInput.h"
#include "podcast/ffmpeg/UBFFmpegVideoEncoder.h"
#include <QCoreApplication>
#include <QAudioSink>
#include <QMediaDevices>
#include <QElapsedTimer>
#include <QFile>
#include <QThread>
#include <atomic>
#include <stdexcept>

static void require(bool condition, const char *message)
{ if (!condition) throw std::runtime_error(message); }
static float sample(const QByteArray &data, int frame, int channel)
{
    float value;
    std::memcpy(&value, data.constData() + (frame * 2 + channel) * 4, 4);
    return value;
}
static void waitMs(int milliseconds)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < milliseconds) { QCoreApplication::processEvents(); QThread::msleep(1); }
}
static double amplitude(const QByteArray &data, int first, int count, int channel, double frequency)
{
    double real = 0, imaginary = 0;
    for (int i = 0; i < count; ++i)
    {
        const double phase = i * 6.283185307179586 * frequency / 48000;
        const double value = sample(data, first + i, channel);
        real += value * std::cos(phase);
        imaginary += value * std::sin(phase);
    }
    return 2 * std::hypot(real, imaginary) / count;
}

class Tone : public QIODevice
{
public:
    std::atomic<bool> enabled {false};
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return 48000 * 8 + QIODevice::bytesAvailable(); }
    qint64 readData(char *data, qint64 bytes) override
    {
        const int frames = int(bytes / 8);
        const float gain = enabled ? 0.25f : 0.0f;
        for (int i = 0; i < frames; ++i, ++mPosition)
            for (int ch = 0; ch < 2; ++ch)
            {
                const float value = gain * std::sin(mPosition * 6.283185307179586 * (ch ? 880 : 440) / 48000);
                std::memcpy(data + i * 8 + ch * 4, &value, 4);
            }
        return frames * 8;
    }
    qint64 writeData(const char *, qint64) override { return -1; }
private: qint64 mPosition = 0;
};

static void timelineTests()
{
    UBAudioTimeline timeline;
    std::vector<float> tone(4800 * 2, 0.2f);
    require(timeline.add(48000, tone.data(), 4800), "scheduled packet");
    auto silence = timeline.takeUntil(48000);
    require(silence == QByteArray(48000 * 8, 0), "idle must produce silence, not collapse time");
    auto sound = timeline.takeUntil(52800);
    require(std::abs(sample(sound, 100, 0) - 0.2f) < 1e-6f, "single source unity gain");
    require(timeline.add(0, tone.data(), 4800), "late packets discarded");
    require(!timeline.add(480000, tone.data(), 4800), "future packet bounded");
    require(timeline.takeUntil(52000).isEmpty(), "clock never goes backwards");
    for (int round = 0; round < 10000; ++round)
    {
        const qint64 position = timeline.position();
        require(timeline.add(position, tone.data(), 4800), "ring add");
        require(timeline.add(position, tone.data(), 4800), "second source add");
        auto mixed = timeline.takeUntil(position + 4800);
        require(std::abs(sample(mixed, 100, 1) - 0.4f) < 1e-6f, "mix/ring stale data");
    }
    std::fill(tone.begin(), tone.end(), 0.9f);
    const qint64 position = timeline.position();
    timeline.add(position, tone.data(), 4800);
    timeline.add(position, tone.data(), 4800);
    auto limited = timeline.takeUntil(position + 4800);
    for (int i = 0; i < 4800; ++i)
        require(std::abs(sample(limited, i, 0)) <= 0.98001f, "mixed clipping protection");
    qInfo() << "PASS timeline: silence, stereo mix, unity gain, clipping, bounded memory, 16-minute ring wrap";
}

static void idlePauseTest()
{
    // No playback client: an idle loopback endpoint may return no packets at
    // all. Repeated start/reset must nevertheless preserve active duration.
    UBWindowsAudioInput input(QStringLiteral("Default"));
    input.setInputDevice(QStringLiteral("None"));
    qint64 bytes = 0, activeMs = 0;
    QString error;
    QObject::connect(&input, &UBMicrophoneInput::dataAvailable, [&](QByteArray data) { bytes += data.size(); });
    QObject::connect(&input, &UBMicrophoneInput::error, [&](QString reason) { error = reason; });
    require(input.init(), "idle init failed");
    for (int i = 0; i < 12; ++i)
    {
        input.start();
        QElapsedTimer active; active.start();
        if (i == 4) QThread::msleep(450); // UI stall: drain synchronously at stop.
        else waitMs(90);
        activeMs += active.elapsed();
        input.stop();
        const auto before = bytes;
        waitMs(25);
        input.stop();
        require(bytes == before, "paused stop appended audio twice");
    }
    require(error.isEmpty(), qPrintable(error));
    const double recordedMs = bytes * 1000.0 / (48000 * 8);
    require(std::abs(recordedMs - activeMs) < 75, "idle/rapid pauses changed audio duration");
    qInfo() << "PASS idle loopback, 12 pause/resume cycles, UI stall, stop while paused:"
            << recordedMs << "audio ms /" << activeMs << "active ms";
}

static void loopbackTest(bool encode)
{
    QAudioFormat format;
    format.setSampleRate(48000); format.setChannelCount(2); format.setSampleFormat(QAudioFormat::Float);
    const auto output = QMediaDevices::defaultAudioOutput();
    require(!output.isNull(), "no hardware playback endpoint available");
    Tone tone;
    tone.open(QIODevice::ReadOnly);
    QAudioSink sink(output, format);
    sink.setBufferSize(4800 * 8);
    sink.start(&tone);
    require(sink.error() == QAudio::NoError, "test playback failed");
    UBWindowsAudioInput input(QStringLiteral("Default"));
    input.setInputDevice(QStringLiteral("None"));
    QByteArray recorded;
    QString error;
    QObject::connect(&input, &UBMicrophoneInput::dataAvailable, [&](QByteArray data) { recorded += data; });
    QObject::connect(&input, &UBMicrophoneInput::error, [&](QString reason) { error = reason; });
    require(input.init(), "loopback init failed");
    input.start();
    waitMs(650);
    tone.enabled = true;
    waitMs(1400);
    input.stop(); // pause, flushing the tail synchronously
    const int beforePause = recorded.size();
    waitMs(850);
    require(recorded.size() == beforePause, "audio grew while paused");
    input.start();
    waitMs(1050);
    input.stop();
    input.stop(); // stop while paused must not extend the audio track
    sink.stop();
    require(error.isEmpty(), qPrintable(error));
    const double seconds = recorded.size() / (48000.0 * 8);
    require(seconds > 3.0 && seconds < 3.3, "pause duration leaked or recording tail lost");
    const double left = amplitude(recorded, 48000, 12000, 0, 440);
    const double right = amplitude(recorded, 48000, 12000, 1, 880);
    const double wrong = amplitude(recorded, 48000, 12000, 0, 880);
    require(left > 0.02 && right > 0.02, "missing system tone");
    require(wrong < left * 0.1, "stereo channels corrupted");
    double initial = 0;
    for (int i = 0; i < 12000; ++i) initial += std::abs(sample(recorded, i, 0));
    require(initial / 12000 < 0.01, "leading silence compressed");
    QFile file(QStringLiteral("loopback-test.f32le"));
    require(file.open(QIODevice::WriteOnly) && file.write(recorded) == recorded.size(), "save test PCM");
    qInfo() << "PASS hardware loopback:" << seconds << "seconds, tone amplitudes" << left << right
            << "crosstalk" << wrong << "device" << output.description();

    if (!encode) return;
    UBFFmpegVideoEncoder encoder;
    encoder.setRecordAudio(true);
    encoder.setAudioRecordingDevice(QStringLiteral("None"));
    encoder.setSystemAudioDevice(QStringLiteral("Default"));
    encoder.setVideoFileName(QStringLiteral("system-audio-recording.mp4"));
    encoder.setVideoSize(QSize(640, 360));
    encoder.setFramesPerSecond(30);
    encoder.setVideoBitsPerSecond(4000000);
    bool finished = false, ok = false;
    QObject::connect(&encoder, &UBAbstractVideoEncoder::encodingFinished,
                     [&](bool success) { finished = true; ok = success; });
    sink.start(&tone);
    tone.enabled = true;
    require(encoder.start(), qPrintable(encoder.lastErrorMessage()));
    qint64 base = 0;
    auto segment = [&](int duration) {
        QElapsedTimer time;
        time.start();
        int frame = 0;
        while (time.elapsed() < duration)
        {
            if (time.elapsed() >= frame * 1000 / 30)
            {
                QImage image(640, 360, QImage::Format_RGB32);
                image.fill(QColor::fromHsv((frame * 7) % 360, 180, 220));
                encoder.newPixmap(image, base + time.elapsed());
                ++frame;
            }
            waitMs(1);
        }
        base += time.elapsed();
    };
    segment(2050);
    require(encoder.pause(), "MP4 pause");
    waitMs(850);
    require(encoder.unpause(), "MP4 resume");
    segment(1050);
    require(encoder.pause(), "MP4 final pause");
    waitMs(350);
    encoder.stop();
    sink.stop();
    QElapsedTimer deadline;
    deadline.start();
    while (!finished && deadline.elapsed() < 10000) waitMs(1);
    require(finished && ok, qPrintable(encoder.lastErrorMessage()));
    qInfo() << "PASS actual WASAPI -> AAC -> MP4 integration, active video ms:" << base;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        timelineTests();
        qInfo() << "Playback devices:" << UBWindowsAudioInput::outputDevices();
        UBWindowsAudioInput missing(QStringLiteral("OpenBoard-test-missing-device-9d6c"));
        missing.setInputDevice(QStringLiteral("None"));
        require(!missing.init(), "unavailable explicit device must not silently fall back");
        qInfo() << "PASS unavailable device handled explicitly";
        if (app.arguments().contains("--loopback"))
        {
            idlePauseTest();
            loopbackTest(app.arguments().contains("--mp4"));
        }
    }
    catch (const std::exception &error) { qCritical() << "FAIL" << error.what(); return 1; }
    return 0;
}
