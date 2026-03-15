#include "convertertool.h"
#include <QDebug>
#include <QFileInfo>

ConverterTool::ConverterTool(QObject *parent) : QObject(parent)
{

}

void ConverterTool::startConvert(QString inputPath, QString outputPath)
{
    AVFormatContext *ifmtCtx = nullptr;
    AVFormatContext *ofmtCtx = nullptr;
    int ret = 0;

    int videoIndex = -1;
    int audioIndex = -1;

    AVCodecContext *videoDecCtx = nullptr;
    AVCodecContext *audioDecCtx = nullptr;
    AVCodecContext *videoEncCtx = nullptr;
    AVCodecContext *audioEncCtx = nullptr;

    SwrContext *swrCtx = nullptr;
    AVAudioFifo *audioFifo = nullptr; // 本地FIFO，不依赖成员变量更稳定

    AVPacket pkt;
    int64_t audioPts = 0;
    int64_t valid_pts = 0;
    int64_t m_nextPts = 0;

    // ==========================
    // 1. 打开输入文件
    // ==========================
    ret = avformat_open_input(&ifmtCtx, inputPath.toLocal8Bit().data(), nullptr, nullptr);
    if (ret < 0) {
        emit finished(false);
        return;
    }
    avformat_find_stream_info(ifmtCtx, nullptr);

    // ==========================
    // 2. 查找音视频流
    // ==========================
    for (int i = 0; i < ifmtCtx->nb_streams; i++) {
        if (ifmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            videoIndex = i;
        if (ifmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            audioIndex = i;
    }

    if (videoIndex < 0 || audioIndex < 0) {
        emit finished(false);
        return;
    }

    AVStream *inVideo = ifmtCtx->streams[videoIndex];
    AVStream *inAudio = ifmtCtx->streams[audioIndex];

    // ==========================
    // 3. 视频解码器
    // ==========================
    const AVCodec *vDec = avcodec_find_decoder(inVideo->codecpar->codec_id);
    videoDecCtx = avcodec_alloc_context3(vDec);
    avcodec_parameters_to_context(videoDecCtx, inVideo->codecpar);
    if (avcodec_open2(videoDecCtx, vDec, nullptr) < 0) {
        emit finished(false);
        return;
    }

    // ==========================
    // 4. 音频解码器
    // ==========================
    const AVCodec *aDec = avcodec_find_decoder(inAudio->codecpar->codec_id);
    audioDecCtx = avcodec_alloc_context3(aDec);
    avcodec_parameters_to_context(audioDecCtx, inAudio->codecpar);
    if (avcodec_open2(audioDecCtx, aDec, nullptr) < 0) {
        emit finished(false);
        return;
    }

    // ==========================
    // 5. 创建输出 MP4
    // ==========================
    avformat_alloc_output_context2(&ofmtCtx, nullptr, nullptr, outputPath.toLocal8Bit().data());
    if (!ofmtCtx) {
        emit finished(false);
        return;
    }

    // ==========================
    // 6. 视频编码器
    // ==========================
    enum AVCodecID id = AV_CODEC_ID_H264;
    if (m_videoEncode == "libx264")
        id = AV_CODEC_ID_H264;
    else if (m_videoEncode == "libx265")
        id = AV_CODEC_ID_H265;
    else if (m_videoEncode == "libxvid")
        id = AV_CODEC_ID_MPEG4;

    const AVCodec *vEnc = avcodec_find_encoder(id);
    videoEncCtx = avcodec_alloc_context3(vEnc);
    videoEncCtx->width = m_width;
    videoEncCtx->height = m_height;
    videoEncCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    videoEncCtx->time_base = (AVRational){ 1, m_frameRate };
    videoEncCtx->framerate = (AVRational){ m_frameRate, 1 };
    videoEncCtx->bit_rate = m_bitrate;

    if (m_videoEncode == "libx264")
        videoEncCtx->profile = FF_PROFILE_H264_MAIN;

    av_opt_set(videoEncCtx->priv_data, "preset", "fast", 0);

    AVStream *outVideo = avformat_new_stream(ofmtCtx, vEnc);
    avcodec_parameters_from_context(outVideo->codecpar, videoEncCtx);
    outVideo->time_base = videoEncCtx->time_base;
    if (avcodec_open2(videoEncCtx, vEnc, nullptr) < 0) {
        emit finished(false);
        return;
    }

    // ==========================
    // 7. 音频编码器
    // ==========================
    if (m_audioEncode == "aac")
        id = AV_CODEC_ID_AAC;
    else if (m_audioEncode == "libmp3lame")
        id = AV_CODEC_ID_MP3;
    else
        id = AV_CODEC_ID_AAC;

    const AVCodec *aEnc = avcodec_find_encoder(id);
    audioEncCtx = avcodec_alloc_context3(aEnc);

    if (m_audioEncode == "libmp3lame")
        audioEncCtx->sample_fmt = AV_SAMPLE_FMT_S16P;
    else
        audioEncCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;

    audioEncCtx->sample_rate = audioDecCtx->sample_rate;
    audioEncCtx->channels = audioDecCtx->channels;
    audioEncCtx->channel_layout = av_get_default_channel_layout(audioEncCtx->channels);
    audioEncCtx->bit_rate = 128000;
    audioEncCtx->time_base = { 1, audioEncCtx->sample_rate };

    if (avcodec_open2(audioEncCtx, aEnc, nullptr) < 0) {
        emit finished(false);
        return;
    }

    AVStream *outAudio = avformat_new_stream(ofmtCtx, aEnc);
    avcodec_parameters_from_context(outAudio->codecpar, audioEncCtx);
    outAudio->time_base = audioEncCtx->time_base;

    // ==========================
    // 8. 重采样 + FIFO
    // ==========================
    swrCtx = swr_alloc_set_opts(nullptr,
        audioEncCtx->channel_layout, audioEncCtx->sample_fmt, audioEncCtx->sample_rate,
        audioDecCtx->channel_layout, audioDecCtx->sample_fmt, audioDecCtx->sample_rate,
        0, nullptr);
    swr_init(swrCtx);

    audioFifo = av_audio_fifo_alloc(
        audioEncCtx->sample_fmt,
        audioEncCtx->channels,
        4096 * 4
    );

    // ==========================
    // 9. 打开输出
    // ==========================
    if (avio_open(&ofmtCtx->pb, outputPath.toLocal8Bit().data(), AVIO_FLAG_WRITE) < 0) {
        emit finished(false);
        return;
    }
    avformat_write_header(ofmtCtx, nullptr);

    // ==========================
    // 10. 主循环
    // ==========================
    while (av_read_frame(ifmtCtx, &pkt) == 0)
    {
        if (pkt.stream_index == videoIndex) {
            avcodec_send_packet(videoDecCtx, &pkt);
            AVFrame *frame = av_frame_alloc();
            if (avcodec_receive_frame(videoDecCtx, frame) == 0) {

                AVFrame *frameForEncode = frame;

                if (videoDecCtx->width != videoEncCtx->width || videoDecCtx->height != videoEncCtx->height)
                {
                    AVFrame *frameScaled = av_frame_alloc();
                    frameScaled->format = AV_PIX_FMT_YUV420P;
                    frameScaled->width = videoEncCtx->width;
                    frameScaled->height = videoEncCtx->height;
                    av_frame_get_buffer(frameScaled, 0);
                    av_frame_make_writable(frameScaled);

                    SwsContext *swsCtx = sws_getContext(
                        frame->width, frame->height, videoDecCtx->pix_fmt,
                        videoEncCtx->width, videoEncCtx->height, AV_PIX_FMT_YUV420P,
                        SWS_BILINEAR, NULL, NULL, NULL);

                    sws_scale(swsCtx,
                        frame->data, frame->linesize, 0, frame->height,
                        frameScaled->data, frameScaled->linesize);

                    sws_freeContext(swsCtx);
                    frameForEncode = frameScaled;
                }

                frameForEncode->pts = m_nextPts++;

                avcodec_send_frame(videoEncCtx, frameForEncode);
                AVPacket epkt;
                av_init_packet(&epkt);
                epkt.data = nullptr;
                epkt.size = 0;

                if (avcodec_receive_packet(videoEncCtx, &epkt) == 0) {
                    epkt.stream_index = outVideo->index;
                    av_packet_rescale_ts(&epkt, videoEncCtx->time_base, outVideo->time_base);
                    av_interleaved_write_frame(ofmtCtx, &epkt);
                    av_packet_unref(&epkt);
                }

                if (frameForEncode != frame)
                    av_frame_free(&frameForEncode);
            }
            valid_pts = frame->pts;
            av_frame_free(&frame);
        }
        else if (pkt.stream_index == audioIndex)
        {
            avcodec_send_packet(audioDecCtx, &pkt);
            AVFrame *frame = av_frame_alloc();

            while (avcodec_receive_frame(audioDecCtx, frame) == 0)
            {
                int maxOutputSamples = av_rescale_rnd(
                    frame->nb_samples,
                    audioEncCtx->sample_rate,
                    audioDecCtx->sample_rate,
                    AV_ROUND_UP
                );

                AVFrame *resampledFrame = av_frame_alloc();
                resampledFrame->format = audioEncCtx->sample_fmt;
                resampledFrame->channel_layout = audioEncCtx->channel_layout;
                resampledFrame->sample_rate = audioEncCtx->sample_rate;
                resampledFrame->nb_samples = maxOutputSamples;
                av_frame_get_buffer(resampledFrame, 0);
                av_frame_make_writable(resampledFrame);

                int converted = swr_convert(swrCtx,
                    resampledFrame->data, resampledFrame->nb_samples,
                    (const uint8_t**)frame->data, frame->nb_samples
                );

                resampledFrame->nb_samples = converted;
                av_audio_fifo_write(audioFifo, (void**)resampledFrame->data, converted);

                while (av_audio_fifo_size(audioFifo) >= audioEncCtx->frame_size)
                {
                    AVFrame *encodeFrame = av_frame_alloc();
                    encodeFrame->nb_samples = audioEncCtx->frame_size;
                    encodeFrame->format = audioEncCtx->sample_fmt;
                    encodeFrame->channel_layout = audioEncCtx->channel_layout;
                    encodeFrame->sample_rate = audioEncCtx->sample_rate;
                    av_frame_get_buffer(encodeFrame, 0);

                    av_audio_fifo_read(audioFifo, (void**)encodeFrame->data, audioEncCtx->frame_size);
                    encodeFrame->pts = audioPts;
                    audioPts += encodeFrame->nb_samples;

                    if (avcodec_send_frame(audioEncCtx, encodeFrame) >= 0) {
                        AVPacket epkt;
                        av_init_packet(&epkt);
                        while (avcodec_receive_packet(audioEncCtx, &epkt) == 0) {
                            epkt.stream_index = outAudio->index;
                            av_packet_rescale_ts(&epkt, audioEncCtx->time_base, outAudio->time_base);
                            av_interleaved_write_frame(ofmtCtx, &epkt);
                            av_packet_unref(&epkt);
                        }
                    }
                    av_frame_free(&encodeFrame);
                }
                av_frame_free(&resampledFrame);
            }
            av_frame_free(&frame);
        }

        av_packet_unref(&pkt);

        double current_sec = valid_pts * av_q2d(inVideo->time_base);
        double total_sec = inVideo->duration * av_q2d(inVideo->time_base);
        double prog = (current_sec / total_sec) * 100.0;
        prog = qBound(0.0, prog, 100.0);
        emit progress(prog);
    }

    emit progress(100);

    // ==========================
    // 刷新音频FIFO剩余数据
    // ==========================
    while (av_audio_fifo_size(audioFifo) > 0)
    {
        AVFrame *encodeFrame = av_frame_alloc();
        encodeFrame->nb_samples = FFMIN(av_audio_fifo_size(audioFifo), audioEncCtx->frame_size);
        encodeFrame->format = audioEncCtx->sample_fmt;
        encodeFrame->channel_layout = audioEncCtx->channel_layout;
        av_frame_get_buffer(encodeFrame, 0);

        av_audio_fifo_read(audioFifo, (void**)encodeFrame->data, encodeFrame->nb_samples);
        encodeFrame->pts = audioPts;
        audioPts += encodeFrame->nb_samples;

        avcodec_send_frame(audioEncCtx, encodeFrame);
        av_frame_free(&encodeFrame);
    }

    // ==========================
    // 刷新编码器
    // ==========================
    avcodec_send_frame(videoEncCtx, nullptr);
    AVPacket epkt;
    av_init_packet(&epkt);
    while (avcodec_receive_packet(videoEncCtx, &epkt) == 0) {
        epkt.stream_index = outVideo->index;
        av_packet_rescale_ts(&epkt, videoEncCtx->time_base, outVideo->time_base);
        av_interleaved_write_frame(ofmtCtx, &epkt);
        av_packet_unref(&epkt);
    }

    avcodec_send_frame(audioEncCtx, nullptr);
    while (avcodec_receive_packet(audioEncCtx, &epkt) == 0) {
        epkt.stream_index = outAudio->index;
        av_packet_rescale_ts(&epkt, audioEncCtx->time_base, outAudio->time_base);
        av_interleaved_write_frame(ofmtCtx, &epkt);
        av_packet_unref(&epkt);
    }

    // ==========================
    // 收尾释放
    // ==========================
    av_write_trailer(ofmtCtx);
    avio_close(ofmtCtx->pb);

    av_audio_fifo_free(audioFifo);
    swr_free(&swrCtx);

    avformat_close_input(&ifmtCtx);
    avformat_free_context(ofmtCtx);

    avcodec_free_context(&videoDecCtx);
    avcodec_free_context(&audioDecCtx);
    avcodec_free_context(&videoEncCtx);
    avcodec_free_context(&audioEncCtx);

    emit finished(true);
}

void ConverterTool::setParam(QStringList inputPaths, QString outputPath)
{
    m_inPaths = inputPaths;
    m_outPath = outputPath;
}

void ConverterTool::setResolution(int widget, int height)
{
    m_width = widget;
    m_height = height;
}

void ConverterTool::setMuxerType(QString type)
{
    m_muxerType = type;
}

void ConverterTool::setBitrate(int64_t bitrate)
{
    m_bitrate = bitrate;
}

void ConverterTool::setFrameRate(int frameRate)
{
    m_frameRate = frameRate;
}

void ConverterTool::setVideoEncode(QString videoEncode)
{
    m_videoEncode = videoEncode;
}

void ConverterTool::setAudioEncode(QString audioEncode)
{
    m_audioEncode = audioEncode;
}

void ConverterTool::doWork()
{
    for (int i = 0; i < m_inPaths.size(); i++)
    {
        QString inPath = m_inPaths.at(i);
        QFileInfo fileInfo(inPath);
        m_nextPts = 0;
        startConvert(inPath, m_outPath + "/out_" + fileInfo.baseName() + "." + m_muxerType);
    }
}

