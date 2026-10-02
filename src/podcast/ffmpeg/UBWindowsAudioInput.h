// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef UBWINDOWSAUDIOINPUT_H
#define UBWINDOWSAUDIOINPUT_H

#include "UBMicrophoneInput.h"
#include <memory>

// WASAPI loopback plus optional microphone, captured against the same QPC
// clock. All COM/audio work lives on one dedicated thread, never the UI thread.
class UBWindowsAudioInput : public UBMicrophoneInput
{
public:
    explicit UBWindowsAudioInput(const QString &outputDevice);
    ~UBWindowsAudioInput() override;
    bool init() override;
    void start() override;
    void stop() override;
    void setInputDevice(QString name = QString()) override;
    int channelCount() override { return 2; }
    int sampleRate() override { return 48000; }
    int sampleSize() override { return 32; }
    int sampleFormat() override { return 3; } // AV_SAMPLE_FMT_FLT
    static QStringList outputDevices();

private:
    void drain();
    struct Impl;
    std::unique_ptr<Impl> d;
};
#endif
