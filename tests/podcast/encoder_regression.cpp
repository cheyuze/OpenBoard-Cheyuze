#include <QtCore>
#include <QImage>
#include <QPainter>
#include <cmath>
#include <limits>
#include <stdexcept>
#include "podcast/ffmpeg/UBFFmpegVideoEncoder.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

namespace {
qint64 privateMemoryBytes()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
        return qint64(memory.PrivateUsage);
#endif
    return -1;
}

void require(bool condition, const QString& message)
{
    if (!condition) throw std::runtime_error(message.toStdString());
}

QByteArray tone(qint64 first, int samples, int rate)
{
    QByteArray data(samples * 2 * int(sizeof(qint16)), Qt::Uninitialized);
    auto* pcm = reinterpret_cast<qint16*>(data.data());
    for (int i = 0; i < samples; ++i) {
        const qint16 sample = qint16(8000 * std::sin(2 * 3.141592653589793 * 440 * (first + i) / rate));
        pcm[2*i] = sample;
        pcm[2*i+1] = sample;
    }
    return data;
}

QImage picture(int index, QSize size)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(QColor::fromHsv(index % 360, 220, 200));
    QPainter p(&image);
    p.fillRect((index * 3) % size.width(), 0, qMax(4, size.width() / 10), size.height(), Qt::white);
    p.fillRect(0, (index * 5) % size.height(), size.width(), 4, Qt::black);
    return image;
}

struct MediaSummary {
    qint64 videoFrames = 0;
    qint64 audioPackets = 0;
    double lastVideoTime = -1;
    double lastAudioEnd = -1;
    int changingLateFrames = 0;
    qint64 lastAudioPts = -1;
};

MediaSummary inspect(const QString& path, double lateStart = 710)
{
    AVFormatContext* format = nullptr;
    require(avformat_open_input(&format, path.toUtf8().constData(), nullptr, nullptr) >= 0, "Cannot open " + path);
    require(avformat_find_stream_info(format, nullptr) >= 0, "Cannot read stream info " + path);
    const int video = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int audio = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    require(video >= 0, "No video stream in " + path);
    AVCodecContext* decoder = avcodec_alloc_context3(avcodec_find_decoder(format->streams[video]->codecpar->codec_id));
    require(decoder, "No video decoder");
    require(avcodec_parameters_to_context(decoder, format->streams[video]->codecpar) >= 0, "decoder parameters");
    require(avcodec_open2(decoder, decoder->codec, nullptr) >= 0, "decoder open");
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    MediaSummary summary;
    quint64 previousLateHash = 0;
    auto receive = [&] {
        for (;;) {
            const int result = avcodec_receive_frame(decoder, frame);
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) break;
            require(result >= 0, "Video decoder failure");
            ++summary.videoFrames;
            const double time = frame->best_effort_timestamp * av_q2d(format->streams[video]->time_base);
            require(time + 0.0001 >= summary.lastVideoTime, "Non-monotonic decoded video time");
            summary.lastVideoTime = time;
            if (time >= lateStart) {
                quint64 hash = 1469598103934665603ULL;
                for (int y = 0; y < frame->height; y += 3)
                    for (int x = 0; x < frame->width; x += 3)
                        hash = (hash ^ frame->data[0][y*frame->linesize[0]+x]) * 1099511628211ULL;
                if (hash != previousLateHash) ++summary.changingLateFrames;
                previousLateHash = hash;
            }
            av_frame_unref(frame);
        }
    };
    for (;;) {
        const int result = av_read_frame(format, packet);
        if (result == AVERROR_EOF) break;
        require(result >= 0, "MP4 packet read failure");
        if (packet->stream_index == video) {
            require(avcodec_send_packet(decoder, packet) >= 0, "Video packet rejected by decoder");
            receive();
        } else if (packet->stream_index == audio) {
            ++summary.audioPackets;
            require(packet->pts >= summary.lastAudioPts || summary.audioPackets == 1, "Non-monotonic audio PTS");
            summary.lastAudioPts = packet->pts;
            summary.lastAudioEnd = (packet->pts + packet->duration) * av_q2d(format->streams[audio]->time_base);
        }
        av_packet_unref(packet);
    }
    require(avcodec_send_packet(decoder, nullptr) >= 0, "Video decoder flush");
    receive();
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&decoder);
    avformat_close_input(&format);
    return summary;
}

QJsonObject jsonSummary(const QString& name, const MediaSummary& m)
{
    return {{"name", name}, {"video_frames", m.videoFrames}, {"audio_packets", m.audioPackets},
            {"last_video_seconds", m.lastVideoTime}, {"audio_end_seconds", m.lastAudioEnd},
            {"changing_frames_after_boundary", m.changingLateFrames}};
}
}

class UBFFmpegVideoEncoderTest
{
public:
    explicit UBFFmpegVideoEncoderTest(QString output) : mOutput(std::move(output)) { QDir().mkpath(mOutput); }

    QJsonObject timeline(const QString& name, int seconds, int fps, bool realtime, int rate = 44100, QSize requestedSize = {})
    {
        qputenv("TEST_AUDIO_SAMPLE_RATE", QByteArray::number(rate));
        UBFFmpegVideoEncoder encoder;
        Completion completion;
        connectCompletion(encoder, completion);
        const QSize size = requestedSize.isValid() ? requestedSize : realtime ? QSize(320,180) : QSize(64,64);
        const QString path = mOutput + "/" + name + ".mp4";
        configure(encoder, path, fps, size, true);
        require(encoder.start(), "Start failed: " + encoder.lastErrorMessage());
        QElapsedTimer elapsed;
        elapsed.start();
        qint64 fedSamples = 0;
        QJsonArray memorySamples;
        const int frames = seconds * fps;
        for (int n = 0; n < frames; ++n) {
            const qint64 timestamp = qint64(n) * 1000 / fps;
            if (realtime) {
                while (elapsed.elapsed() < timestamp) {
                    QCoreApplication::processEvents();
                    QThread::msleep(1);
                }
            }
            pace(encoder, completion);
            encoder.newPixmap(picture(n, size), timestamp);
            const qint64 targetSamples = qint64(n + 1) * rate / fps;
            encoder.onAudioAvailable(tone(fedSamples, int(targetSamples-fedSamples), rate));
            fedSamples = targetSamples;
            if (n % (fps * 60) == 0) {
                const qint64 memory = privateMemoryBytes();
                memorySamples.append(QJsonObject{{"timeline_seconds", n/fps}, {"private_bytes", memory}});
                qInfo().noquote() << name << "progress" << n/fps << "s timeline; wall" << elapsed.elapsed()/1000.0 << "s; private MiB" << memory/(1024.0*1024);
            }
        }
        require(encoder.stop(), "stop rejected");
        waitFinished(completion);
        require(completion.ok, "Encoding failed: " + encoder.lastErrorMessage());
        const MediaSummary m = inspect(path, seconds > 715 ? 710 : seconds-60);
        require(m.videoFrames == frames, QString("Video count mismatch: %1/%2").arg(m.videoFrames).arg(frames));
        require(std::abs(m.lastVideoTime - double(frames-1)/fps) < 0.002, "Video timeline truncated");
        require(m.audioPackets > 0 && std::abs(m.lastAudioEnd - seconds) < 0.06, "Audio timeline drift/truncation");
        require(m.changingLateFrames > qMin(frames/2, fps*10), "Video frozen after boundary");
        QJsonObject result = jsonSummary(name, m);
        result.insert("wall_seconds", elapsed.elapsed()/1000.0);
        result.insert("input_sample_rate", rate);
        result.insert("video_size", QString("%1x%2").arg(size.width()).arg(size.height()));
        result.insert("path", path);
        result.insert("memory_samples", memorySamples);
        qInfo().noquote() << "PASS" << QJsonDocument(result).toJson(QJsonDocument::Compact);
        return result;
    }

    QJsonObject rapidStop()
    {
        for (int trial = 0; trial < 20; ++trial) {
            UBFFmpegVideoEncoder encoder;
            Completion completion;
            connectCompletion(encoder, completion);
            const QString path = mOutput + QString("/rapid-stop-%1.mp4").arg(trial);
            configure(encoder, path, 30, QSize(64,64), true);
            require(encoder.start(), "rapid stop init");
            for (int n = 0; n < 3; ++n) encoder.newPixmap(picture(n, QSize(64,64)), qint64(n)*1000/30);
            encoder.onAudioAvailable(tone(0, 4410, 44100));
            encoder.stop();
            waitFinished(completion);
            require(completion.ok, "Rapid stop failed: " + encoder.lastErrorMessage());
            const auto m = inspect(path, 0);
            require(m.videoFrames == 3, "Startup/stop race lost queued video");
            require(m.audioPackets > 0 && m.lastAudioEnd > 0.09, "Startup/stop race lost queued audio");
        }
        qInfo() << "PASS rapid stop x20, queued video and final partial audio preserved";
        return {{"name", "rapid-stop"}, {"runs", 20}, {"pass", true}};
    }

    QJsonObject audio64Bit()
    {
        UBFFmpegVideoEncoder encoder;
        Completion completion;
        connectCompletion(encoder, completion);
        const QString path = mOutput + "/audio-64bit.mp4";
        configure(encoder, path, 30, QSize(64,64), true);
        require(encoder.start(), "audio64 init");
        const qint64 initial = qint64(std::numeric_limits<qint32>::max()) - 1023;
        encoder.mAudioFrameCount = initial;
        const qint64 timestamp = initial * 1000 / 44100;
        for (int n = 0; n < 3; ++n) encoder.newPixmap(picture(n, QSize(64,64)), timestamp + qint64(n)*1000/30);
        encoder.onAudioAvailable(tone(0, 4410, 44100));
        require(encoder.mAudioFrameCount > std::numeric_limits<qint32>::max(), "Audio sample counter wrapped at 32-bit boundary");
        encoder.stop();
        waitFinished(completion);
        require(completion.ok, "audio64 encoding error: " + encoder.lastErrorMessage());
        const auto m = inspect(path, 0);
        require(m.videoFrames == 3, "audio64 video missing");
        require(m.lastAudioPts > std::numeric_limits<qint32>::max(), "Encoded audio packet timestamps wrapped");
        qInfo() << "PASS audio64, final audio PTS" << m.lastAudioPts;
        return {{"name", "audio-64bit"}, {"final_audio_pts", m.lastAudioPts}, {"pass", true}};
    }

    QJsonObject pauseStop()
    {
        UBFFmpegVideoEncoder encoder;
        Completion completion;
        connectCompletion(encoder, completion);
        const QString path = mOutput + "/pause-resume-stop-paused.mp4";
        configure(encoder, path, 30, QSize(64,64), true);
        require(encoder.start(), "pause test init");
        for (int n = 0; n < 90; ++n) {
            pace(encoder, completion);
            encoder.newPixmap(picture(n, QSize(64,64)), qint64(n)*1000/30);
            encoder.onAudioAvailable(tone(qint64(n)*1470, 1470, 44100));
            if (n == 20 || n == 50 || n == 89) {
                require(encoder.pause(), "pause rejected");
                QElapsedTimer paused;
                paused.start();
                while (paused.elapsed() < 100) {
                    QCoreApplication::processEvents();
                    QThread::msleep(1);
                }
                if (n != 89) require(encoder.unpause(), "resume rejected");
            }
        }
        encoder.stop();
        waitFinished(completion);
        require(completion.ok, "Stop while paused failed: " + encoder.lastErrorMessage());
        const auto m = inspect(path, 0);
        require(m.videoFrames == 90 && std::abs(m.lastVideoTime - 89.0/30) < 0.002, "Pause/stop lost video tail");
        require(std::abs(m.lastAudioEnd - 3.0) < 0.06, "Pause/stop lost audio tail");
        qInfo() << "PASS two pause/resume cycles and final stop while paused";
        return jsonSummary("pause-resume-stop-paused", m);
    }

    QJsonObject muxFailure()
    {
        UBFFmpegVideoEncoder encoder;
        Completion completion;
        connectCompletion(encoder, completion);
        configure(encoder, mOutput + "/injected-disk-full.mp4", 30, QSize(64,64), false);
        require(encoder.start(), "mux failure init");
        // No packets are queued yet, so this buffered I/O failure is injected
        // while the encoder worker cannot be inside a muxer write.
        encoder.mOutputFormatContext->pb->error = AVERROR(ENOSPC);
        for (int n = 0; n < 5; ++n) encoder.newPixmap(picture(n, QSize(64,64)), qint64(n)*1000/30);
        encoder.stop();
        waitFinished(completion);
        require(!completion.ok, "Disk-full I/O error incorrectly reported as success");
        require(!encoder.lastErrorMessage().isEmpty(), "Missing user-visible disk-full error");
        qInfo() << "PASS disk full error propagation:" << encoder.lastErrorMessage();
        return {{"name", "mux-failure"}, {"pass", true}, {"message", encoder.lastErrorMessage()}};
    }

    QJsonObject encodeFailure()
    {
        UBFFmpegVideoEncoder encoder;
        Completion completion;
        connectCompletion(encoder, completion);
        configure(encoder, mOutput + "/injected-codec-error.mp4", 30, QSize(64,64), false);
        require(encoder.start(), "codec failure init");
        AVFrame* invalid = av_frame_alloc();
        invalid->format = AV_PIX_FMT_YUV420P;
        invalid->width = 0;
        invalid->height = 0;
        invalid->pts = 0;
        encoder.mVideoWorker->queueVideoFrame(invalid);
        encoder.mVideoWorker->mWaitCondition.wakeAll();
        waitFinished(completion);
        require(!completion.ok, "Invalid video frame incorrectly reported as success");
        require(!encoder.lastErrorMessage().isEmpty(), "Missing codec failure error");
        qInfo() << "PASS codec error propagation:" << encoder.lastErrorMessage();
        return {{"name", "encode-failure"}, {"pass", true}, {"message", encoder.lastErrorMessage()}};
    }

    QJsonObject missingPacket()
    {
        for (bool audio : {false, true}) {
            UBFFmpegVideoEncoder encoder;
            Completion completion;
            connectCompletion(encoder, completion);
            configure(encoder, mOutput + (audio ? "/missing-audio-packet.mp4" : "/missing-video-packet.mp4"), 30, QSize(64,64), audio);
            // Hold only this test instance's started notification so the
            // allocation fault is in place before runEncoding examines it.
            encoder.mVideoEncoderThread->blockSignals(true);
            require(encoder.start(), "Missing packet init");
            if (audio) av_packet_free(&encoder.mVideoWorker->mAudioPacket);
            else av_packet_free(&encoder.mVideoWorker->mVideoPacket);
            encoder.mVideoEncoderThread->blockSignals(false);
            require(QMetaObject::invokeMethod(encoder.mVideoWorker, "runEncoding", Qt::QueuedConnection), "Invoke worker after fault");
            waitFinished(completion);
            require(!completion.ok, "Missing AVPacket reported success");
            require(!encoder.lastErrorMessage().isEmpty(), "Missing AVPacket error message");
        }
        qInfo() << "PASS null video/audio AVPacket each failed cleanly without a crash";
        return {{"name", "missing-packet"}, {"pass", true}, {"cases", 2}};
    }

    QJsonObject overloadQueue()
    {
        UBFFmpegVideoEncoder encoder;
        Completion completion;
        connectCompletion(encoder, completion);
        const QSize size(1920,1080);
        const QString path = mOutput + "/bounded-queue-1080p.mp4";
        configure(encoder, path, 30, size, false);
        // Delay this instance's worker so overproduction is deterministic,
        // independent of how quickly the machine encodes frames today.
        encoder.mVideoEncoderThread->blockSignals(true);
        require(encoder.start(), "bounded queue init");
        for (int n = 0; n < 80; ++n) encoder.newPixmap(picture(n, size), qint64(n)*1000/30);
        const int retained = encoder.mVideoWorker->mImageQueue.size();
        const qint64 queuedBytes = encoder.mVideoWorker->mQueuedVideoBytes;
        const qint64 firstPts = encoder.mVideoWorker->mImageQueue.first()->pts;
        const qint64 lastPts = encoder.mVideoWorker->mImageQueue.last()->pts;
        encoder.stop();
        encoder.mVideoEncoderThread->blockSignals(false);
        require(QMetaObject::invokeMethod(encoder.mVideoWorker, "runEncoding", Qt::QueuedConnection), "Invoke worker after overload");
        waitFinished(completion);
        require(retained > 1 && retained < 60, "1080p video queue did not enforce memory bound");
        require(queuedBytes <= 64*1024*1024, "Video raw queue exceeded 64 MiB");
        require(firstPts == 0, "Overload dropped first frame before worker startup");
        require(lastPts == 7899, "Overload dropped newest frame");
        require(completion.ok, "bounded queue finish: " + encoder.lastErrorMessage());
        const auto media = inspect(path, 0);
        require(media.videoFrames == retained, "Queued survivors were not all encoded");
        require(std::abs(media.lastVideoTime - 79.0/30) < 0.002, "Overload lost final timestamp");
        QJsonObject result = jsonSummary("bounded-queue-1080p", media);
        result.insert("submitted_frames", 80);
        result.insert("retained_frames", retained);
        result.insert("peak_queue_bytes", queuedBytes);
        qInfo().noquote() << "PASS" << QJsonDocument(result).toJson(QJsonDocument::Compact);
        return result;
    }

    QJsonObject audioOverload()
    {
        UBFFmpegVideoEncoder encoder;
        Completion completion;
        connectCompletion(encoder, completion);
        configure(encoder, mOutput + "/audio-overload.mp4", 30, QSize(64,64), true);
        encoder.mVideoEncoderThread->blockSignals(true);
        require(encoder.start(), "audio overload init");
        encoder.newPixmap(picture(0, QSize(64,64)), 0);
        for (int n = 0; n < 120; ++n)
            encoder.onAudioAvailable(tone(qint64(n)*4410, 4410, 44100));
        const int pending = encoder.mVideoWorker->mAudioQueue.size();
        const bool failed = encoder.mVideoWorker->hasFailed();
        const int limit = 44100 * 10 / encoder.mAudioCodecContext->frame_size;
        encoder.mVideoEncoderThread->blockSignals(false);
        require(QMetaObject::invokeMethod(encoder.mVideoWorker, "runEncoding", Qt::QueuedConnection), "Invoke worker after audio overload");
        waitFinished(completion);
        require(failed && !completion.ok, "Audio overcapacity did not fail recording");
        require(pending <= limit, "Audio queue grew past 10-second limit");
        require(!encoder.lastErrorMessage().isEmpty(), "Audio overcapacity had no user error");
        qInfo() << "PASS audio overcapacity bounded at" << pending << "frames, then failed cleanly";
        return {{"name", "audio-overload"}, {"pass", true}, {"queued_frames", pending}, {"limit", limit}};
    }

    QJsonObject synchronousShutdown()
    {
        Completion completion;
        UBFFmpegVideoEncoder encoder;
        connectCompletion(encoder, completion);
        const QString path = mOutput + "/synchronous-shutdown.mp4";
        configure(encoder, path, 30, QSize(64,64), true);
        require(encoder.start(), "shutdown init");
        for (int n = 0; n < 45; ++n) {
            encoder.newPixmap(picture(n, QSize(64,64)), qint64(n)*1000/30);
            encoder.onAudioAvailable(tone(qint64(n)*1470, 1470, 44100));
        }
        // Mimic the application leaving its event loop. No processEvents is
        // allowed until this method has itself completed the file and signal.
        encoder.finishPendingRecording();
        require(completion.finished && completion.ok && completion.calls == 1,
                "Shutdown did not finalize synchronously: " + encoder.lastErrorMessage());
        const auto media = inspect(path, 0);
        require(media.videoFrames == 45, "Shutdown lost queued video frames");
        require(std::abs(media.lastAudioEnd - 1.5) < 0.06, "Shutdown lost final audio");
        QCoreApplication::processEvents();
        require(completion.calls == 1, "Late queued shutdown completion emitted twice");
        qInfo() << "PASS synchronous shutdown without Qt event loop, complete MP4, single completion signal";
        return jsonSummary("synchronous-shutdown", media);
    }

private:
    struct Completion { bool finished = false; bool ok = false; int calls = 0; };
    static void connectCompletion(UBFFmpegVideoEncoder& encoder, Completion& completion)
    {
        QObject::connect(&encoder, &UBAbstractVideoEncoder::encodingFinished, &encoder, [&completion](bool ok) {
            completion.finished = true;
            completion.ok = ok;
            ++completion.calls;
        });
    }
    static void configure(UBFFmpegVideoEncoder& encoder, const QString& path, int fps, QSize size, bool audio)
    {
        encoder.setFramesPerSecond(fps);
        encoder.setVideoSize(size);
        encoder.setVideoBitsPerSecond(12000000);
        encoder.setVideoFileName(path);
        encoder.setRecordAudio(audio);
    }
    static void waitFinished(Completion& completion)
    {
        QElapsedTimer deadline;
        deadline.start();
        while (!completion.finished && deadline.elapsed() < 30000) {
            QCoreApplication::processEvents();
            QThread::msleep(1);
        }
        require(completion.finished, "Stop failed to complete within 30 seconds");
        require(completion.calls == 1, "Completion signal must emit exactly once");
    }
    static void pace(UBFFmpegVideoEncoder& encoder, Completion& completion)
    {
        QElapsedTimer deadline;
        deadline.start();
        for (;;) {
            QCoreApplication::processEvents();
            require(!completion.finished, "Encoder stopped during feed: " + encoder.lastErrorMessage());
            int pending;
            {
                QMutexLocker lock(&encoder.mVideoWorker->mFrameQueueMutex);
                pending = encoder.mVideoWorker->mImageQueue.size() + encoder.mVideoWorker->mAudioQueue.size();
            }
            if (pending < 8) return;
            require(deadline.elapsed() < 10000, "Worker queue stopped draining");
            QThread::msleep(1);
        }
    }
    QString mOutput;
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    av_log_set_level(AV_LOG_ERROR);
    const bool realtime = app.arguments().contains("--realtime");
    const bool shortOnly = app.arguments().contains("--short");
    const bool extended = app.arguments().contains("--extended");
    const QString output = QDir::currentPath() + (realtime ? "/output-realtime" : extended ? "/output-extended" : "/output-regression");
    UBFFmpegVideoEncoderTest test(output);
    QJsonArray results;
    int code = 0;
    auto run = [&](const QString& name, auto function) {
        try {
            results.append(function());
        } catch (const std::exception& e) {
            qCritical() << "FAIL" << name << e.what();
            results.append(QJsonObject{{"name", name}, {"pass", false}, {"error", QString::fromUtf8(e.what())}});
            code = 1;
        }
    };
    if (realtime) {
        run("realtime-15min-30fps", [&] { return test.timeline("realtime-15min-30fps", 900, 30, true); });
    } else if (extended) {
        run("missing-packet", [&] { return test.missingPacket(); });
        run("encode-failure-autonomous", [&] { return test.encodeFailure(); });
        run("bounded-queue-1080p", [&] { return test.overloadQueue(); });
        run("audio-overload", [&] { return test.audioOverload(); });
        run("synchronous-shutdown", [&] { return test.synchronousShutdown(); });
        run("1080p-60sec-30fps", [&] { return test.timeline("1080p-60sec-30fps", 60, 30, false, 44100, QSize(1920,1080)); });
    } else {
        run("rapid-stop", [&] { return test.rapidStop(); });
        run("audio-64bit", [&] { return test.audio64Bit(); });
        run("pause-stop", [&] { return test.pauseStop(); });
        run("mux-failure", [&] { return test.muxFailure(); });
        run("encode-failure", [&] { return test.encodeFailure(); });
        run("resample-48khz", [&] { return test.timeline("resample-48khz", 10, 30, false, 48000); });
        if (!shortOnly) {
            run("accelerated-20min-30fps", [&] { return test.timeline("accelerated-20min-30fps", 1200, 30, false); });
            run("accelerated-8min-60fps", [&] { return test.timeline("accelerated-8min-60fps", 480, 60, false); });
        }
    }
    QFile report(output + "/results.json");
    if (report.open(QIODevice::WriteOnly)) report.write(QJsonDocument(results).toJson(QJsonDocument::Indented));
    qInfo() << "RESULT" << (code == 0 ? "PASS" : "FAIL") << "report" << report.fileName();
    return code;
}
