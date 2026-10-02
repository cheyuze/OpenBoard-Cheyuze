// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef UBAUDIOTIMELINE_H
#define UBAUDIOTIMELINE_H

#include <QByteArray>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

// A bounded, timestamp-addressed stereo mixer. Silence advances the timeline
// just like sound; loopback devices need not supply packets while idle.
class UBAudioTimeline
{
public:
    static constexpr int Rate = 48000;
    static constexpr int Channels = 2;
    static constexpr int Capacity = Rate * 2;

    UBAudioTimeline() : mSamples(Capacity * Channels, 0.0f) {}
    qint64 position() const { return mPosition; }

    bool add(qint64 firstFrame, const float *samples, int frames)
    {
        if (frames < 0 || firstFrame + frames > mPosition + Capacity)
            return false;
        for (int i = 0; i < frames; ++i)
        {
            const qint64 frame = firstFrame + i;
            if (frame < mPosition)
                continue; // Stale/pre-start packets cannot move the clock back.
            const int slot = int(frame % Capacity) * Channels;
            for (int ch = 0; ch < Channels; ++ch)
            {
                const float value = samples[i * Channels + ch];
                if (std::isfinite(value))
                    mSamples[slot + ch] += value;
            }
        }
        return true;
    }

    QByteArray takeUntil(qint64 endFrame)
    {
        const int frames = int(std::clamp<qint64>(endFrame - mPosition, 0, Capacity));
        QByteArray result(frames * Channels * int(sizeof(float)), Qt::Uninitialized);
        for (int i = 0; i < frames; ++i, ++mPosition)
        {
            const int slot = int(mPosition % Capacity) * Channels;
            const float peak = std::max(std::abs(mSamples[slot]), std::abs(mSamples[slot + 1]));
            // Linked stereo peak limiter: unity gain below clipping, immediate
            // attack and a 100 ms release. Never normalize/amplify quiet input.
            const float target = peak > 0.98f ? 0.98f / peak : 1.0f;
            mGain = target < mGain ? target : std::min(target, mGain + 1.0f / (Rate / 10));
            for (int ch = 0; ch < Channels; ++ch)
            {
                const float value = mSamples[slot + ch] * mGain;
                std::memcpy(result.data() + (i * Channels + ch) * sizeof(float), &value, sizeof(value));
                mSamples[slot + ch] = 0.0f;
            }
        }
        return result;
    }

private:
    std::vector<float> mSamples;
    qint64 mPosition = 0;
    float mGain = 1.0f;
};
#endif
