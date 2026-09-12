#ifndef UBMICROPHONEINPUT_H
#define UBMICROPHONEINPUT_H
#include <QtCore>
// Replace only the physical capture backend. The production encoder, audio
// conversion/FIFO, worker thread, codecs, muxer and completion signals are real.
class UBMicrophoneInput : public QObject
{
    Q_OBJECT
public:
    UBMicrophoneInput() = default;
    bool init() { return true; }
    void start() { mRunning = true; }
    void stop() { mRunning = false; }
    void setInputDevice(QString = QString()) {}
    int channelCount() { return 2; }
    int sampleRate() { return qEnvironmentVariableIntValue("TEST_AUDIO_SAMPLE_RATE") == 48000 ? 48000 : 44100; }
    int sampleSize() { return 16; }
    int sampleFormat() { return 1; } // AV_SAMPLE_FMT_S16
signals:
    void audioLevelChanged(quint8);
    void dataAvailable(QByteArray);
    void error(QString);
private:
    bool mRunning = false;
};
#endif
