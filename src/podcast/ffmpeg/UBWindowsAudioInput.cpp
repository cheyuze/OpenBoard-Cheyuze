// SPDX-License-Identifier: GPL-3.0-or-later
#include "UBWindowsAudioInput.h"
#include "UBAudioTimeline.h"
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <avrt.h>
#include <wrl/client.h>
#include <thread>
#include <limits>
extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
}

using Microsoft::WRL::ComPtr;
namespace {
qint64 clock100ns()
{
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    return qint64((long double)now.QuadPart * 10000000 / frequency.QuadPart);
}
qint64 audioFrame(qint64 time100ns) { return time100ns * UBAudioTimeline::Rate / 10000000; }

QString deviceName(IMMDevice *device)
{
    ComPtr<IPropertyStore> properties;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &properties)))
        return {};
    PROPVARIANT value;
    PropVariantInit(&value);
    const HRESULT hr = properties->GetValue(PKEY_Device_FriendlyName, &value);
    const QString name = SUCCEEDED(hr) && value.vt == VT_LPWSTR
            ? QString::fromWCharArray(value.pwszVal) : QString();
    PropVariantClear(&value);
    return name;
}

ComPtr<IMMDevice> findDevice(IMMDeviceEnumerator *enumerator, EDataFlow flow, const QString &name)
{
    ComPtr<IMMDevice> result;
    if (name.isEmpty() || name == QStringLiteral("Default"))
        enumerator->GetDefaultAudioEndpoint(flow, eConsole, &result);
    else
    {
        ComPtr<IMMDeviceCollection> devices;
        UINT count = 0;
        if (SUCCEEDED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &devices)))
            devices->GetCount(&count);
        for (UINT i = 0; i < count; ++i)
        {
            ComPtr<IMMDevice> candidate;
            if (SUCCEEDED(devices->Item(i, &candidate)) && deviceName(candidate.Get()) == name)
                return candidate;
        }
    }
    return result; // Never silently substitute another explicitly chosen device.
}

struct CaptureSource
{
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    WAVEFORMATEX *format = nullptr;
    SwrContext *resampler = nullptr;
    QString label;
    bool packed24 = false;
    bool running = false;
    qint64 nextFrame = -1;
    ~CaptureSource()
    {
        if (running) client->Stop();
        swr_free(&resampler);
        CoTaskMemFree(format);
    }

    QString failure(HRESULT hr) const
    {
        return QStringLiteral("%1采集失败（0x%2）。请检查设备连接和 Windows 声音设置后重新录制。")
                .arg(label).arg(quint32(hr), 8, 16, QLatin1Char('0'));
    }

    QString open(IMMDeviceEnumerator *enumerator, bool loopback, const QString &name)
    {
        label = loopback ? QStringLiteral("电脑声音") : QStringLiteral("麦克风");
        auto device = findDevice(enumerator, loopback ? eRender : eCapture, name);
        if (!device)
            return label + QStringLiteral("设备不可用，请在录制设置中重新选择：") + name;
        HRESULT hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client);
        if (FAILED(hr)) return failure(hr);
        hr = client->GetMixFormat(&format);
        if (FAILED(hr)) return failure(hr);
        if (!format->nSamplesPerSec || !format->nChannels || format->nChannels > 32)
            return label + QStringLiteral("的音频格式无效。");

        WORD tag = format->wFormatTag;
        uint64_t mask = 0;
        if (tag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= 22)
        {
            auto ext = reinterpret_cast<WAVEFORMATEXTENSIBLE *>(format);
            if (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) tag = WAVE_FORMAT_IEEE_FLOAT;
            else if (ext->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) tag = WAVE_FORMAT_PCM;
            mask = ext->dwChannelMask;
        }
        AVSampleFormat inputFormat = AV_SAMPLE_FMT_NONE;
        if (tag == WAVE_FORMAT_IEEE_FLOAT)
            inputFormat = format->wBitsPerSample == 32 ? AV_SAMPLE_FMT_FLT
                    : (format->wBitsPerSample == 64 ? AV_SAMPLE_FMT_DBL : AV_SAMPLE_FMT_NONE);
        else if (tag == WAVE_FORMAT_PCM)
        {
            packed24 = format->wBitsPerSample == 24;
            if (format->wBitsPerSample == 8) inputFormat = AV_SAMPLE_FMT_U8;
            if (format->wBitsPerSample == 16) inputFormat = AV_SAMPLE_FMT_S16;
            if (packed24 || format->wBitsPerSample == 32) inputFormat = AV_SAMPLE_FMT_S32;
        }
        if (inputFormat == AV_SAMPLE_FMT_NONE)
            return label + QStringLiteral("使用了不支持的音频格式。");

        AVChannelLayout input = {}, output = AV_CHANNEL_LAYOUT_STEREO;
        if (!mask || av_channel_layout_from_mask(&input, mask) < 0
                || input.nb_channels != format->nChannels)
        {
            av_channel_layout_uninit(&input);
            av_channel_layout_default(&input, format->nChannels);
        }
        const int ret = swr_alloc_set_opts2(&resampler, &output, AV_SAMPLE_FMT_FLT,
                UBAudioTimeline::Rate, &input, inputFormat, format->nSamplesPerSec, 0, nullptr);
        av_channel_layout_uninit(&input);
        if (ret < 0 || swr_init(resampler) < 0)
            return label + QStringLiteral("音频转换初始化失败。");

        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0, 2000000, 0, format, nullptr);
        if (FAILED(hr)) return failure(hr);
        hr = client->GetService(__uuidof(IAudioCaptureClient), &capture);
        return FAILED(hr) ? failure(hr) : QString();
    }

    QString begin()
    {
        nextFrame = -1;
        swr_close(resampler);
        if (swr_init(resampler) < 0) return label + QStringLiteral("音频转换重置失败。");
        HRESULT hr = client->Start();
        running = SUCCEEDED(hr);
        return running ? QString() : failure(hr);
    }

    QString read(UBAudioTimeline &timeline, qint64 epoch)
    {
        UINT32 available = 0;
        HRESULT hr = capture->GetNextPacketSize(&available);
        if (FAILED(hr)) return failure(hr);
        while (available)
        {
            BYTE *data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            UINT64 position = 0, timestamp = 0;
            hr = capture->GetBuffer(&data, &frames, &flags, &position, &timestamp);
            if (FAILED(hr)) return failure(hr);
            const bool badTimestamp = flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR;
            qint64 first = audioFrame(qint64(timestamp) - epoch);
            if (badTimestamp)
                first = nextFrame >= 0 ? nextFrame
                        : audioFrame(clock100ns() - epoch) - qint64(frames) * 48000 / format->nSamplesPerSec;
            const qint64 delay = swr_get_delay(resampler, 48000);
            if (nextFrame >= 0 && qAbs(first - (nextFrame + delay)) <= 96)
                first = nextFrame; // Suppress sub-2 ms timestamp rounding/jitter.
            else
            {
                // A genuine idle interval/discontinuity must not be compressed
                // away, nor interpolate stale samples into the next sound.
                swr_close(resampler);
                if (swr_init(resampler) < 0)
                {
                    capture->ReleaseBuffer(frames);
                    return label + QStringLiteral("音频重采样失败。");
                }
            }

            QByteArray silence, expanded;
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
            {
                silence = QByteArray(int(frames * format->nBlockAlign),
                        format->wBitsPerSample == 8 ? char(0x80) : char(0));
                data = reinterpret_cast<BYTE *>(silence.data());
            }
            if (packed24)
            {
                const int samples = int(frames * format->nChannels);
                expanded.resize(samples * 4);
                for (int i = 0; i < samples; ++i)
                {
                    const quint32 value = quint32(data[i*3]) << 8 | quint32(data[i*3+1]) << 16
                            | quint32(data[i*3+2]) << 24;
                    std::memcpy(expanded.data() + i*4, &value, 4);
                }
                data = reinterpret_cast<BYTE *>(expanded.data());
            }
            const int capacity = swr_get_out_samples(resampler, int(frames));
            std::vector<float> converted(qMax(1, capacity) * 2);
            uint8_t *output = reinterpret_cast<uint8_t *>(converted.data());
            const uint8_t *input = data;
            const int count = swr_convert(resampler, &output, capacity, &input, int(frames));
            const HRESULT released = capture->ReleaseBuffer(frames);
            if (count < 0) return label + QStringLiteral("音频重采样失败。");
            if (FAILED(released)) return failure(released);
            if (!timeline.add(first, converted.data(), count))
                return label + QStringLiteral("时间戳异常，录制已安全停止。");
            nextFrame = first + count;
            hr = capture->GetNextPacketSize(&available);
            if (FAILED(hr)) return failure(hr);
        }
        return {};
    }

    void end(UBAudioTimeline &timeline)
    {
        if (running) client->Stop();
        running = false;
        // Drain the resampler delay before resetting for a new pause segment.
        std::vector<float> tail(4096 * 2);
        uint8_t *output = reinterpret_cast<uint8_t *>(tail.data());
        const int count = swr_convert(resampler, &output, 4096, nullptr, 0);
        if (count > 0 && nextFrame >= 0) timeline.add(nextFrame, tail.data(), count);
        client->Reset();
    }
};
}

struct UBWindowsAudioInput::Impl
{
    QString microphone, output, error;
    QMutex mutex;
    QWaitCondition changed;
    std::thread worker;
    QTimer timer;
    QByteArray pending;
    bool ready = false, begin = false, running = false, stop = false, quit = false;
    bool errorDelivered = false;
    qint64 stopTime = 0;

    bool append(const QByteArray &data)
    {
        QMutexLocker lock(&mutex);
        if (pending.size() + data.size() > 48000 * 2 * 4 * 10)
        {
            error = QStringLiteral("录音处理持续落后，已停止录制以防音画不同步。请关闭高负载程序后重试。");
            return false;
        }
        pending.append(data);
        return true;
    }

    void run()
    {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        QString problem;
        {
            ComPtr<IMMDeviceEnumerator> enumerator;
            CaptureSource speaker, mic;
            if (FAILED(com)) problem = QStringLiteral("无法初始化 Windows 音频服务。");
            else if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                             IID_PPV_ARGS(&enumerator))))
                problem = QStringLiteral("无法枚举 Windows 音频设备。");
            if (problem.isEmpty()) problem = speaker.open(enumerator.Get(), true, output);
            const bool withMic = microphone != QStringLiteral("None");
            if (problem.isEmpty() && withMic) problem = mic.open(enumerator.Get(), false, microphone);
            {
                QMutexLocker lock(&mutex);
                error = problem;
                ready = true;
                changed.wakeAll();
            }
            DWORD taskIndex = 0;
            HANDLE priority = problem.isEmpty() ? AvSetMmThreadCharacteristicsW(L"Audio", &taskIndex) : nullptr;
            while (problem.isEmpty())
            {
                {
                    QMutexLocker lock(&mutex);
                    while (!begin && !quit) changed.wait(&mutex);
                    if (quit) break;
                    begin = false;
                }
                UBAudioTimeline timeline;
                const qint64 epoch = clock100ns();
                problem = speaker.begin();
                if (problem.isEmpty() && withMic) problem = mic.begin();
                {
                    QMutexLocker lock(&mutex);
                    running = problem.isEmpty();
                    error = problem;
                    changed.wakeAll();
                }
                qint64 endTime = epoch;
                while (problem.isEmpty())
                {
                    bool ending;
                    {
                        QMutexLocker lock(&mutex);
                        ending = stop || quit;
                        endTime = stop ? stopTime : clock100ns();
                    }
                    problem = speaker.read(timeline, epoch);
                    if (problem.isEmpty() && withMic) problem = mic.read(timeline, epoch);
                    if (!problem.isEmpty() || ending) break;
                    // 100 ms of jitter tolerance, independent of packet arrival
                    // and of UI thread load. Silent periods still emit PCM.
                    if (!append(timeline.takeUntil(audioFrame(endTime - epoch) - 4800)))
                    {
                        QMutexLocker lock(&mutex);
                        problem = error;
                        break;
                    }
                    QMutexLocker lock(&mutex);
                    if (!stop && !quit) changed.wait(&mutex, 5);
                }
                speaker.end(timeline);
                if (withMic) mic.end(timeline);
                if (problem.isEmpty()) append(timeline.takeUntil(audioFrame(endTime - epoch)));
                {
                    QMutexLocker lock(&mutex);
                    if (!problem.isEmpty()) error = problem;
                    running = false;
                    stop = false;
                    changed.wakeAll();
                }
            }
            if (priority) AvRevertMmThreadCharacteristics(priority);
        } // COM objects must be released before CoUninitialize.
        if (SUCCEEDED(com)) CoUninitialize();
    }
};

UBWindowsAudioInput::UBWindowsAudioInput(const QString &outputDevice) : d(new Impl)
{
    d->output = outputDevice;
    d->timer.setInterval(20);
    connect(&d->timer, &QTimer::timeout, this, [this] { drain(); });
}

UBWindowsAudioInput::~UBWindowsAudioInput()
{
    stop();
    {
        QMutexLocker lock(&d->mutex);
        d->quit = true;
        d->changed.wakeAll();
    }
    if (d->worker.joinable()) d->worker.join();
}

void UBWindowsAudioInput::setInputDevice(QString name) { d->microphone = name; }

bool UBWindowsAudioInput::init()
{
    if (!d->worker.joinable()) d->worker = std::thread([this] { d->run(); });
    QMutexLocker lock(&d->mutex);
    while (!d->ready) d->changed.wait(&d->mutex);
    const QString problem = d->error;
    lock.unlock();
    if (!problem.isEmpty()) { drain(); return false; }
    return true;
}

void UBWindowsAudioInput::start()
{
    QMutexLocker lock(&d->mutex);
    if (d->running || !d->ready || !d->error.isEmpty()) return;
    d->stop = false;
    d->pending.clear();
    d->begin = true;
    d->changed.wakeAll();
    while (!d->running && d->error.isEmpty()) d->changed.wait(&d->mutex);
    lock.unlock();
    d->timer.start();
    drain();
}

void UBWindowsAudioInput::stop()
{
    d->timer.stop();
    QMutexLocker lock(&d->mutex);
    if (d->running)
    {
        d->stopTime = clock100ns();
        d->stop = true;
        d->changed.wakeAll();
        while (d->running) d->changed.wait(&d->mutex);
    }
    lock.unlock();
    drain(); // Synchronous tail delivery before the encoder flushes its FIFO.
    emit audioLevelChanged(0);
}

void UBWindowsAudioInput::drain()
{
    QByteArray data;
    QString problem;
    {
        QMutexLocker lock(&d->mutex);
        data.swap(d->pending);
        if (!d->errorDelivered && !d->error.isEmpty())
        {
            problem = d->error;
            d->errorDelivered = true;
        }
    }
    if (!data.isEmpty())
    {
        double squares = 0;
        const qsizetype count = data.size() / sizeof(float);
        for (qsizetype i = 0; i < count; ++i)
        {
            float sample;
            std::memcpy(&sample, data.constData() + i * sizeof(float), sizeof(sample));
            squares += double(sample) * sample;
        }
        emit audioLevelChanged(quint8(qBound(0, int(std::sqrt(squares / count) * 255), 255)));
        emit dataAvailable(data);
    }
    if (!problem.isEmpty()) { d->timer.stop(); emit error(problem); }
}

QStringList UBWindowsAudioInput::outputDevices()
{
    QStringList names;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {
        ComPtr<IMMDeviceEnumerator> enumerator;
        ComPtr<IMMDeviceCollection> devices;
        UINT count = 0;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      IID_PPV_ARGS(&enumerator)))
                && SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices)))
            devices->GetCount(&count);
        for (UINT i = 0; i < count; ++i)
        {
            ComPtr<IMMDevice> device;
            if (SUCCEEDED(devices->Item(i, &device))) names << deviceName(device.Get());
        }
    }
    if (SUCCEEDED(com)) CoUninitialize();
    names.removeAll(QString());
    names.removeDuplicates();
    return names;
}
