#include "widget.h"
#include "ui_widget.h"
#include <QFileDialog>
#include <QDebug>
#include <QThread>

#include <QMessageBox>

void Widget::startConvert_bak(QString inputPath, QString outputPath)
{
    AVFormatContext *ifmtCtx = nullptr;
    AVFormatContext *ofmtCtx = nullptr;
    int ret = 0;

    int videoIndex = -1;
    int audioIndex = -1;
    AVStream *inVideoStream = nullptr;
    AVStream *inAudioStream = nullptr;
    AVStream *outVideoStream = nullptr;
    AVStream *outAudioStream = nullptr;

    AVCodecContext *videoDecCtx = nullptr;
    AVCodecContext *audioDecCtx = nullptr;
    AVCodecContext *videoEncCtx = nullptr;
    AVCodecContext *audioEncCtx = nullptr;

    SwrContext *swrCtx = nullptr;

    // ----------------------
    // 1. 打开输入文件
    // ----------------------
    if (avformat_open_input(&ifmtCtx, inputPath.toLocal8Bit().data(), nullptr, nullptr) < 0) {
        emit finished(false);
        return;
    }
    avformat_find_stream_info(ifmtCtx, nullptr);

    // ----------------------
    // 2. 寻找音视频流
    // ----------------------
    for (int i = 0; i < ifmtCtx->nb_streams; i++) {
        if (ifmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoIndex < 0) {
            videoIndex = i;
        }
        if (ifmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioIndex < 0) {
            audioIndex = i;
        }
    }

    if (videoIndex == -1 || audioIndex == -1) {
        emit finished(false);
        return;
    }

    inVideoStream = ifmtCtx->streams[videoIndex];
    inAudioStream = ifmtCtx->streams[audioIndex];

    // ----------------------
    // 3. 初始化解码器
    // ----------------------
    const AVCodec *videoDec = avcodec_find_decoder(inVideoStream->codecpar->codec_id);
    videoDecCtx = avcodec_alloc_context3(videoDec);
    avcodec_parameters_to_context(videoDecCtx, inVideoStream->codecpar);
    avcodec_open2(videoDecCtx, videoDec, nullptr);

    const AVCodec *audioDec = avcodec_find_decoder(inAudioStream->codecpar->codec_id);
    audioDecCtx = avcodec_alloc_context3(audioDec);
    avcodec_parameters_to_context(audioDecCtx, inAudioStream->codecpar);
    avcodec_open2(audioDecCtx, audioDec, nullptr);

    // ----------------------
    // 4. 创建输出 MP4
    // ----------------------
    avformat_alloc_output_context2(&ofmtCtx, nullptr, "mp4", outputPath.toLocal8Bit().data());

    // ======================
    // 视频编码器：H.265(libx265)
    // ======================
    const AVCodec *videoEnc = avcodec_find_encoder(AV_CODEC_ID_HEVC);
    videoEncCtx = avcodec_alloc_context3(videoEnc);
    videoEncCtx->codec_type = AVMEDIA_TYPE_VIDEO;
    videoEncCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    videoEncCtx->width = videoDecCtx->width;
    videoEncCtx->height = videoDecCtx->height;
    videoEncCtx->time_base = inVideoStream->time_base;
    videoEncCtx->framerate = av_guess_frame_rate(ifmtCtx, inVideoStream, nullptr);
    videoEncCtx->bit_rate = 5000000; // 码率

    av_opt_set(videoEncCtx->priv_data, "preset", "fast", 0);
    av_opt_set(videoEncCtx->priv_data, "crf", "28", 0);

    outVideoStream = avformat_new_stream(ofmtCtx, videoEnc);
    avcodec_parameters_from_context(outVideoStream->codecpar, videoEncCtx);
    outVideoStream->time_base = videoEncCtx->time_base;
    avcodec_open2(videoEncCtx, videoEnc, nullptr);

    // ======================
    // 音频编码器：AAC
    // ======================
    const AVCodec *audioEnc = avcodec_find_encoder(AV_CODEC_ID_AAC);
    audioEncCtx = avcodec_alloc_context3(audioEnc);
    audioEncCtx->codec_type = AVMEDIA_TYPE_AUDIO;
    audioEncCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    audioEncCtx->sample_rate = 44100;
    audioEncCtx->channels = 2;
    audioEncCtx->channel_layout = AV_CH_LAYOUT_STEREO;
    audioEncCtx->bit_rate = 128000;
    audioEncCtx->time_base = {1, audioEncCtx->sample_rate};

    outAudioStream = avformat_new_stream(ofmtCtx, audioEnc);
    avcodec_parameters_from_context(outAudioStream->codecpar, audioEncCtx);
    outAudioStream->time_base = audioEncCtx->time_base;
    avcodec_open2(audioEncCtx, audioEnc, nullptr);

    // 重采样
    swrCtx = swr_alloc_set_opts(nullptr,
                                audioEncCtx->channel_layout, audioEncCtx->sample_fmt, audioEncCtx->sample_rate,
                                audioDecCtx->channel_layout, audioDecCtx->sample_fmt, audioDecCtx->sample_rate,
                                0, nullptr);
    swr_init(swrCtx);

    // ----------------------
    // 5. 写文件头
    // ----------------------
    avio_open(&ofmtCtx->pb, outputPath.toLocal8Bit().data(), AVIO_FLAG_WRITE);
    avformat_write_header(ofmtCtx, nullptr);

    // ----------------------
    // 6. 开始转码
    // ----------------------
    AVPacket pkt;
    while (av_read_frame(ifmtCtx, &pkt) == 0)
    {
        if (pkt.stream_index == videoIndex) {
            // 视频解码 → 编码
            avcodec_send_packet(videoDecCtx, &pkt);
            AVFrame *frame = av_frame_alloc();
            if (avcodec_receive_frame(videoDecCtx, frame) == 0) {
                avcodec_send_frame(videoEncCtx, frame);
                AVPacket encPkt;
                av_init_packet(&encPkt);
                encPkt.data = nullptr;
                encPkt.size = 0;
                if (avcodec_receive_packet(videoEncCtx, &encPkt) == 0) {
                    encPkt.stream_index = outVideoStream->index;
                    av_packet_rescale_ts(&encPkt, videoEncCtx->time_base, outVideoStream->time_base);
                    av_interleaved_write_frame(ofmtCtx, &encPkt);
                    av_packet_unref(&encPkt);
                }
            }
            av_frame_free(&frame);
        }
        else if (pkt.stream_index == audioIndex) {
            // 音频解码 → 重采样 → 编码
            avcodec_send_packet(audioDecCtx, &pkt);
            AVFrame *frame = av_frame_alloc();
            if (avcodec_receive_frame(audioDecCtx, frame) == 0) {
                AVFrame *outFrame = av_frame_alloc();
                outFrame->format = audioEncCtx->sample_fmt;
                outFrame->channels = 2;
                outFrame->channel_layout = AV_CH_LAYOUT_STEREO;
                outFrame->nb_samples = audioEncCtx->frame_size;
                av_frame_get_buffer(outFrame, 0);

                swr_convert(swrCtx,
                            outFrame->data, outFrame->nb_samples,
                            (const uint8_t **)frame->data, frame->nb_samples);

                avcodec_send_frame(audioEncCtx, outFrame);
                AVPacket encPkt;
                av_init_packet(&encPkt);
                encPkt.data = nullptr;
                encPkt.size = 0;
                if (avcodec_receive_packet(audioEncCtx, &encPkt) == 0) {
                    encPkt.stream_index = outAudioStream->index;
                    av_packet_rescale_ts(&encPkt, audioEncCtx->time_base, outAudioStream->time_base);
                    av_interleaved_write_frame(ofmtCtx, &encPkt);
                    av_packet_unref(&encPkt);
                }
                av_frame_free(&outFrame);
            }
            av_frame_free(&frame);
        }
        av_packet_unref(&pkt);

        // 进度
        double prog = pkt.pts * 1.0 / inVideoStream->duration * 100;
        //emit progress(prog > 100 ? 100 : prog);
    }

    // ----------------------
    // 7. 写文件尾 + 释放
    // ----------------------
    av_write_trailer(ofmtCtx);
    avio_close(ofmtCtx->pb);
    avformat_free_context(ofmtCtx);
    avformat_close_input(&ifmtCtx);

    emit finished(true);
}

Widget::Widget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::Widget)
{
    ui->setupUi(this);
    // 允许选中整行
    ui->tableWidget_fileList->setSelectionBehavior(QAbstractItemView::SelectRows);
    // 允许多选：按住 Ctrl / Shift 选多行
    ui->tableWidget_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->tableWidget_fileList->horizontalHeader()->setStyleSheet(R"(
                                                                QHeaderView::section {
                                                                    background-color: #f0f0f0;
                                                                    color: black;
                                                                    font-weight: bold;
                                                                    border: 1px solid #dcdcdc;
                                                                    padding: 4px;
                                                                }
                                                            )");
    ui->tableWidget_fileList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableWidget_fileList->horizontalHeader()->setHighlightSections(false);//点击表头时不对表头光亮
    ui->tableWidget_fileList->horizontalHeader()->setFont(QFont("song", 12));
    // 监听 选中状态改变
    connect(ui->tableWidget_fileList, &QTableWidget::itemSelectionChanged, this, [=]() {
        // 获取选中行数
        int count = ui->tableWidget_fileList->selectedItems().size();

        if (count == 0) {
            // ======================================
            // ✅ 这里就是：没有选中任何行 或 取消选中
            // ======================================
            qDebug() << "当前没有选中任何行！";
            selectRowPaths.clear();

            // 你可以在这里做：
            // 按钮禁用、清空信息、重置状态等
            // ui->pushButton->setEnabled(false);
        } else {
            // 有选中
            qDebug() << "当前有选中行";
            // ui->pushButton->setEnabled(true);

            QModelIndexList rowsList = ui->tableWidget_fileList->selectionModel()->selectedRows(); //光标选中的行

            for(int i = 0 ; i < rowsList.count() ;i++){
                int row = rowsList.at(i).row();
                QString path = ui->tableWidget_fileList->item(row, 7)->text();
                selectRowPaths.append(path);
            }
        }
    });
}

Widget::~Widget()
{
    delete ui;
}

void get_decoder_name(AVGeneralMediaInfo *avmi, AVFormatContext * avFmtCtx, int type)
{
    int index = -1;
    if (type == 0) {
        index = avmi->videoStreamIndex;
    }
    else if (type == 1) {
        index = avmi->audioStreamIndex;
    }

    if (index >= 0) {
        AVCodecParameters *codecpar = NULL;
        const AVCodec *avcodec = NULL;
        codecpar = avFmtCtx->streams[index]->codecpar;
        avcodec= avcodec_find_decoder(codecpar->codec_id);
        if (type == 0) {
            strcpy(avmi->videoCodecName, avcodec->name);
            printf("videoCodecName:%s\n", avmi->videoCodecName);
        }
        else if (type == 1) {
            strcpy(avmi->audioCodecName, avcodec->name);
            printf("audioCodecName:%s\n", avmi->audioCodecName);
        }

    }
}

void Widget::get_avgeneral_mediainfo(AVGeneralMediaInfo *avmi, const char *filepath)
{
    int ret = -1;
    int i;
    AVFormatContext * avFmtCtx = NULL;
    if (avmi == NULL || filepath == NULL) return;

    ret = avformat_open_input(&avFmtCtx, filepath, NULL, NULL);
    if (ret < 0) {
        qDebug() << "error avformat_open_input:" << ret;
        return;
    }

    avformat_find_stream_info(avFmtCtx, NULL);

    av_dump_format(avFmtCtx, 0, filepath, 0);

    avmi->duration = avFmtCtx->duration;
    avmi->totalBitrate = avFmtCtx->bit_rate;

    printf("duration=%lld, totalBitrate=%lld\n", avmi->duration, avmi->totalBitrate);
    for (i = 0; i < avFmtCtx->nb_streams; i++) {
        AVStream *avstmp = avFmtCtx->streams[i];
        if (avstmp->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            avmi->videoStreamIndex = i;
            avmi->width = avstmp->codecpar->width;
            avmi->height = avstmp->codecpar->height;

            if (avstmp->avg_frame_rate.den != 0)
                avmi->frameRate = (double) avstmp->avg_frame_rate.num / (double)avstmp->avg_frame_rate.den;

        }
        else if (avstmp->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            avmi->audioStreamIndex = i;
            avmi->sampleRate = avstmp->codecpar->sample_rate;
            avmi->channels = avstmp->codecpar->channels;

        }
    }

    get_decoder_name(avmi, avFmtCtx, AVMEDIA_TYPE_VIDEO);

    get_decoder_name(avmi, avFmtCtx, AVMEDIA_TYPE_AUDIO);

    avformat_close_input(&avFmtCtx);
}

QTableWidgetItem* makeCenterItem(const QString& text)
{
    QTableWidgetItem* item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignCenter);
    return item;
}

void Widget::on_pushButton_addFile_clicked()
{
//    QFileDialog *fileDialog = new QFileDialog(this);
//    fileDialog->setWindowTitle("打开文件");
//    fileDialog->setDirectory(QDir::currentPath());
//    fileDialog->setNameFilter(tr("video(*.mp4 *.flv *mkv);;All files(*.*)"));

//    if (fileDialog->exec())
//    {

//    }
    QString filePath = QFileDialog::getOpenFileName(
        this,
        "打开视频文件",          // 窗口标题
        QDir::currentPath(),    // 正确的当前目录（不会报错）
        "视频文件 (*.mp4 *.flv *.mkv);;所有文件 (*.*)"  // 过滤器
    );

    // 判断用户是否选择了文件
    if (filePath.isEmpty()) return;

    QFileInfo fileInfo(filePath);

    int rowNum = ui->tableWidget_fileList->rowCount();

    ui->tableWidget_fileList->setRowCount(rowNum + 1);
    ui->tableWidget_fileList->setItem(rowNum, 0, makeCenterItem(fileInfo.fileName()));
    ui->tableWidget_fileList->setItem(rowNum, 1, makeCenterItem(fileInfo.suffix()));

    AVGeneralMediaInfo avmi;
    get_avgeneral_mediainfo(&avmi, filePath.toUtf8().data());

    ui->tableWidget_fileList->setItem(rowNum, 2, makeCenterItem(avmi.videoCodecName));
    ui->tableWidget_fileList->setItem(rowNum, 3, makeCenterItem(avmi.audioCodecName));
    ui->tableWidget_fileList->setItem(rowNum, 4, makeCenterItem(QString("%1").arg(avmi.duration / 1000000.0)));
    ui->tableWidget_fileList->setItem(rowNum, 5, makeCenterItem(QString("%1").arg(avmi.totalBitrate / 1000)));
    ui->tableWidget_fileList->setItem(rowNum, 6, makeCenterItem(QString("%1x%2").arg(avmi.width).arg(avmi.height)));
    ui->tableWidget_fileList->setItem(rowNum, 7, makeCenterItem(filePath));
}

void Widget::on_pushButton_startConcerter_clicked()
{
    // 判断有没有选中
    if (selectRowPaths.size() <= 0) {
        QMessageBox::warning(this, "提示", "请先选择行！");
        return;
    }
    if (ui->lineEdit_out->text().isEmpty())
    {
        QMessageBox::warning(this, "提示", "输出文件路径为空");
        return;
    }


//    // 创建转码对象
//    ConverterTool *conv = new ConverterTool;
//    QThread *thread = new QThread;

//    conv->moveToThread(thread);

//    connect(thread, &QThread::started, [=]() {
//        conv->startConvert(text, ui->lineEdit_out->text() + "/test.mp4");
//    });
//    connect(conv, &ConverterTool::finished, thread, &QThread::quit);
//    connect(conv, &ConverterTool::finished, conv, &ConverterTool::deleteLater);
//    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
//    connect(m_thread, &QThread::finished, this, [=]() {
//        qDebug() << "转换完毕，删除对象";
//    });

//    // 进度条
//    connect(conv, &ConverterTool::progress, ui->progressBar, &QProgressBar::setValue);

//    thread->start();

    // 先判断：正在转码中，不允许重复点击
    if (m_thread && m_thread->isRunning()) {
        QMessageBox::warning(this, "提示", "转码正在进行中，请稍候...");
        return;
    }

    // 创建对象
    m_convert = new ConverterTool;
    m_thread = new QThread;
    m_convert->moveToThread(m_thread);

    connect(this, &Widget::startWork, m_convert, &ConverterTool::doWork);

    // 自动回收（非常重要，不写会泄漏）
    connect(m_convert, &ConverterTool::finished, m_thread, &QThread::quit);
    connect(m_convert, &ConverterTool::finished, m_convert, &ConverterTool::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);
    connect(m_thread, &QThread::finished, this, [=]() {
        m_thread = nullptr;   // 结束后置空
        m_convert = nullptr;
    });

    // 进度条
    connect(m_convert, &ConverterTool::progress, ui->progressBar, &QProgressBar::setValue);

    // 启动
    m_thread->start();

    m_convert->setParam(selectRowPaths, ui->lineEdit_out->text());
    QString resolution = ui->comboBox_resolution->currentText();
    QStringList list = resolution.split("x");    qDebug() << list;
    int width = list.at(0).toInt();
    int height = list.at(1).toInt();

    m_convert->setResolution(width, height);
    m_convert->setMuxerType(ui->comboBox_muxerType->currentText());
    m_convert->setBitrate(ui->comboBox_Bitrate->currentText().toInt() * 1000);
    m_convert->setFrameRate(ui->comboBox_frameRate->currentText().toInt());
    emit startWork();
}

void Widget::on_pushButton_open_clicked()
{
    // 弹出选择目录对话框
    QString savePath = QFileDialog::getExistingDirectory(
        this,                        // 父窗口
        "请选择输出目录",            // 标题
        QDir::currentPath(),            // 默认打开的目录
        QFileDialog::ShowDirsOnly    // 只显示目录
    );

    // 判断用户是否选择了
    if (!savePath.isEmpty()) {
        qDebug() << "选择的目录：" << savePath;
        // 在这里使用 savePath
        ui->lineEdit_out->setText(savePath);
    } else {
        qDebug() << "用户取消选择";
    }
}
