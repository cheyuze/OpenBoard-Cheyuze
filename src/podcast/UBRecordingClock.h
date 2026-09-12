#ifndef UBRECORDINGCLOCK_H
#define UBRECORDINGCLOCK_H

#include <QtGlobal>

// All inputs are milliseconds from a monotonic source (QElapsedTimer in the
// controller). Explicit input makes long recordings and pause boundaries
// testable without sleeping or changing the computer's wall clock.
class UBRecordingClock
{
public:
    void start(qint64 now)
    {
        mAccumulated = 0;
        mStartedAt = now;
        mRunning = true;
    }

    qint64 elapsed(qint64 now) const
    {
        return mAccumulated + (mRunning ? qMax<qint64>(0, now - mStartedAt) : 0);
    }

    void pause(qint64 now)
    {
        if (mRunning)
        {
            mAccumulated = elapsed(now);
            mRunning = false;
        }
    }

    void resume(qint64 now)
    {
        if (!mRunning)
        {
            mStartedAt = now;
            mRunning = true;
        }
    }

    void stop(qint64 now) { pause(now); }

private:
    qint64 mAccumulated = 0;
    qint64 mStartedAt = 0;
    bool mRunning = false;
};

#endif
