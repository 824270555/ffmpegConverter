#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include "convertertool.h"

namespace Ui {
class Widget;
}

typedef struct __AVGeneralMediaInfo {
    char filePath[1024];
    int64_t duration;
    int64_t totalBitrate;
    int videoStreamIndex;
    int audioStreamIndex;
    char videoCodecName[256];
    char audioCodecName[256];
    int width;
    int height;
    double frameRate;

    int sampleRate;
    int channels;
} AVGeneralMediaInfo;

class Widget : public QWidget
{
    Q_OBJECT

public:
    explicit Widget(QWidget *parent = nullptr);
    ~Widget();
    void get_avgeneral_mediainfo(AVGeneralMediaInfo *avmi, const char * filepath);

    void startConvert_bak(QString inputPath, QString outputPath);

signals:
    void finished(bool);
    void startWork();
private slots:
    void on_pushButton_addFile_clicked();

    void on_pushButton_startConcerter_clicked();

    void on_pushButton_open_clicked();

private:
    Ui::Widget *ui;
    ConverterTool *m_convert = nullptr;   // 成员变量，不要局部new
    QThread *m_thread = nullptr;          // 成员变量
    QStringList selectRowPaths;
};

#endif // WIDGET_H
