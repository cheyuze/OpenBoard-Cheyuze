#include "podcast/UBRecordingClock.h"
#include <QElapsedTimer>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>

static void expect(qint64 actual, qint64 expected, const char *scenario)
{
    if (actual != expected)
    {
        std::cerr << "FAIL " << scenario << ": expected " << expected
                  << ", got " << actual << '\n';
        std::exit(EXIT_FAILURE);
    }
}

int main()
{
    static_assert(std::is_same<decltype(UBRecordingClock().elapsed(0)), qint64>::value,
                  "Recording timestamps must stay 64-bit across the API");
    if (!QElapsedTimer::isMonotonic())
    {
        std::cerr << "A monotonic elapsed timer is required\n";
        return EXIT_FAILURE;
    }

    UBRecordingClock clock;
    expect(clock.elapsed(9000), 0, "idle clock");
    clock.start(1000);
    expect(clock.elapsed(716800), 715800, "original overflow boundary");
    expect(clock.elapsed(781000), 780000, "13 minute recording");

    clock.pause(781000);
    expect(clock.elapsed(1381000), 780000, "paused time does not advance");
    clock.pause(1381000);
    expect(clock.elapsed(1381000), 780000, "duplicate pause is harmless");
    clock.stop(1381000);
    expect(clock.elapsed(1981000), 780000, "stop while paused excludes entire pause");

    clock.start(0);
    clock.pause(10000);
    clock.resume(70000);
    clock.resume(80000);
    expect(clock.elapsed(80000), 20000, "resume excludes pause; duplicate resume is harmless");
    clock.pause(90000);
    clock.resume(150000);
    clock.stop(160000);
    expect(clock.elapsed(999999), 40000, "multiple pauses and stop freeze exact active duration");

    // Elapsed time has no relationship to the current date/time. This passes
    // midnight, daylight-saving changes and many 32-bit millisecond windows.
    constexpr qint64 day = Q_INT64_C(24) * 60 * 60 * 1000;
    clock.start(0);
    expect(clock.elapsed(day + 2000), day + 2000, "crossing midnight");
    expect(clock.elapsed(40 * day), 40 * day, "40 day duration exceeds signed 32-bit ms");
    clock.pause(40 * day);
    expect(clock.elapsed(80 * day), 40 * day, "long paused interval stays 64-bit");
    clock.resume(80 * day);
    clock.stop(81 * day);
    expect(clock.elapsed(90 * day), 41 * day, "long pause/resume stays 64-bit");

    clock.start(12345);
    expect(clock.elapsed(12345), 0, "new recording resets previous elapsed time");
    expect(clock.elapsed(12300), 0, "invalid backward sample never yields negative time");

    // Exercise thousands of transitions against a simple known active-time
    // total, with no sleeps, device dependencies or actual 12-minute wait.
    qint64 now = 0;
    qint64 expected = 0;
    clock.start(now);
    for (int i = 0; i < 10000; ++i)
    {
        const qint64 active = 123 + i;
        now += active;
        expected += active;
        clock.pause(now);
        now += 987 + i;
        expect(clock.elapsed(now), expected, "repeated pause freeze");
        clock.resume(now);
        expect(clock.elapsed(now), expected, "repeated resume boundary");
    }
    clock.stop(now);
    expect(clock.elapsed(now + day), expected, "stress-test stopped time");
    std::cout << "PASS: 64-bit monotonic clock; 13-minute/40-day recordings, "
                 "pause-stop, repeated pause/resume and 10,000 transitions\n";
    return EXIT_SUCCESS;
}
