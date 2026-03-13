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
    avcodec_open2(videoDecCtx, vDec, nullptr);

    // ==========================
    // 4. 音频解码器
    // ==========================
    const AVCodec *aDec = avcodec_find_decoder(inAudio->codecpar->codec_id);
    audioDecCtx = avcodec_alloc_context3(aDec);
    avcodec_parameters_to_context(audioDecCtx, inAudio->codecpar);
    avcodec_open2(audioDecCtx, aDec, nullptr);

    // ==========================
    // 5. 创建输出 MP4
    // ==========================
    avformat_alloc_output_context2(&ofmtCtx, nullptr, nullptr, outputPath.toLocal8Bit().data());

    // ==========================
    // 6. 视频编码器 H.265
    // ==========================
    const AVCodec *vEnc = avcodec_find_encoder(AV_CODEC_ID_H264);
    videoEncCtx = avcodec_alloc_context3(vEnc);
    videoEncCtx->width = m_width;
    videoEncCtx->height = m_height;
    videoEncCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    videoEncCtx->time_base = inVideo->time_base;
    videoEncCtx->framerate = av_guess_frame_rate(ifmtCtx, inVideo, nullptr);
    videoEncCtx->bit_rate = m_bitrate;

    // H.264 必须加 profile，否则兼容性差
    videoEncCtx->profile = FF_PROFILE_H264_MAIN;

    av_opt_set(videoEncCtx->priv_data, "preset", "fast", 0);
    //av_opt_set(videoEncCtx->priv_data, "crf", "23", 0);

    AVStream *outVideo = avformat_new_stream(ofmtCtx, vEnc);
    avcodec_parameters_from_context(outVideo->codecpar, videoEncCtx);
    outVideo->time_base = videoEncCtx->time_base;
    avcodec_open2(videoEncCtx, vEnc, nullptr);

    // ==========================
    // 7. 音频编码器 AAC（修复）
    // ==========================
    const AVCodec *aEnc = avcodec_find_encoder(AV_CODEC_ID_AAC);
    audioEncCtx = avcodec_alloc_context3(aEnc);

    audioEncCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    audioEncCtx->sample_rate = audioDecCtx->sample_rate;
    audioEncCtx->channels = audioDecCtx->channels;
    audioEncCtx->channel_layout = av_get_default_channel_layout(audioEncCtx->channels);
    audioEncCtx->bit_rate = 128000;
    audioEncCtx->time_base = { 1, audioEncCtx->sample_rate };


    AVStream *outAudio = avformat_new_stream(ofmtCtx, aEnc);
    avcodec_parameters_from_context(outAudio->codecpar, audioEncCtx);
    outAudio->time_base = audioEncCtx->time_base;
    avcodec_open2(audioEncCtx, aEnc, nullptr);

    // ==========================
    // 8. 音频重采样（安全版）
    // ==========================
    swrCtx = swr_alloc_set_opts(nullptr,
        audioEncCtx->channel_layout, audioEncCtx->sample_fmt, audioEncCtx->sample_rate,
        audioDecCtx->channel_layout, audioDecCtx->sample_fmt, audioDecCtx->sample_rate,
        0, nullptr);
    swr_init(swrCtx);

    // ==========================
    // 9. 打开输出文件
    // ==========================
    avio_open(&ofmtCtx->pb, outputPath.toLocal8Bit().data(), AVIO_FLAG_WRITE);
    avformat_write_header(ofmtCtx, nullptr);

    // ==========================
    // 10. 转码主循环
    // ==========================
    AVPacket pkt;
    int64_t audioPts = 0;
    int64_t valid_pts = 0;

    while (av_read_frame(ifmtCtx, &pkt) == 0)
    {
        if (pkt.stream_index == videoIndex) {
            // ----------------------
            // 视频处理
            // ----------------------
            avcodec_send_packet(videoDecCtx, &pkt);
            AVFrame *frame = av_frame_alloc();
            if (avcodec_receive_frame(videoDecCtx, frame) == 0) {

                AVFrame *frameForEncode = frame; // 默认直接使用原帧（不缩放）
                // ==========================
                // 正确创建目标帧（关键修复）
                // ==========================
                if (videoDecCtx->width != videoEncCtx->width ||
                    videoDecCtx->height != videoEncCtx->height)
                {
                    AVFrame *frameScaled = av_frame_alloc();
                    frameScaled->format = AV_PIX_FMT_YUV420P;
                    frameScaled->width  = videoEncCtx->width;
                    frameScaled->height = videoEncCtx->height;

                    // 👇 必须加这句！分配图像缓冲区
                    av_frame_get_buffer(frameScaled, 0);

                    // 👇 必须加这句！清空数据，防止野指针
                    av_frame_make_writable(frameScaled);
                    // ==========================
                    // ✅ 修复视频倍速关键代码
                    // ==========================
                    frameScaled->pts = frame->pts;  // 继承原始时间戳！！！

                    // ==========================
                    // 缩放器（正确写法）
                    // ==========================
                    SwsContext *swsCtx = sws_getContext(
                        frame->width, frame->height, videoDecCtx->pix_fmt,
                        videoEncCtx->width, videoEncCtx->height, AV_PIX_FMT_YUV420P,
                        SWS_BILINEAR, NULL, NULL, NULL);

                    sws_scale(swsCtx,
                        frame->data, frame->linesize, 0, frame->height,
                        frameScaled->data, frameScaled->linesize);

                    sws_freeContext(swsCtx);
                    // 用缩放后的帧编码
                    frameForEncode = frameScaled;
                }

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
                // 释放
                if (frameForEncode != frame) {
                    av_frame_free(&frameForEncode);
                }
            }
            valid_pts = frame->pts;
            av_frame_free(&frame);
        }
        else if (pkt.stream_index == audioIndex) {
            // ----------------------
            // 音频处理（彻底修复）
            // ----------------------
            avcodec_send_packet(audioDecCtx, &pkt);
            AVFrame *frame = av_frame_alloc();
            while (avcodec_receive_frame(audioDecCtx, frame) == 0)
            {
                AVFrame *outFrame = av_frame_alloc();
                outFrame->nb_samples = audioEncCtx->frame_size;
                outFrame->format = audioEncCtx->sample_fmt;
                outFrame->channel_layout = audioEncCtx->channel_layout;
                outFrame->sample_rate = audioEncCtx->sample_rate;
                av_frame_get_buffer(outFrame, 0);

                // 重采样
                swr_convert(swrCtx,
                    outFrame->data, outFrame->nb_samples,
                    (const uint8_t**)frame->data, frame->nb_samples);

                // 音频 PTS（安全自增，永不崩溃）
                outFrame->pts = audioPts;
                audioPts += outFrame->nb_samples;

                // 编码
                if (avcodec_send_frame(audioEncCtx, outFrame) >= 0) {
                    AVPacket epkt;
                    av_init_packet(&epkt);
                    epkt.data = nullptr;
                    epkt.size = 0;
                    while (avcodec_receive_packet(audioEncCtx, &epkt) == 0) {
                        epkt.stream_index = outAudio->index;
                        av_packet_rescale_ts(&epkt, audioEncCtx->time_base, outAudio->time_base);
                        av_interleaved_write_frame(ofmtCtx, &epkt);
                        av_packet_unref(&epkt);
                    }
                }
                av_frame_free(&outFrame);
            }
            av_frame_free(&frame);
        }

        av_packet_unref(&pkt);

        // 安全的进度计算（修复负数、异常值）
        if (inVideo->duration <= 0) {
            qDebug() << "视频时长无效，跳过进度计算";
            //return;
        }

        // 转换为秒（FFmpeg标准写法）
        double current_sec = valid_pts * av_q2d(inVideo->time_base);
        double total_sec = inVideo->duration * av_q2d(inVideo->time_base);

        double prog = (current_sec / total_sec) * 100.0;

        // 边界限制（防止超出0-100）
        prog = qBound(0.0, prog, 100.0);;

        emit progress(prog);
    }
    emit progress(100); //跳出来后强制100%
    // ==========================
    // 11. 【关键】安全刷新编码器
    // ==========================
//    avcodec_send_frame(videoEncCtx, nullptr);
//    AVPacket epkt;
//    while (avcodec_receive_packet(videoEncCtx, &epkt) == 0) {
//        epkt.stream_index = outVideo->index;
//        av_packet_rescale_ts(&epkt, videoEncCtx->time_base, outVideo->time_base);
//        av_interleaved_write_frame(ofmtCtx, &epkt);
//        av_packet_unref(&epkt);
//    }

//    avcodec_send_frame(audioEncCtx, nullptr);
//    while (avcodec_receive_packet(audioEncCtx, &epkt) == 0) {
//        epkt.stream_index = outAudio->index;
//        av_packet_rescale_ts(&epkt, audioEncCtx->time_base, outAudio->time_base);
//        av_interleaved_write_frame(ofmtCtx, &epkt);
//        av_packet_unref(&epkt);
//    }

    // ==========================
    // 12. 安全收尾释放
    // ==========================
    av_write_trailer(ofmtCtx);
    avio_close(ofmtCtx->pb);

    av_frame_free(nullptr);
    av_packet_unref(&pkt);

    avformat_close_input(&ifmtCtx);
    avformat_free_context(ofmtCtx);

    swr_free(&swrCtx);
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

void ConverterTool::doWork()
{
    for (int i = 0; i < m_inPaths.size(); i++)
    {
        QString inPath = m_inPaths.at(i);
        QFileInfo fileInfo(inPath);
        startConvert(inPath, m_outPath + "/out_" + fileInfo.baseName() + "." + m_muxerType);
    }
}

