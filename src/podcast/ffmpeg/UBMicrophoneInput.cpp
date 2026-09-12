/*
 * Copyright (C) 2015-2022 Département de l'Instruction Publique (DIP-SEM)
 *
 * This file is part of OpenBoard.
 *
 * OpenBoard is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License,
 * with a specific linking exception for the OpenSSL project's
 * "OpenSSL" library (or with modified versions of it that use the
 * same license as the "OpenSSL" library).
 *
 * OpenBoard is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenBoard. If not, see <http://www.gnu.org/licenses/>.
 */

#include "UBMicrophoneInput.h"

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
#include <QMediaDevices>
#endif

UBMicrophoneInput::UBMicrophoneInput()
    : mAudioInput(NULL)
    , mIODevice(NULL)
    , mCaptureActive(false)
    , mLastAudioLevel(0)
{
}

UBMicrophoneInput::~UBMicrophoneInput()
{
    if (mAudioInput) {
        disconnect(mAudioInput, nullptr, this, nullptr);
        if (mIODevice)
            disconnect(mIODevice.data(), nullptr, this, nullptr);
        mCaptureActive = false;
        mAudioInput->stop();
        delete mAudioInput;
    }
}

bool UBMicrophoneInput::init()
{
    if (mAudioInput) {
        stop();
        delete mAudioInput;
        mAudioInput = nullptr;
    }

    if (mAudioDeviceInfo.isNull()) {
        qWarning("No audio input device selected; using default");
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        mAudioDeviceInfo = QMediaDevices::defaultAudioInput();
#else
        mAudioDeviceInfo = QAudioDeviceInfo::defaultInputDevice();
#endif
    }

    if (mAudioDeviceInfo.isNull()) {
        qWarning() << "No audio input device is available";
        return false;
    }

    mAudioFormat = mAudioDeviceInfo.preferredFormat();

    /*
     *  https://ffmpeg.org/doxygen/3.1/group__lavu__sampfmts.html#gaf9a51ca15301871723577c730b5865c5
        The data described by the sample format is always in native-endian order.
        Sample values can be expressed by native C types, hence the lack of a signed 24-bit sample format
        even though it is a common raw audio data format.

        If a signed 24-bit sample format is natively preferred, we a set signed 16-bit sample format instead.
    */
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    if (mAudioFormat.bytesPerSample() == 3)
        mAudioFormat.setSampleFormat(QAudioFormat::Int16);
#else
    if (mAudioFormat.sampleSize() == 24) {
        mAudioFormat.setSampleSize(16);
        mAudioFormat.setSampleType(QAudioFormat::SignedInt);
    }
#endif

    if (!mAudioFormat.isValid() || mAudioFormat.sampleRate() <= 0
            || mAudioFormat.channelCount() <= 0 || mAudioFormat.bytesPerFrame() <= 0
            || sampleFormat() < 0 || !mAudioDeviceInfo.isFormatSupported(mAudioFormat)) {
        qWarning() << "The selected audio device does not provide a supported PCM format";
        return false;
    }

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    mAudioInput = new QAudioSource(mAudioDeviceInfo, mAudioFormat);
#else
    mAudioInput = new QAudioInput(mAudioDeviceInfo, mAudioFormat);
#endif

    connect(mAudioInput, SIGNAL(stateChanged(QAudio::State)),
            this, SLOT(onAudioInputStateChanged(QAudio::State)));


#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    qDebug() << "Input device name: " << mAudioDeviceInfo.description();
    qDebug() << "Input sample format: " << (mAudioFormat.bytesPerSample() * 8) << "bit"
             << mAudioFormat.sampleFormat() << "at" << mAudioFormat.sampleRate() << "Hz";
#else
    qDebug() << "Input device name: " << mAudioDeviceInfo.deviceName();
    qDebug() << "Input sample format: " << mAudioFormat.sampleSize() << "bit"
             << mAudioFormat.sampleType() << "at" << mAudioFormat.sampleRate() << "Hz"
             << "; codec: " << mAudioFormat.codec();
#endif

    return true;
}

void UBMicrophoneInput::start()
{
    if (!mAudioInput) {
        emit error(tr("Audio input has not been initialized"));
        return;
    }
    if (mCaptureActive)
        return;

    mPendingAudio.clear();
    mLastAudioLevel = 0;
    mCaptureActive = true;
    mIODevice = mAudioInput->start();

    if (!mIODevice || mAudioInput->error() != QAudio::NoError) {
        // A synchronous stateChanged signal may already have reported the error.
        const bool reportError = mCaptureActive;
        mCaptureActive = false;
        mIODevice = nullptr;
        if (reportError)
            emit error(mAudioInput->error() == QAudio::NoError
                       ? tr("Couldn't open the audio device")
                       : getErrorString(mAudioInput->error()));
        return;
    }

    connect(mIODevice.data(), SIGNAL(readyRead()),
            this, SLOT(onDataReady()), Qt::UniqueConnection);
    onDataReady();
}

void UBMicrophoneInput::stop()
{
    if (!mAudioInput)
        return;

    if (mCaptureActive) {
        // The device discards unread samples when stopped. Preserve its final
        // complete PCM frames, including the partial 100 ms batch on pause.
        onDataReady();
        emitBufferedAudio(true);
    }
    mCaptureActive = false;
    if (mIODevice)
        disconnect(mIODevice.data(), nullptr, this, nullptr);
    mIODevice = nullptr;
    mAudioInput->stop();
    mPendingAudio.clear();
    if (mLastAudioLevel != 0) {
        mLastAudioLevel = 0;
        emit audioLevelChanged(0);
    }
}

QStringList UBMicrophoneInput::availableDevicesNames()
{
    QStringList names;

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    QList<QAudioDevice> devices = QMediaDevices::audioInputs();

    for (const QAudioDevice& device : devices) {
        names.push_back(device.description());
    }
#else
    QList<QAudioDeviceInfo> devices = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);

    foreach (QAudioDeviceInfo device, devices) {
        names.push_back(device.deviceName());
    }
#endif

    return names;
}

void UBMicrophoneInput::setInputDevice(QString name)
{
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    if (name.isEmpty()) {
        mAudioDeviceInfo = QMediaDevices::defaultAudioInput();
        return;
    }

    QList<QAudioDevice> devices = QMediaDevices::audioInputs();
    bool found = false;

    for (const QAudioDevice& device : devices) {
        if (device.description() == name) {
            mAudioDeviceInfo = device;
            found = true;
            break;
        }
    }

    if (!found) {
        qWarning() << "Audio input device not found; using default instead";
        mAudioDeviceInfo = QMediaDevices::defaultAudioInput();
    }
#else
    if (name.isEmpty()) {
        mAudioDeviceInfo = QAudioDeviceInfo::defaultInputDevice();
        return;
    }

    QList<QAudioDeviceInfo> devices = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);
    bool found = false;

    foreach (QAudioDeviceInfo device, devices) {
        if (device.deviceName() == name) {
            mAudioDeviceInfo = device;
            found = true;
            break;
        }
    }

    if (!found) {
        qWarning() << "Audio input device not found; using default instead";
        mAudioDeviceInfo = QAudioDeviceInfo::defaultInputDevice();
    }
#endif
}

int UBMicrophoneInput::channelCount()
{
    return mAudioFormat.channelCount();
}

int UBMicrophoneInput::sampleRate()
{
    return mAudioFormat.sampleRate();
}

/* Return the sample size in bits */
int UBMicrophoneInput::sampleSize()
{
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    return 8 * mAudioFormat.bytesPerSample();
#else
    return mAudioFormat.sampleSize();
#endif
}

/** Return the sample format in FFMpeg style (AVSampleFormat enum) */
int UBMicrophoneInput::sampleFormat()
{
    enum AVSampleFormat {
        AV_SAMPLE_FMT_NONE = -1,
        AV_SAMPLE_FMT_U8,
        AV_SAMPLE_FMT_S16,
        AV_SAMPLE_FMT_S32,
        AV_SAMPLE_FMT_FLT,
        AV_SAMPLE_FMT_DBL,
        AV_SAMPLE_FMT_U8P,
        AV_SAMPLE_FMT_S16P,
        AV_SAMPLE_FMT_S32P,
        AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_DBLP,
        AV_SAMPLE_FMT_NB
    };

    int sampleSizeBits = sampleSize();

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    QAudioFormat::SampleFormat sampleType = mAudioFormat.sampleFormat();
#else
    QAudioFormat::SampleType sampleType = mAudioFormat.sampleType();
#endif

    switch (sampleType) {
        case QAudioFormat::Unknown:
            return AV_SAMPLE_FMT_NONE;

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        case QAudioFormat::Int16:
            return AV_SAMPLE_FMT_S16;

        case QAudioFormat::Int32:
            return AV_SAMPLE_FMT_S32;

        case QAudioFormat::UInt8:
            return AV_SAMPLE_FMT_U8;
#else
        case QAudioFormat::SignedInt:
            if (sampleSizeBits == 16)
                return AV_SAMPLE_FMT_S16;
            if (sampleSizeBits == 32)
                return AV_SAMPLE_FMT_S32;
            break;

        case QAudioFormat::UnSignedInt:
            if (sampleSizeBits == 8)
                return AV_SAMPLE_FMT_U8;
#endif
            break;

        case QAudioFormat::Float:
            if (sampleSizeBits == 32)
                return AV_SAMPLE_FMT_FLT;
            break;

        default:
            return AV_SAMPLE_FMT_NONE;
    }

    return AV_SAMPLE_FMT_NONE;
}

void UBMicrophoneInput::onDataReady()
{
    if (!mCaptureActive || !mIODevice)
        return;

    // Read each byte once. Counting bytesReady repeatedly without consuming it
    // overcounts the same samples, and a static counter leaks across recordings.
    qint64 remaining = mIODevice->bytesAvailable();
    while (remaining > 0 && mCaptureActive && mIODevice) {
        const QByteArray data = mIODevice->read(qMin<qint64>(remaining, 64 * 1024));
        if (data.isEmpty())
            break;
        remaining -= data.size();
        mPendingAudio.append(data);
        emitBufferedAudio(false);
    }
}

void UBMicrophoneInput::emitBufferedAudio(bool flush)
{
    const int frameBytes = mAudioFormat.bytesPerFrame();
    if (frameBytes <= 0)
        return;
    const int chunkBytes = qMax(frameBytes,
                               mAudioFormat.bytesForDuration(100000) / frameBytes * frameBytes);

    while (mCaptureActive && mPendingAudio.size() >= (flush ? frameBytes : chunkBytes)) {
        const int bytes = qMin<qint64>(chunkBytes, mPendingAudio.size() / frameBytes * frameBytes);
        const QByteArray data = mPendingAudio.left(bytes);
        mPendingAudio.remove(0, bytes);
        const quint8 level = audioLevel(data);
        if (level != mLastAudioLevel) {
            mLastAudioLevel = level;
            emit audioLevelChanged(level);
        }

        emit dataAvailable(data);
    }
}

void UBMicrophoneInput::onAudioInputStateChanged(QAudio::State state)
{
    switch (state) {
        case QAudio::StoppedState:
            if (mAudioInput->error() != QAudio::NoError) {
                mCaptureActive = false;
                emit error(getErrorString(mAudioInput->error()));
            }
            break;

        // handle other states?

        default:
            break;
    }
}

/**
 * @brief Calculate the current audio level of an array of samples and return it
 * @param data An array of audio samples
 * @return A value between 0 and 255
 *
 * Audio level is calculated as the RMS (root mean square) of the samples
 * in the supplied array.
 */
quint8 UBMicrophoneInput::audioLevel(const QByteArray &data)
{
    if (data.isEmpty() || mAudioFormat.channelCount() <= 0)
        return 0;
    int bytesPerSample = mAudioFormat.bytesPerFrame() / mAudioFormat.channelCount();
    if (bytesPerSample <= 0 || data.size() < bytesPerSample)
        return 0;

    const char * ptr = data.constData();
    double sum = 0;
    int n_samples = data.size() / bytesPerSample;

    for (int i(0); i <= (data.size() - bytesPerSample); i += bytesPerSample) {
        sum += pow(sampleRelativeLevel(ptr + i), 2);
    }

    double rms = sqrt(sum/n_samples);

    // The vu meter looks a bit better when the RMS isn't displayed linearly, as perceived sound
    // level increases logarithmically. So here RMS is substituted by rms^(1/e)
    rms = pow(rms, 1./exp(1));

    return UINT8_MAX * qBound(0.0, rms, 1.0);
}

/**
 * @brief Calculate one sample's level relative to its maximum value
 * @param sample One sample, in the format specified by mAudioFormat
 * @return A double between 0 and 1.0, where 1.0 is the maximum value the sample can take,
 *         or -1 if the value couldn't be calculated.
 */
double UBMicrophoneInput::sampleRelativeLevel(const char* sample)
{
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    QAudioFormat::SampleFormat type =  mAudioFormat.sampleFormat();
    int sampleSizeBits = sampleSize();

    if (sampleSizeBits == 16 && type == QAudioFormat::Int16)
        return double(*reinterpret_cast<const int16_t*>(sample))/INT16_MAX;

    if (sampleSizeBits == 8 && type == QAudioFormat::UInt8)
        return double(*reinterpret_cast<const uint8_t*>(sample))/UINT8_MAX;

    if (type == QAudioFormat::Float)
        return (*reinterpret_cast<const float*>(sample) + 1.0)/2.;
#else
    QAudioFormat::SampleType type =  mAudioFormat.sampleType();
    int sampleSize = mAudioFormat.sampleSize();

    if (sampleSize == 16 && type == QAudioFormat::SignedInt)
        return double(*reinterpret_cast<const int16_t*>(sample))/INT16_MAX;

    if (sampleSize == 8 && type == QAudioFormat::SignedInt)
        return double(*reinterpret_cast<const int8_t*>(sample))/INT8_MAX;

    if (sampleSize == 16 && type == QAudioFormat::UnSignedInt)
        return double(*reinterpret_cast<const uint16_t*>(sample))/UINT16_MAX;

    if (sampleSize == 8 && type == QAudioFormat::UnSignedInt)
        return double(*reinterpret_cast<const uint8_t*>(sample))/UINT8_MAX;

    if (type == QAudioFormat::Float)
        return (*reinterpret_cast<const float*>(sample) + 1.0)/2.;
#endif

    return -1;
}

/**
 * @brief Return a meaningful error string based on QAudio error codes
 */
QString UBMicrophoneInput::getErrorString(QAudio::Error errorCode)
{
    switch (errorCode) {
        case QAudio::NoError :
            return "";

        case QAudio::OpenError :
            return "Couldn't open the audio device";

        case QAudio::IOError :
            return "Error reading from audio device";

        case QAudio::UnderrunError :
            return "Underrun error";

        case QAudio::FatalError :
            return "Fatal error; audio device unusable";

        default:
            return "unhandled error...";
    }
    return "";
}
