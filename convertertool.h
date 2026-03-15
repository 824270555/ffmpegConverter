#ifndef CONVERTERTOOL_H
#define CONVERTERTOOL_H

#include <QObject>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libswresample/swresample.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libavutil/audio_fifo.h>
}

class ConverterTool : public QObject
{
    Q_OBJECT
public:
    explicit ConverterTool(QObject *parent = nullptr);
    void startConvert(QString inputPath, QString outputPath);
    void setParam(QStringList inputPaths, QString outputPath);
    void setResolution(int width, int height);
    void setMuxerType(QString type);
    void setBitrate(int64_t bitrate);
    void setFrameRate(int frameRate);
    void setVideoEncode(QString videoEncode);
    void setAudioEncode(QString audioEncode);

private:
    QStringList m_inPaths;
    QString m_outPath;
    int m_width;
    int m_height;
    QString m_muxerType;
    int64_t m_bitrate;
    int m_frameRate;
    QString m_videoEncode;
    QString m_audioEncode;
    int64_t m_nextPts;

signals:
    void progress(double percent);    // 进度 0~100
    void finished(bool success);      // 完成信号

public slots:
    void doWork();
};

#endif // CONVERTERTOOL_H
